#include "Framework.h"
#include "Resource.h"
#include "Renderer.h"
#include "PerformanceAnalyzer.h"
#include "ModelLoader.h"
#include <omp.h>

Framework::Framework() : m_hWnd(nullptr), m_hInstance(nullptr), m_pRenderer(nullptr), m_perfAnalyzer(), m_szTitle(), m_szWindowClass()
{

}

Framework::~Framework()
{
}

bool Framework::Initialize(HINSTANCE hInstance, int nCmdShow)
{
    // 전역 문자열을 초기화합니다.
    LoadStringW(hInstance, IDS_APP_TITLE, m_szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_SOFTRENDERERPROJECT, m_szWindowClass, MAX_LOADSTRING);

    // Window Class Register

    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc = Framework::WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_SOFTRENDERERPROJECT));
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = MAKEINTRESOURCEW(IDC_SOFTRENDERERPROJECT);
    wcex.lpszClassName = m_szWindowClass;
    wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    if (!RegisterClassExW(&wcex))
    {
        return FALSE; // 등록 실패 시 즉시 종료
    }

    // Init Class Instance
    m_hWnd = CreateWindowW(m_szWindowClass, m_szTitle, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, this);

    if (!m_hWnd)
    {
        return FALSE;
    }

    ShowWindow(m_hWnd, nCmdShow);
    UpdateWindow(m_hWnd);

    m_pRenderer = std::make_unique<Renderer>();

    if (!m_pRenderer->Initialize(m_hWnd))
    {
        return FALSE;
    }

    m_perfAnalyzer.Initialize();

    if (!ModelLoader::LoadOBJ("assets/teapot.obj", m_model))
    {
        MessageBox(m_hWnd, L"Failed to load Model.", L"Model Load Error", MB_OK);
        return false;
    }

    return TRUE;
}

void Framework::Run()
{
    MSG msg;
    ZeroMemory(&msg, sizeof(msg));

    // 기본 메시지 루프입니다:
    while (msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        int prevFPS = m_perfAnalyzer.GetAvgFPSForSecond();
        // 메시지가 없는 이 시간에 렌더링 코드를 실행!
        m_perfAnalyzer.Update();
        
        if (m_perfAnalyzer.GetAvgFPSForSecond() != prevFPS) {
            // 문자열 버퍼를 준비하고
            wchar_t buffer[100];
            // "SoftrendererProject - FPS: 60" 같은 형식으로 문자열을 만듭니다.
            swprintf_s(buffer, 100, L"%s - AvgFPS: %d", 
                m_szTitle, m_perfAnalyzer.GetAvgFPSForSecond());

            // 창 제목을 설정합니다.
            SetWindowText(m_hWnd, buffer);
        }

        Framework::Update();
        Framework::Render();

        HDC hdc = GetDC(m_hWnd);
        if (m_pRenderer)
        {
            m_pRenderer->Present(hdc);
        }
        ReleaseDC(m_hWnd, hdc);
    }
}

void Framework::Render()
{
    if (!m_pRenderer) return;

    // Buffer Clear
    m_pRenderer->Clear();

    // Transform Matrix(MVP);
    int width = m_pRenderer->GetWidth();
    int height = m_pRenderer->GetHeight();

    // Calculate aspect ratio
    float aspectRatio = static_cast<float>(width) / height;

    SRMath::mat4 scaleMatrix = SRMath::scale({ 0.04f, 0.04f, 0.04f });
    SRMath::mat4 rotationMatrix = SRMath::rotate(PI, { 0.0f, 1.0f, 0.0f }); // 모델 뒤집기
    SRMath::mat4 translationMatrix = SRMath::translate({ 0.0f, -1.5f, -5.0f });
    SRMath::mat4 modelMatrix = translationMatrix * rotationMatrix * scaleMatrix;

    SRMath::mat4 viewMatrix(1.0f); // Camera is at (0, 0, 0);
    SRMath::mat4 projectionMatrix = 
        SRMath::perspective(PI / 3.0f, aspectRatio, 0.1f, 100.f); // 60 FOV
    
    SRMath::vec3 light_dir = 
        SRMath::normalize(SRMath::vec3{ 0.0f, 0.5f, -1.0f });
    
    // Final MVP Matrix
    SRMath::mat4 mvp = projectionMatrix * viewMatrix * modelMatrix;
    
    const auto& positions = m_model.GetPositions();
    const auto& indices = m_model.GetPositionIndices();

    for (size_t i = 0; i < indices.size(); i += 3)
    {
        const auto& v0 = positions[indices[i]];
        const auto& v1 = positions[indices[i + 1]];
        const auto& v2 = positions[indices[i + 2]];

        SRMath::vec3 v0_World = modelMatrix * v0;
        SRMath::vec3 v1_World = modelMatrix * v1;
        SRMath::vec3 v2_World = modelMatrix * v2;

        SRMath::vec3 normal_world = 
            SRMath::normalize(SRMath::cross(v1_World - v0_World, v2_World - v0_World));
        SRMath::vec3 view_dir = SRMath::normalize(v0_World);

        // Back face culling
        if (SRMath::dot(normal_world, view_dir) <= 0)
        {
            continue;
        }

        // Lambersian Light model 
        float intensity = std::max(0.0f, SRMath::dot(normal_world, light_dir));
        SRMath::vec3 base_color = { 1.f, 1.f, 1.0f };
        SRMath::vec3 final_color_vec = base_color * intensity;
        unsigned int final_color = RGB(
            final_color_vec.x * 255.0f, 
            final_color_vec.y * 255.0f,
            final_color_vec.z * 255.0f);

        // Clipping
        SRMath::mat4 vp = projectionMatrix * viewMatrix;
        SRMath::vec4 v0_clip = vp * v0_World;
        SRMath::vec4 v1_clip = vp * v1_World;
        SRMath::vec4 v2_clip = vp * v2_World;

        // 8. 최종적으로 정점들을 화면 좌표로 변환
        SRMath::vec4 clip_coords[] = { v0_clip, v1_clip, v2_clip };
        SRMath::vec2 screen_coords[3];
        float z_depth[3]; // Z-버퍼에 사용할 깊이 값 저장용

        for (int j = 0; j < 3; ++j)
        {
            // 간단한 클리핑: w가 0보다 작거나 같으면 카메라 뒤쪽이므로 그리지 않음
            if (clip_coords[j].w <= 0) continue;

            // 8-1. 원근 분할 (Perspective Division): 클립 공간 -> NDC 공간
            // x, y, z를 w로 나누어 원근을 적용하고 -1 ~ 1 범위로 정규화합니다.
            clip_coords[j].x /= clip_coords[j].w;
            clip_coords[j].y /= clip_coords[j].w;
            clip_coords[j].z /= clip_coords[j].w;

            // 8-2. 뷰포트 변환 (Viewport Transform): NDC 공간 -> 화면 공간
            // -1 ~ 1 범위의 좌표를 실제 창 픽셀 좌표(0~width, 0~height)로 변환합니다.
            screen_coords[j].x = (clip_coords[j].x + 1.0f) * 0.5f * width;
            screen_coords[j].y = (1.0f - clip_coords[j].y) * 0.5f * height; // Y축 뒤집기

            // 8-3. Z-버퍼에 사용할 깊이 값을 저장해 둡니다.
            z_depth[j] = clip_coords[j].z;
        }

        // 9. 계산된 최종 색상과 화면 좌표로 삼각형 그리기
        // 참고: Z-버퍼를 사용하려면 DrawFilledTriangle 함수가 깊이 값도 받아야 합니다.
        m_pRenderer->DrawFilledTriangle(screen_coords[0], screen_coords[1], screen_coords[2], final_color);

    }

    //m_pRenderer->Render();
}

// Framework Logic Update(For Game)
void Framework::Update()
{
    //TODO Logic Update
}

void Framework::Shutdown()
{
    m_pRenderer->Shutdown();
}

LRESULT Framework::HandleMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_COMMAND:
    {
        int wmId = LOWORD(wParam);
        // 메뉴 선택을 구문 분석합니다:
        switch (wmId)
        {
        case IDM_ABOUT:
            DialogBox(m_hInstance, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, Framework::About);
            break;
        case ID_LINEALGORITHM_BRESENHAM:
            m_pRenderer->SetLineAlgorithm(ELineAlgorithm::Bresenham);
            break;
        case ID_LINEALGORITHM_DDA:
            m_pRenderer->SetLineAlgorithm(ELineAlgorithm::DDA);
            break;

        case IDM_EXIT:
            DestroyWindow(hWnd);
            break;
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
    }
    break;
    case WM_PAINT:
    {
        //It is not used anymore
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        EndPaint(hWnd, &ps);
    }
    break;

    case WM_SIZE:
    {
        if (m_pRenderer)
        {
            m_pRenderer->OnResize(hWnd);

            Render();

            HDC hdc = GetDC(hWnd);
            m_pRenderer->Present(hdc);
            ReleaseDC(hWnd, hdc);
        }
    }
    break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

// 메시지 처리기
LRESULT Framework::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    Framework* pFramework = nullptr;

    if (message == WM_CREATE)
    {
        CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
        pFramework = reinterpret_cast<Framework*>(pCreate->lpCreateParams);

        SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)pFramework);
    }
    else {
        pFramework = reinterpret_cast<Framework*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
    }

    if (pFramework)
    {
        return pFramework->HandleMessage(hWnd, message, wParam, lParam);
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}

// 정보 대화 상자의 메시지 처리기입니다.
INT_PTR Framework::About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}


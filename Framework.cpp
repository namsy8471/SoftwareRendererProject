#include "Framework.h"
#include "Resource.h"
#include "Renderer.h"
#include "PerformanceAnalyzer.h"
#include <omp.h>

Framework::Framework() : m_hWnd(nullptr), m_hInstance(nullptr), m_pRenderer(nullptr), 
m_perfAnalyzer(), m_szTitle(), m_szWindowClass()
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

    SRMath::mat4 viewMatrix = SRMath::lookAt(m_cameraPos,
        m_cameraPos + m_cameraforward, SRMath::vec3(0.f, 1.f, 0.f));
    

    SRMath::mat4 projectionMatrix = 
        SRMath::perspective(PI / 3.0f, aspectRatio, 0.01f, 100.f); // 60 FOV
    
    SRMath::vec3 light_dir = 
        SRMath::normalize(SRMath::vec3{ 0.0f, -0.5f, 1.0f });

    m_pRenderer->Render(projectionMatrix, viewMatrix, light_dir);
}

// Framework Logic Update(For Game)
void Framework::Update()
{
    //TODO Logic Update
        // 매 프레임 시간(deltaTime)을 구해서 곱해주어야 일정한 속도로 움직입니다.
    // 여기서는 간단히 고정된 속도로 움직입니다.
    const float moveSpeed = .1f;

    // --- 카메라의 실제 방향 벡터 계산 ---
    // Yaw와 Pitch를 모두 사용하여 3D 방향 벡터를 계산합니다.
    m_cameraforward = {
        cos(m_cameraPitch) * sin(m_cameraYaw),
        sin(m_cameraPitch),
        cos(m_cameraPitch) * cos(m_cameraYaw)
    };
    m_cameraforward = SRMath::normalize(m_cameraforward); // 정규화하여 길이를 1로 만듦

    // 월드의 '위쪽' 방향 벡터
    SRMath::vec3 worldUp = { 0.0f, 1.0f, 0.0f };
    // forward 벡터와 worldUp 벡터를 외적하여 카메라의 '오른쪽' 방향 벡터를 구합니다.
    SRMath::vec3 right = SRMath::normalize(SRMath::cross(m_cameraforward, worldUp));

    if (m_keys['W']) m_cameraPos = m_cameraPos + m_cameraforward * moveSpeed;
    if (m_keys['S']) m_cameraPos = m_cameraPos - m_cameraforward * moveSpeed;
    if (m_keys['A']) m_cameraPos = m_cameraPos - right * moveSpeed;
    if (m_keys['D']) m_cameraPos = m_cameraPos + right * moveSpeed;
}

void Framework::Shutdown()
{
    //m_models.clear();

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

        // Other variables
        case ID_LINEALGORITHM_BRESENHAM:
            m_pRenderer->SetLineAlgorithm(ELineAlgorithm::Bresenham);
            break;
        case ID_LINEALGORITHM_DDA:
            m_pRenderer->SetLineAlgorithm(ELineAlgorithm::DDA);
            break;
        case ID_DEBUG_NORMALVECTOR:
            m_pRenderer->SetDebugNormal();
            break;

        case IDM_ABOUT:
            DialogBox(m_hInstance, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, Framework::About);
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

    // Keyborad Input
    case WM_KEYDOWN:
        m_keys[wParam] = true;
        break;
    case WM_KEYUP:
        m_keys[wParam] = false;
        break;

        // Mouse Input
    case WM_RBUTTONDOWN:
        m_isRightMouseDown = true;
        GetCursorPos(&m_lastMousePos);
        ScreenToClient(hWnd, &m_lastMousePos);
        break;
    case WM_RBUTTONUP:
        m_isRightMouseDown = false;
        break;
    case WM_MOUSEMOVE:
        if (m_isRightMouseDown)
        {
            POINT currentMousePos = { LOWORD(lParam), HIWORD(lParam) };
            float deltaX = currentMousePos.x - m_lastMousePos.x;
            float deltaY = currentMousePos.y - m_lastMousePos.y;

            m_cameraYaw -= deltaX * 0.005f;
            m_cameraPitch -= deltaY * 0.005f;

            if (m_cameraPitch > PI / 2.0f - 0.01f) m_cameraPitch = PI / 2.0f - 0.01f;
            if (m_cameraPitch < -PI / 2.0f + 0.01f) m_cameraPitch = -PI / 2.0f + 0.01f;

            m_lastMousePos = currentMousePos;
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


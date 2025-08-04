#include "Framework.h"
#include <omp.h>
#include "Resource.h"
#include "Renderer.h"
#include "PerformanceAnalyzer.h"
#include "ModelLoader.h"
#include "GameObject.h"
#include "Model.h"

Framework::Framework() = default;
Framework::~Framework() = default;

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

    
    if (!initializeGameobject(SRMath::vec3(0.f, 0.f, 0.f), SRMath::vec3(0.f, 0.f, 0.0f),
        SRMath::vec3(0.04f, 0.04f, 0.04f), "IronMan.obj"))
        return false;

	m_camera.Initialize(SRMath::vec3(0.f, 0.f, 5.f));

    return TRUE;
}

bool Framework::initializeGameobject(const SRMath::vec3& pos, const SRMath::vec3& rotation, const SRMath::vec3& scale, const std::string modelName)
{
    m_gameobject = std::make_shared<GameObject>();
    std::string modelPath = "assets/" + modelName;

    // Load Model
    if (!m_gameobject->Initialize(pos, rotation, scale, ModelLoader::LoadOBJ(modelPath)))
    {
        MessageBox(m_hWnd, L"Failed to load Model.", L"Model Load Error", MB_OK);
        return false;
    }

    m_gameobjects.push_back(std::move(m_gameobject));
    return true;
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

        Framework::Update(m_perfAnalyzer.GetDeltaTime());
        Framework::Render();

        HDC hdc = GetDC(m_hWnd);
        if (m_pRenderer)
        {
            m_pRenderer->Present(hdc);
        }
        ReleaseDC(m_hWnd, hdc);
        

    }
}

// Framework Logic Update(For Game)
void Framework::Update(const float deltaTime)
{
    int width = m_pRenderer->GetWidth();
    int height = m_pRenderer->GetHeight();

    // Calculate aspect ratio
    float aspectRatio = static_cast<float>(width) / height;

    //TODO Logic Update
    m_camera.Update(deltaTime, m_keys, aspectRatio);
    for (const auto& gameObject : m_gameobjects)
    {
		const Frustum& frustum = m_camera.GetFrustum();

        if (frustum.IsAABBInFrustum(gameObject->GetWorldAABB()))
        {
            gameObject->Update(deltaTime);
        }
    }
}

void Framework::Render()
{
    if (!m_pRenderer) return;

    // Buffer Clear
    m_pRenderer->Clear();

    SRMath::vec3 light_dir = 
        SRMath::normalize(SRMath::vec3{ 0.0f, -0.5f, 1.0f });

    m_pRenderer->Render(m_camera.GetProjectionMatrix(), m_camera.GetViewMatrix(), light_dir);
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

            float newYaw = m_camera.GetCameraYaw() - deltaX * 0.005f;
            float newPitch = m_camera.GetCameraPitch() - deltaY * 0.005f;

            m_camera.SetCameraYaw(newYaw);
            m_camera.SetCameraPitch(newPitch);


            if (m_camera.GetCameraPitch() > PI / 2.0f - 0.01f) m_camera.SetCameraPitch(PI / 2.0f - 0.01f);
            if (m_camera.GetCameraPitch() < -PI / 2.0f + 0.01f) m_camera.SetCameraPitch(-PI / 2.0f + 0.01f);

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


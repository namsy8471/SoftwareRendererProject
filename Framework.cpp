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
    // ���� ���ڿ��� �ʱ�ȭ�մϴ�.
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
        return FALSE; // ��� ���� �� ��� ����
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

	m_gameobject = std::make_shared<GameObject>();
    // Load Model
    if (!m_gameobject->Initialize(SRMath::vec3(0.f, 0.f, 0.f), SRMath::vec3(0.f, 0.f, 0.0f)
        , SRMath::vec3(0.04f, 0.04f, 0.04f), ModelLoader::LoadOBJ("assets/IronMan.obj")))
    {
        MessageBox(m_hWnd, L"Failed to load Model.", L"Model Load Error", MB_OK);
        return false;
    }

    m_gameobjects.push_back(std::move(m_gameobject));

	m_camera.Initialize(SRMath::vec3(0.f, 0.f, 5.f));

    return TRUE;
}

void Framework::Run()
{
    MSG msg;
    ZeroMemory(&msg, sizeof(msg));

    // �⺻ �޽��� �����Դϴ�:
    while (msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        int prevFPS = m_perfAnalyzer.GetAvgFPSForSecond();
        // �޽����� ���� �� �ð��� ������ �ڵ带 ����!
        m_perfAnalyzer.Update();

        if (m_perfAnalyzer.GetAvgFPSForSecond() != prevFPS) {
            // ���ڿ� ���۸� �غ��ϰ�
            wchar_t buffer[100];
            // "SoftrendererProject - FPS: 60" ���� �������� ���ڿ��� ����ϴ�.
            swprintf_s(buffer, 100, L"%s - AvgFPS: %d",
                m_szTitle, m_perfAnalyzer.GetAvgFPSForSecond());

            // â ������ �����մϴ�.
            SetWindowText(m_hWnd, buffer);
        }

        Framework::Update(m_perfAnalyzer.GetDeltaTime());
        Framework::Render(m_perfAnalyzer.GetDeltaTime());

        HDC hdc = GetDC(m_hWnd);
        if (m_pRenderer)
        {
            m_pRenderer->Present(hdc);
        }
        ReleaseDC(m_hWnd, hdc);
        

    }
}

void Framework::Render(float deltaTime)
{
    if (!m_pRenderer) return;

    // Buffer Clear
    m_pRenderer->Clear();

    // Transform Matrix(MVP);
    int width = m_pRenderer->GetWidth();
    int height = m_pRenderer->GetHeight();

    // Calculate aspect ratio
    float aspectRatio = static_cast<float>(width) / height;

	SRMath::vec3 camPos = m_camera.GetCameraPos();
	SRMath::vec3 camForward = m_camera.GetCameraForward();

    SRMath::mat4 viewMatrix = SRMath::lookAt(camPos, camPos + camForward,
        SRMath::vec3(0.f, 1.f, 0.f));
    
    SRMath::mat4 projectionMatrix = 
        SRMath::perspective(PI / 3.0f, aspectRatio, 0.1f, 100.f); // 60 FOV
    
    SRMath::vec3 light_dir = 
        SRMath::normalize(SRMath::vec3{ 0.0f, -0.5f, 1.0f });

    m_pRenderer->Render(projectionMatrix, viewMatrix, light_dir, deltaTime);
}

// Framework Logic Update(For Game)
void Framework::Update(float deltaTime)
{
    //TODO Logic Update
	m_camera.Update(deltaTime, m_keys);
    for(const auto& gameObject : m_gameobjects)
    {
        if (gameObject)
        {
            gameObject->Update(deltaTime);
        }
	}
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
        // �޴� ������ ���� �м��մϴ�:
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

            Render(m_perfAnalyzer.GetDeltaTime());

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

// �޽��� ó����
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

// ���� ��ȭ ������ �޽��� ó�����Դϴ�.
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


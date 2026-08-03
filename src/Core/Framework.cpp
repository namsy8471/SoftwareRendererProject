#include "Framework.h"
#include "resource.h"
#include "Renderer/Renderer.h"
#include "Math/Frustum.h"
#include "Scene/GameObject.h"
#include "Graphics/ModelLoader.h"
#include "Graphics/Model.h"
#include "Utils/Utils.h"

Framework::Framework(HINSTANCE hInstance, int nCmdShow)
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
        throw std::runtime_error("Failed to register window class");
    }

    // Init Class Instance
    m_hWnd = CreateWindowW(m_szWindowClass, m_szTitle, WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, this);

    if (!m_hWnd)
    {
        throw std::runtime_error("Failed to create window");
    }

    ShowWindow(m_hWnd, nCmdShow);
    UpdateWindow(m_hWnd);

    m_pRenderer = std::make_unique<Renderer>(m_hWnd);
    m_perfAnalyzer = PerformanceAnalyzer();

    if (!initializeGameobject(SRMath::vec3(0.f, 0.f, 0.f), SRMath::vec3(0.f, 0.f, 0.0f),
        SRMath::vec3(0.04f, 0.04f, 0.04f), "IronMan"))
        throw std::runtime_error("Failed to initialize IronMan GameObject");

    if (!initializeGameobject(SRMath::vec3(-10.f, 0.f, 0.f), SRMath::vec3(0.f, 0.f, 0.0f),
        SRMath::vec3(0.04f, 0.04f, 0.04f), "teapot"))
        throw std::runtime_error("Failed to initialize teapot GameObject");

    m_camera = Camera(SRMath::vec3(0.f, 0.f, -5.f));

    m_lights.push_back(DirectionalLight());
    m_lights.push_back(DirectionalLight{ SRMath::vec3(1.f, 0.f, 0.f), SRMath::vec3(1.0f, 1.0f, 1.0f) });
    m_lights.push_back(DirectionalLight{ SRMath::vec3(0.f, 1.f, -1.f), SRMath::vec3(1.0f, 1.0f, 1.0f) });
}


Framework::~Framework() = default;

bool Framework::initializeGameobject(const SRMath::vec3& pos, const SRMath::vec3& rotation, const SRMath::vec3& scale, const std::string modelName)
{
    try
    {
        std::string modelPath = MakeAssetPath(modelName);
        auto m_gameobject = std::make_shared<GameObject>(pos, rotation, scale, ModelLoader::LoadOBJ(modelPath));

        m_gameobjects.push_back(std::move(m_gameobject));
        return true;
    }
    catch (const std::runtime_error& e)
    {
        std::string errorMessage = e.what();
        std::wstring wErrorMessage(errorMessage.begin(), errorMessage.end());

        MessageBox(m_hWnd, wErrorMessage.c_str(), L"GameObject Creation Error", MB_OK);

        return false;
    }
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

        else {
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
            Framework::Render();

            HDC hdc = GetDC(m_hWnd);
            if (m_pRenderer)
            {
                m_pRenderer->Present(hdc);
            }
            ReleaseDC(m_hWnd, hdc);
        }
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

	m_renderQueue.Clear();

    const Frustum& frustum = m_camera.GetFrustum();
    for (const auto& gameObject : m_gameobjects)
    {
        if (gameObject)
        {
            gameObject->Update(deltaTime, m_isRotateMode);

            if (frustum.IsAABBInFrustum(gameObject->GetWorldAABB()))
            {
                gameObject->SubmitToRenderQueue(m_renderQueue, frustum, m_debugFlags);
            }
        }
    }
}

void Framework::Render()
{
    if (!m_pRenderer) return;

    // Buffer Clear
    m_pRenderer->Clear();

    m_pRenderer->RenderScene(m_renderQueue, m_camera, m_lights);
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
            CheckMenuBox(m_debugFlags.bShowNormal, ID_LINEALGORITHM_BRESENHAM);

            break;
        case ID_LINEALGORITHM_DDA:
            m_pRenderer->SetLineAlgorithm(ELineAlgorithm::DDA);
            CheckMenuBox(m_debugFlags.bShowNormal, ID_LINEALGORITHM_DDA);

            break;
        case ID_DEBUG_NORMALVECTOR:
            m_debugFlags.bShowNormal = !m_debugFlags.bShowNormal;
			CheckMenuBox(m_debugFlags.bShowNormal, ID_DEBUG_NORMALVECTOR);
            break;
        
        case ID_DEBUG_AABB: 
            m_debugFlags.bShowAABB = !m_debugFlags.bShowAABB;
			CheckMenuBox(m_debugFlags.bShowAABB, ID_DEBUG_AABB);
            break;
        
        case ID_DEBUG_WIREFRAME: 
            m_debugFlags.bShowWireframe = !m_debugFlags.bShowWireframe;
			CheckMenuBox(m_debugFlags.bShowWireframe, ID_DEBUG_WIREFRAME);
            break;

        case ID_ANTIALIASING_NONE:
            m_pRenderer->SetAAAlgorithm(EAAAlgorithm::None);
            CheckMenuBox(m_debugFlags.bShowWireframe, ID_DEBUG_WIREFRAME);
            break;

        case ID_ANTIALIASING_FXAA:
			m_pRenderer->SetAAAlgorithm(EAAAlgorithm::FXAA);
            CheckMenuBox(m_debugFlags.bShowWireframe, ID_DEBUG_WIREFRAME);
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
        // It is not used anymore
        // ������ ����� ������ ���� Ȥ�� ���� â�� �� �߹Ƿ� ����. 
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
        if (m_keys[VK_SPACE])
        {
            m_isRotateMode = !m_isRotateMode;
        }
        else if (m_keys[VK_F1])
        {
			m_pRenderer->SetAAAlgorithm(EAAAlgorithm::None);
        }
        else if (m_keys[VK_F2])
        {
            m_pRenderer->SetAAAlgorithm(EAAAlgorithm::FXAA);
        }

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
            auto deltaX = currentMousePos.x - m_lastMousePos.x;
            auto deltaY = currentMousePos.y - m_lastMousePos.y;

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

void Framework::CheckMenuBox(bool isOn, const int& menuID)
{
    
    HMENU hMenu = GetMenu(m_hWnd);

    // CheckMenuItem �Լ��� ȣ���Ͽ� üũ ǥ�ø� ������Ʈ�մϴ�.
    if (isOn)
    {
        // ���°� true�̸�, MF_CHECKED �÷��׷� üũ ǥ�ø� �߰��մϴ�.
        CheckMenuItem(hMenu, menuID, MF_BYCOMMAND | MF_CHECKED);
    }
    else
    {
        // ���°� false�̸�, MF_UNCHECKED �÷��׷� üũ ǥ�ø� �����մϴ�.
        CheckMenuItem(hMenu, menuID, MF_BYCOMMAND | MF_UNCHECKED);
    }

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

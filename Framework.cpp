#include "Framework.h"
#include "Resource.h"
#include "Renderer.h"
#include "PerformanceAnalyzer.h"
#include "ModelLoader.h"

Framework::Framework() : m_hWnd(nullptr), m_hInstance(nullptr), m_pRenderer(nullptr), m_perfAnalyzer(), m_szTitle(), m_szWindowClass()
{

}

Framework::~Framework()
{
}

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

    SRMath::mat4 modelMatrix = SRMath::translate({ 0.0f, -1.0f, 5.0f }) * SRMath::rotate(PI, SRMath::vec3(1.0f, 0, 0)) * SRMath::scale({ 0.01f, 0.01f, 0.01f });
    SRMath::mat4 viewMatrix(1.0f); // Camera is at (0, 0, 0);
    SRMath::mat4 projectionMatrix = SRMath::perspective(PI / 3.0f, aspectRatio, 0.1f, 100.f); // 60 FOV
    
    // Final MVP Matrix
    SRMath::mat4 mvp = projectionMatrix * viewMatrix * modelMatrix;
    
    const auto& positions = m_model.GetPositions();
    for (const auto& pos : positions)
    {
        SRMath::vec4 v = { pos.x, pos.y, pos.z, 1.0f };
        SRMath::vec4 v_clip = mvp * v;

        if (v_clip.w != 0.0f)
        {
            v_clip.x /= v_clip.w;
            v_clip.y /= v_clip.w;
            v_clip.z /= v_clip.w;
        }

        int screenX = static_cast<int>((v_clip.x + 1.0f) * 0.5f * width);
        int screenY = static_cast<int>((1.0f - v_clip.y) * 0.5f * height); // inverse Y
        //int screenY = static_cast<int>((v_clip.y + 1.0f) * 0.5f * height); // inverse Y

        m_pRenderer->DrawPixel(screenX, screenY, RGB(255, 255, 255));
    }


    m_pRenderer->Render();
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
        // �޴� ������ ���� �м��մϴ�:
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


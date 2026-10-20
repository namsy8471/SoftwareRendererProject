#include "Framework.h"
#include "resource.h"
#include "Renderer/Renderer.h"
#include "Utils/Utils.h"

#include <array>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace
{
// GetDC/ReleaseDC is a paired Win32 resource API. A scoped owner makes future
// early returns safe and keeps raw HDC lifetime out of the frame loop.
class WindowDc final
{
  public:
    explicit WindowDc(HWND window) noexcept : m_window(window), m_dc(GetDC(window)) {}
    ~WindowDc()
    {
        if (m_dc)
            ReleaseDC(m_window, m_dc);
    }
    WindowDc(const WindowDc &) = delete;
    WindowDc &operator=(const WindowDc &) = delete;

    [[nodiscard]] HDC Get() const noexcept { return m_dc; }
    [[nodiscard]] explicit operator bool() const noexcept { return m_dc != nullptr; }

  private:
    HWND m_window = nullptr;
    HDC m_dc = nullptr;
};

[[nodiscard]] std::wstring utf8ToWide(std::string_view text)
{
    if (text.empty())
        return {};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                           static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0)
        return L"Asset loading failed";

    std::wstring converted(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                        converted.data(), length);
    return converted;
}

} // namespace

Framework::Framework(HINSTANCE hInstance, int nCmdShow)
{
    initializeWindow(hInstance, nCmdShow);
    m_renderer = std::make_unique<Renderer>();
    if (!resizeRenderTarget(m_hWnd))
        throw std::runtime_error("Failed to initialize render target");
    initializeScene();
}

void Framework::initializeWindow(HINSTANCE hInstance, int nCmdShow)
{
    m_hInstance = hInstance;
    // 리소스 문자열과 창 클래스를 준비한다.
    LoadStringW(hInstance, IDS_APP_TITLE, m_szTitle.data(), static_cast<int>(m_szTitle.size()));
    LoadStringW(hInstance, IDC_SOFTRENDERERPROJECT, m_szWindowClass.data(),
                static_cast<int>(m_szWindowClass.size()));

    // 지정하지 않은 Win32 선택 필드도 0/nullptr여야 하므로 전체를 값 초기화한다.
    WNDCLASSEXW wcex{};

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
    wcex.lpszClassName = m_szWindowClass.data();
    wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    if (!RegisterClassExW(&wcex))
    {
        throw std::runtime_error("Failed to register window class");
    }

    // Init Class Instance
    m_hWnd = CreateWindowW(m_szWindowClass.data(), m_szTitle.data(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, 0,
                           CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, this);

    if (!m_hWnd)
    {
        throw std::runtime_error("Failed to create window");
    }

    // 정렬/Hi-Z와 숨은 와이어는 기본 비활성화다. 기본 와이어는 빠른 X-ray 표시다.
    const HMENU algorithmsMenu = GetSubMenu(GetMenu(m_hWnd), 1);
    if (!algorithmsMenu ||
        !AppendMenuW(algorithmsMenu, MF_STRING | MF_UNCHECKED, ID_RENDER_FRONTTOBACK,
                     L"Front-to-back sorting (experimental)") ||
        !AppendMenuW(algorithmsMenu, MF_STRING | MF_UNCHECKED, ID_RENDER_HIZ,
                     L"Hi-Z culling (experimental)") ||
        !AppendMenuW(algorithmsMenu, MF_STRING | MF_UNCHECKED, ID_RENDER_HIDDEN_WIRE,
                     L"Hidden-line wireframe (depth prepass)"))
        throw std::runtime_error("Failed to initialize rendering options menu");

    ShowWindow(m_hWnd, nCmdShow);
    UpdateWindow(m_hWnd);
}

void Framework::initializeScene()
{
    // 경로 오류는 여기서 사용자 메시지로 바꾼다.
    if (const auto loaded = m_scene.Load(GetExecutableDirectory() / L"assets"); !loaded)
    {
        showAssetError(loaded.error());
        throw std::runtime_error("Failed to initialize demo scene");
    }
}

Framework::~Framework() = default;

bool Framework::resizeRenderTarget(HWND window)
{
    // 프레임 작업은 동기식이다. 이전 DIB가 해제되기 전에 빌린 뷰부터 무효화한다.
    m_renderTarget.Detach();
    RECT clientRect{};
    if (!GetClientRect(window, &clientRect))
    {
        m_presenter.Reset();
        return false;
    }
    const auto resized = m_presenter.Resize(window, static_cast<int>(clientRect.right - clientRect.left),
                                             static_cast<int>(clientRect.bottom - clientRect.top));
    if (!resized)
        return false;
    if (!m_renderTarget.Attach(m_presenter.Surface()))
    {
        // 색상 DIB 생성 후 깊이/ID 할당만 실패해도 낡거나 부분적인 화면을 표시하지 않는다.
        m_presenter.Reset();
        return false;
    }
    return true;
}

void Framework::showAssetError(const AssetLoadError &error) const
{
    // 로더는 예외나 UI 호출 대신 구조화 오류를 반환한다. Framework만이
    // WinAPI 사용자 메시지로 변환하므로 로더 테스트와 재사용이 쉬워진다.
    std::wstring message = utf8ToWide(error.message);
    if (!error.path.empty())
        message += L"\nPath: " + error.path.wstring();
    if (error.line != 0)
        message += L"\nLine: " + std::to_wstring(error.line);
    MessageBoxW(m_hWnd, message.c_str(), L"GameObject Creation Error", MB_OK | MB_ICONERROR);
}

void Framework::Run()
{
    MSG message{};
    while (message.message != WM_QUIT)
    {
        if (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&message);
            DispatchMessage(&message);
        }
        else
        {
            runFrame();
        }
    }
}

void Framework::runFrame()
{
    updateFrameTime();
    Update(m_perfAnalyzer.GetDeltaTime());
    Render();
    const WindowDc screenDc{m_hWnd};
    if (m_renderer && screenDc)
        m_presenter.Present(screenDc.Get());
}

void Framework::updateFrameTime()
{
    const int previousFps = m_perfAnalyzer.GetAvgFPSForSecond();
    m_perfAnalyzer.Update();
    if (previousFps == m_perfAnalyzer.GetAvgFPSForSecond())
        return;
    std::array<wchar_t, maxLoadString> title{};
    swprintf_s(title.data(), title.size(), L"%s - AvgFPS: %d", m_szTitle.data(),
               m_perfAnalyzer.GetAvgFPSForSecond());
    SetWindowText(m_hWnd, title.data());
}

// Framework Logic Update(For Game)
void Framework::Update(const float deltaTime)
{
    if (!m_renderer)
        return;
    m_scene.Update(deltaTime, m_renderTarget.Width(), m_renderTarget.Height(), m_keys);
}

void Framework::Render()
{
    if (!m_renderer)
        return;

    // Buffer Clear
    m_renderTarget.Clear();

    m_renderer->RenderScene(m_renderTarget, m_scene.GetRenderQueue(), m_scene.GetCamera(), m_scene.GetLights());
}

LRESULT Framework::HandleMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_COMMAND:
    {
        if (!handleMenuCommand(LOWORD(wParam)))
            return DefWindowProc(hWnd, message, wParam, lParam);
    }
    break;
    case WM_PAINT:
    {
        // invalid region을 검증하여 WM_PAINT가 반복 발생하지 않게 한다.
        PAINTSTRUCT ps{};
        BeginPaint(hWnd, &ps);
        EndPaint(hWnd, &ps);
    }
    break;

    case WM_SIZE:
    {
        if (m_renderer)
        {
            if (!resizeRenderTarget(hWnd))
                break;

            Update(0.f);
            Render();

            const WindowDc screenDc{hWnd};
            if (screenDc)
                m_presenter.Present(screenDc.Get());
        }
    }
    break;

    // Keyborad Input
    case WM_KEYDOWN:
        handleKeyDown(wParam, (lParam & (1LL << 30)) != 0);
        break;
    case WM_KEYUP:
        if (wParam < m_keys.size())
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
        handleMouseMove(lParam);
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

bool Framework::handleMenuCommand(int commandId)
{
    if (!m_renderer)
        return false;
    // 메뉴 선택을 구문 분석합니다:
    switch (commandId)
    {

    // Other variables
    case ID_LINEALGORITHM_BRESENHAM:
        m_renderer->SetLineAlgorithm(ELineAlgorithm::Bresenham);
        CheckMenuBox(true, ID_LINEALGORITHM_BRESENHAM);
        CheckMenuBox(false, ID_LINEALGORITHM_DDA);

        break;
    case ID_LINEALGORITHM_DDA:
        m_renderer->SetLineAlgorithm(ELineAlgorithm::DDA);
        CheckMenuBox(false, ID_LINEALGORITHM_BRESENHAM);
        CheckMenuBox(true, ID_LINEALGORITHM_DDA);

        break;
    case ID_DEBUG_NORMALVECTOR:
    {
        auto &flags = m_scene.GetDebugFlags();
        flags.bShowNormal = !flags.bShowNormal;
        CheckMenuBox(flags.bShowNormal, ID_DEBUG_NORMALVECTOR);
        break;
    }
    case ID_DEBUG_AABB:
    {
        auto &flags = m_scene.GetDebugFlags();
        flags.bShowAABB = !flags.bShowAABB;
        CheckMenuBox(flags.bShowAABB, ID_DEBUG_AABB);
        break;
    }
    case ID_DEBUG_WIREFRAME:
    {
        auto &flags = m_scene.GetDebugFlags();
        flags.bShowWireframe = !flags.bShowWireframe;
        CheckMenuBox(flags.bShowWireframe, ID_DEBUG_WIREFRAME);
        break;
    }
    case ID_DEBUG_LIGHTDIRECTION:
    {
        auto &flags = m_scene.GetDebugFlags();
        flags.bShowLightDirection = !flags.bShowLightDirection;
        CheckMenuBox(flags.bShowLightDirection, ID_DEBUG_LIGHTDIRECTION);
        break;
    }

    case ID_ANTIALIASING_NONE:
        m_renderer->SetAAAlgorithm(EAAAlgorithm::None);
        CheckMenuBox(true, ID_ANTIALIASING_NONE);
        CheckMenuBox(false, ID_ANTIALIASING_FXAA);
        break;

    case ID_ANTIALIASING_FXAA:
        m_renderer->SetAAAlgorithm(EAAAlgorithm::FXAA);
        CheckMenuBox(false, ID_ANTIALIASING_NONE);
        CheckMenuBox(true, ID_ANTIALIASING_FXAA);
        break;

    case ID_RENDER_FRONTTOBACK:
    {
        auto options = m_renderer->GetOptions();
        options.frontToBack = !options.frontToBack;
        m_renderer->SetOptions(options);
        CheckMenuBox(options.frontToBack, ID_RENDER_FRONTTOBACK);
        break;
    }
    case ID_RENDER_HIZ:
    {
        auto options = m_renderer->GetOptions();
        options.hiZ = !options.hiZ;
        m_renderer->SetOptions(options);
        CheckMenuBox(options.hiZ, ID_RENDER_HIZ);
        break;
    }
    case ID_RENDER_HIDDEN_WIRE:
    {
        auto options = m_renderer->GetOptions();
        options.hiddenWireframe = !options.hiddenWireframe;
        m_renderer->SetOptions(options);
        CheckMenuBox(options.hiddenWireframe, ID_RENDER_HIDDEN_WIRE);
        break;
    }

    case IDM_ABOUT:
        DialogBox(m_hInstance, MAKEINTRESOURCE(IDD_ABOUTBOX), m_hWnd, Framework::About);
        break;
    case IDM_EXIT:
        DestroyWindow(m_hWnd);
        break;
    default:
        return false;
    }
    return true;
}
void Framework::handleMouseMove(LPARAM position)
{
    if (m_isRightMouseDown)
    {
        // 마우스 좌표는 음수일 수 있으므로 16-bit signed 좌표로 해석한다.
        POINT currentMousePos = {static_cast<short>(LOWORD(position)), static_cast<short>(HIWORD(position))};
        auto deltaX = currentMousePos.x - m_lastMousePos.x;
        auto deltaY = currentMousePos.y - m_lastMousePos.y;

        Camera &camera = m_scene.GetCamera();
        float newYaw = camera.GetCameraYaw() - deltaX * 0.005f;
        float newPitch = camera.GetCameraPitch() - deltaY * 0.005f;

        camera.SetCameraYaw(newYaw);
        camera.SetCameraPitch(newPitch);

        constexpr float pitchMargin = 0.01f;
        constexpr float halfPi = std::numbers::pi_v<float> / 2.0f;
        camera.SetCameraPitch(std::clamp(camera.GetCameraPitch(), -halfPi + pitchMargin, halfPi - pitchMargin));

        m_lastMousePos = currentMousePos;
    }
}
void Framework::handleKeyDown(WPARAM key, bool repeated)
{
    if (key >= m_keys.size())
        return;
    m_keys[key] = true;
    if (repeated)
        return;
    if (key == VK_SPACE)
        m_scene.SetRotateMode(!m_scene.IsRotateMode());
    else if (key == VK_F1)
        handleMenuCommand(ID_ANTIALIASING_NONE);
    else if (key == VK_F2)
        handleMenuCommand(ID_ANTIALIASING_FXAA);
}

void Framework::CheckMenuBox(bool isOn, int menuID) const noexcept
{
    const HMENU hMenu = GetMenu(m_hWnd);

    // CheckMenuItem 함수를 호출하여 체크 표시를 업데이트합니다.
    if (isOn)
    {
        // 상태가 true이면, MF_CHECKED 플래그로 체크 표시를 추가합니다.
        CheckMenuItem(hMenu, menuID, MF_BYCOMMAND | MF_CHECKED);
    }
    else
    {
        // 상태가 false이면, MF_UNCHECKED 플래그로 체크 표시를 제거합니다.
        CheckMenuItem(hMenu, menuID, MF_BYCOMMAND | MF_UNCHECKED);
    }
}

// 메시지 처리기
LRESULT Framework::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    Framework *pFramework = nullptr;

    if (message == WM_CREATE)
    {
        CREATESTRUCT *pCreate = reinterpret_cast<CREATESTRUCT *>(lParam);
        pFramework = reinterpret_cast<Framework *>(pCreate->lpCreateParams);

        SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)pFramework);
    }
    else
    {
        pFramework = reinterpret_cast<Framework *>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
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

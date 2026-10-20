#include "App/Win32App.h"
#include "resource.h"
#include "Core/DemoScene.h"
#include "Renderer/Renderer.h"
#include "Utils/Utils.h"

#include <algorithm>
#include <array>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>
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

// 켜고 끄는 메뉴 항목 표. 라디오 그룹(선 알고리즘, AA)은 handleMenuCommand의 switch에 있다.
struct DebugFlagMenu
{
    int commandId;
    bool DebugFlags::*flag;
};
constexpr std::array debugFlagMenus{DebugFlagMenu{ID_DEBUG_NORMALVECTOR, &DebugFlags::bShowNormal},
                                    DebugFlagMenu{ID_DEBUG_AABB, &DebugFlags::bShowAABB},
                                    DebugFlagMenu{ID_DEBUG_WIREFRAME, &DebugFlags::bShowWireframe},
                                    DebugFlagMenu{ID_DEBUG_LIGHTDIRECTION, &DebugFlags::bShowLightDirection}};

struct RenderOptionMenu
{
    int commandId;
    bool RenderOptions::*option;
    const wchar_t *label; // 리소스 메뉴에 없어 시작 시 Algorithms 메뉴 끝에 추가한다.
};
constexpr std::array renderOptionMenus{
    RenderOptionMenu{ID_RENDER_FRONTTOBACK, &RenderOptions::frontToBack, L"Front-to-back sorting (experimental)"},
    RenderOptionMenu{ID_RENDER_HIZ, &RenderOptions::hiZ, L"Hi-Z culling (experimental)"},
    RenderOptionMenu{ID_RENDER_HIDDEN_WIRE, &RenderOptions::hiddenWireframe,
                     L"Hidden-line wireframe (depth prepass)"}};
} // namespace

Win32App::Win32App(HINSTANCE hInstance, int nCmdShow)
{
    initializeWindow(hInstance, nCmdShow);
    if (!resizeRenderTarget(m_hWnd))
        throw std::runtime_error("Failed to initialize render target");
    initializeScene();
    m_isInitialized = true;
}

Win32App::~Win32App() = default;

void Win32App::initializeWindow(HINSTANCE hInstance, int nCmdShow)
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
    wcex.lpfnWndProc = Win32App::WndProc;
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

    m_hWnd = CreateWindowW(m_szWindowClass.data(), m_szTitle.data(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, 0,
                           CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, this);

    if (!m_hWnd)
    {
        throw std::runtime_error("Failed to create window");
    }

    // 정렬/Hi-Z와 숨은 와이어는 기본 비활성화다. 기본 와이어는 빠른 X-ray 표시다.
    const HMENU algorithmsMenu = GetSubMenu(GetMenu(m_hWnd), 1);
    if (!algorithmsMenu)
        throw std::runtime_error("Failed to initialize rendering options menu");
    for (const auto &menu : renderOptionMenus)
        if (!AppendMenuW(algorithmsMenu, MF_STRING | MF_UNCHECKED, menu.commandId, menu.label))
            throw std::runtime_error("Failed to initialize rendering options menu");

    ShowWindow(m_hWnd, nCmdShow);
    UpdateWindow(m_hWnd);
}

void Win32App::initializeScene()
{
    // 경로 오류는 여기서 사용자 메시지로 바꾼다.
    auto loaded = sr::demo::LoadScene(GetExecutableDirectory() / L"assets");
    if (!loaded)
    {
        showAssetError(loaded.error());
        throw std::runtime_error("Failed to initialize demo scene");
    }
    m_runner.SetScene(std::move(*loaded));
}

bool Win32App::resizeRenderTarget(HWND window)
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

void Win32App::showAssetError(const AssetLoadError &error) const
{
    // 로더는 예외나 UI 호출 대신 구조화 오류를 반환한다. Win32App만이
    // WinAPI 사용자 메시지로 변환하므로 로더 테스트와 재사용이 쉬워진다.
    std::wstring message = utf8ToWide(error.message);
    if (!error.path.empty())
        message += L"\nPath: " + error.path.wstring();
    if (error.line != 0)
        message += L"\nLine: " + std::to_wstring(error.line);
    MessageBoxW(m_hWnd, message.c_str(), L"GameObject Creation Error", MB_OK | MB_ICONERROR);
}

void Win32App::Run()
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

void Win32App::runFrame()
{
    updateFrameTime();
    updateFrame(m_perfAnalyzer.GetDeltaTime());
    renderFrame();
    presentFrame(m_hWnd);
}

void Win32App::updateFrame(float deltaTime)
{
    m_runner.Update(deltaTime, m_renderTarget.Width(), m_renderTarget.Height(), m_keys);
}

void Win32App::renderFrame()
{
    m_runner.Render(m_renderTarget);
}

void Win32App::presentFrame(HWND window)
{
    const WindowDc screenDc{window};
    if (screenDc)
        m_presenter.Present(screenDc.Get());
}

void Win32App::updateFrameTime()
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

LRESULT Win32App::HandleMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_COMMAND:
        if (!handleMenuCommand(LOWORD(wParam)))
            return DefWindowProc(hWnd, message, wParam, lParam);
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
        if (m_isInitialized && resizeRenderTarget(hWnd))
        {
            updateFrame(0.f);
            renderFrame();
            presentFrame(hWnd);
        }
        break;

    // Keyboard Input
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

bool Win32App::handleMenuCommand(int commandId)
{
    if (!m_isInitialized)
        return false;
    Renderer &renderer = m_runner.GetRenderer();

    for (const auto &menu : debugFlagMenus)
    {
        if (menu.commandId != commandId)
            continue;
        bool &flag = m_runner.GetDebugFlags().*menu.flag;
        flag = !flag;
        checkMenuItem(commandId, flag);
        return true;
    }
    for (const auto &menu : renderOptionMenus)
    {
        if (menu.commandId != commandId)
            continue;
        auto options = renderer.GetOptions();
        options.*menu.option = !(options.*menu.option);
        renderer.SetOptions(options);
        checkMenuItem(commandId, options.*menu.option);
        return true;
    }

    switch (commandId)
    {
    case ID_LINEALGORITHM_BRESENHAM:
    case ID_LINEALGORITHM_DDA:
    {
        const bool dda = commandId == ID_LINEALGORITHM_DDA;
        renderer.SetLineAlgorithm(dda ? ELineAlgorithm::DDA : ELineAlgorithm::Bresenham);
        checkMenuItem(ID_LINEALGORITHM_BRESENHAM, !dda);
        checkMenuItem(ID_LINEALGORITHM_DDA, dda);
        break;
    }
    case ID_ANTIALIASING_NONE:
    case ID_ANTIALIASING_FXAA:
    {
        const bool fxaa = commandId == ID_ANTIALIASING_FXAA;
        renderer.SetAAAlgorithm(fxaa ? EAAAlgorithm::FXAA : EAAAlgorithm::None);
        checkMenuItem(ID_ANTIALIASING_NONE, !fxaa);
        checkMenuItem(ID_ANTIALIASING_FXAA, fxaa);
        break;
    }
    case IDM_ABOUT:
        DialogBox(m_hInstance, MAKEINTRESOURCE(IDD_ABOUTBOX), m_hWnd, Win32App::About);
        break;
    case IDM_EXIT:
        DestroyWindow(m_hWnd);
        break;
    default:
        return false;
    }
    return true;
}

void Win32App::handleMouseMove(LPARAM position)
{
    if (!m_isRightMouseDown)
        return;
    // 마우스 좌표는 음수일 수 있으므로 16-bit signed 좌표로 해석한다.
    const POINT currentMousePos = {static_cast<short>(LOWORD(position)), static_cast<short>(HIWORD(position))};
    const auto deltaX = currentMousePos.x - m_lastMousePos.x;
    const auto deltaY = currentMousePos.y - m_lastMousePos.y;

    Camera &camera = m_runner.GetScene().GetCamera();
    camera.SetCameraYaw(camera.GetCameraYaw() - deltaX * 0.005f);

    constexpr float pitchMargin = 0.01f;
    constexpr float halfPi = std::numbers::pi_v<float> / 2.0f;
    camera.SetCameraPitch(
        std::clamp(camera.GetCameraPitch() - deltaY * 0.005f, -halfPi + pitchMargin, halfPi - pitchMargin));

    m_lastMousePos = currentMousePos;
}

void Win32App::handleKeyDown(WPARAM key, bool repeated)
{
    if (key >= m_keys.size())
        return;
    m_keys[key] = true;
    if (repeated)
        return;
    if (key == VK_SPACE)
    {
        Scene &scene = m_runner.GetScene();
        scene.SetRotateMode(!scene.IsRotateMode());
    }
    else if (key == VK_F1)
        handleMenuCommand(ID_ANTIALIASING_NONE);
    else if (key == VK_F2)
        handleMenuCommand(ID_ANTIALIASING_FXAA);
}

void Win32App::checkMenuItem(int menuId, bool checked) const noexcept
{
    CheckMenuItem(GetMenu(m_hWnd), menuId, MF_BYCOMMAND | (checked ? MF_CHECKED : MF_UNCHECKED));
}

// 메시지 처리기
LRESULT Win32App::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    Win32App *app = nullptr;

    if (message == WM_CREATE)
    {
        const auto *create = reinterpret_cast<CREATESTRUCT *>(lParam);
        app = reinterpret_cast<Win32App *>(create->lpCreateParams);
        SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    else
    {
        app = reinterpret_cast<Win32App *>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
    }

    if (app)
        return app->HandleMessage(hWnd, message, wParam, lParam);

    return DefWindowProc(hWnd, message, wParam, lParam);
}

// 정보 대화 상자의 메시지 처리기입니다.
INT_PTR Win32App::About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
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

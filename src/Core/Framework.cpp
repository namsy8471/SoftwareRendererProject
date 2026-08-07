#include "Framework.h"
#include "resource.h"
#include "Renderer/Renderer.h"
#include "Math/Frustum.h"
#include "Scene/GameObject.h"
#include "Graphics/ModelLoader.h"
#include "Graphics/Model.h"
#include "Utils/Utils.h"

#include <array>
#include <numbers>
#include <utility>

namespace
{
constexpr float kLightGizmoLength = 1.5f;
constexpr float kArrowHeadLength = 0.3f;
constexpr float kArrowHeadWidth = 0.18f;

// GetDC/ReleaseDC is a paired Win32 resource API. A scoped owner makes future
// early returns safe and keeps raw HDC lifetime out of the frame loop.
class WindowDc final
{
public:
    explicit WindowDc(HWND window) noexcept : m_window(window), m_dc(GetDC(window)) {}
    ~WindowDc() { if (m_dc) ReleaseDC(m_window, m_dc); }
    WindowDc(const WindowDc&) = delete;
    WindowDc& operator=(const WindowDc&) = delete;

    [[nodiscard]] HDC get() const noexcept { return m_dc; }
    [[nodiscard]] explicit operator bool() const noexcept { return m_dc != nullptr; }

private:
    HWND m_window = nullptr;
    HDC m_dc = nullptr;
};

[[nodiscard]] std::wstring Utf8ToWide(std::string_view text)
{
    if (text.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0) return L"Asset loading failed";

    std::wstring converted(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
        static_cast<int>(text.size()), converted.data(), length);
    return converted;
}

void AddDebugLine(DebugPrimitiveCommand& command, const SRMath::vec3& from,
    const SRMath::vec3& to, const SRMath::vec4& color)
{
    command.vertices.push_back({ from, color });
    command.vertices.push_back({ to, color });
}

void SubmitDirectionalLightGizmos(RenderQueue& renderQueue, std::span<const DirectionalLight> lights)
{
    for (std::size_t index = 0; index < lights.size(); ++index)
    {
        const auto& light = lights[index];
        const SRMath::vec3 rayDirection = SRMath::normalize(light.rayDirection);
        const SRMath::vec3 anchor{ -3.0f + static_cast<float>(index) * 3.0f, 3.0f, 0.0f };
        const SRMath::vec3 tip = anchor + rayDirection * kLightGizmoLength;
        const SRMath::vec3 up = std::abs(rayDirection.y) > 0.9f
            ? SRMath::vec3{ 1.0f, 0.0f, 0.0f }
            : SRMath::vec3{ 0.0f, 1.0f, 0.0f };
        const SRMath::vec3 side = SRMath::normalize(SRMath::cross(rayDirection, up));

        DebugPrimitiveCommand command;
        command.worldTransform = SRMath::mat4::identity();
        command.type = DebugPrimitiveType::Line;

        // 노란색: DirectionalLight가 정의한 실제 광선 진행 방향.
        const SRMath::vec4 rayColor{ 1.0f, 0.85f, 0.0f, 1.0f };
        AddDebugLine(command, anchor, tip, rayColor);
        AddDebugLine(command, tip, tip - rayDirection * kArrowHeadLength + side * kArrowHeadWidth, rayColor);
        AddDebugLine(command, tip, tip - rayDirection * kArrowHeadLength - side * kArrowHeadWidth, rayColor);

        // 청록색: Phong 난반사 계산에 사용되는 surface-to-light 방향.
        const SRMath::vec4 shadingColor{ 0.0f, 0.85f, 1.0f, 1.0f };
        AddDebugLine(command, anchor, anchor - rayDirection * (kLightGizmoLength * 0.55f), shadingColor);

        renderQueue.Submit(std::move(command));
    }
}
}

Framework::Framework(HINSTANCE hInstance, int nCmdShow)
{
    // 전역 문자열을 초기화합니다.
    LoadStringW(hInstance, IDS_APP_TITLE, m_szTitle.data(), static_cast<int>(m_szTitle.size()));
    LoadStringW(hInstance, IDC_SOFTRENDERERPROJECT, m_szWindowClass.data(), static_cast<int>(m_szWindowClass.size()));

    // Window Class Register
    // 값 초기화로 모든 필드를 0/nullptr로 만든다. C의 ZeroMemory보다 타입 안전하다.
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
    m_hWnd = CreateWindowW(m_szWindowClass.data(), m_szTitle.data(), WS_OVERLAPPEDWINDOW,
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
    //m_lights.push_back(DirectionalLight{ SRMath::vec3(1.f, 0.f, 0.f), SRMath::vec3(1.0f, 1.0f, 1.0f) });
    //m_lights.push_back(DirectionalLight{ SRMath::vec3(0.f, 1.f, -1.f), SRMath::vec3(1.0f, 1.0f, 1.0f) });
}


Framework::~Framework() = default;

bool Framework::initializeGameobject(const SRMath::vec3& pos, const SRMath::vec3& rotation,
    const SRMath::vec3& scale, std::string_view modelName)
{
    const auto modelPath = MakeAssetPath(modelName);
    auto model = ModelLoader::LoadOBJ(modelPath);
    if (!model)
    {
        // 로더는 예외나 UI 호출 대신 구조화 오류를 반환한다. Framework만이
        // WinAPI 사용자 메시지로 변환하므로 로더 테스트와 재사용이 쉬워진다.
        std::wstring message = Utf8ToWide(model.error().message);
        if (!model.error().path.empty())
            message += L"\nPath: " + model.error().path.wstring();
        if (model.error().line != 0)
            message += L"\nLine: " + std::to_wstring(model.error().line);
        MessageBoxW(m_hWnd, message.c_str(), L"GameObject Creation Error", MB_OK | MB_ICONERROR);
        return false;
    }

    auto gameObject = std::make_shared<GameObject>(pos, rotation, scale, std::move(*model));
    m_gameobjects.push_back(std::move(gameObject));
    return true;
}

void Framework::Run()
{
    MSG msg{};

    // 기본 메시지 루프입니다:
    while (msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        else {
            int prevFPS = m_perfAnalyzer.GetAvgFPSForSecond();
            // 메시지가 없는 이 시간에 렌더링 코드를 실행!
            m_perfAnalyzer.Update();

            if (m_perfAnalyzer.GetAvgFPSForSecond() != prevFPS) {
                // 문자열 버퍼를 준비하고
                std::array<wchar_t, max_load_string> buffer{};
                // "SoftrendererProject - FPS: 60" 같은 형식으로 문자열을 만듭니다.
                swprintf_s(buffer.data(), buffer.size(), L"%s - AvgFPS: %d",
                    m_szTitle.data(), m_perfAnalyzer.GetAvgFPSForSecond());

                // 창 제목을 설정합니다.
                SetWindowText(m_hWnd, buffer.data());
            }

            Framework::Update(m_perfAnalyzer.GetDeltaTime());
            Framework::Render();

            const WindowDc screenDc{ m_hWnd };
            if (m_pRenderer && screenDc)
            {
                m_pRenderer->Present(screenDc.get());
            }
        }
    }
}

// Framework Logic Update(For Game)
void Framework::Update(const float deltaTime)
{
    const int width = m_pRenderer->GetWidth();
    const int height = m_pRenderer->GetHeight();
    if (width <= 0 || height <= 0) return; // minimized windows have no valid aspect ratio

    const float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
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

    if (m_debugFlags.bShowLightDirection)
        SubmitDirectionalLightGizmos(m_renderQueue, m_lights);
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
        // 메뉴 선택을 구문 분석합니다:
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

        case ID_DEBUG_LIGHTDIRECTION:
            m_debugFlags.bShowLightDirection = !m_debugFlags.bShowLightDirection;
            CheckMenuBox(m_debugFlags.bShowLightDirection, ID_DEBUG_LIGHTDIRECTION);
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
        // 하지만 지우면 윈도우 에러 혹은 헬프 창이 안 뜨므로 남김.
        PAINTSTRUCT ps{};
        BeginPaint(hWnd, &ps);
        EndPaint(hWnd, &ps);
    }
    break;

    case WM_SIZE:
    {
        if (m_pRenderer)
        {
            m_pRenderer->OnResize(hWnd);

            Render();

            const WindowDc screenDc{ hWnd };
            if (screenDc) m_pRenderer->Present(screenDc.get());
        }
    }
    break;

    // Keyborad Input
    case WM_KEYDOWN:
        // WPARAM은 배열 범위보다 클 수 있으므로 span 전환과 함께 경계를 검사한다.
        if (wParam >= m_keys.size()) break;
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
        if (wParam < m_keys.size()) m_keys[wParam] = false;
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


            constexpr float pitchMargin = 0.01f;
            constexpr float halfPi = std::numbers::pi_v<float> / 2.0f;
            m_camera.SetCameraPitch(std::clamp(m_camera.GetCameraPitch(),
                -halfPi + pitchMargin, halfPi - pitchMargin));

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

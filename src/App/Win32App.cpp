#include "App/Win32App.h"
#include "resource.h"
#include "Core/DemoScene.h"
#include "Animation/Animator.h"
#include "Graphics/GltfLoader.h"
#include "Graphics/Model.h"
#include "Graphics/ModelLoader.h"
#include "Renderer/Renderer.h"
#include "Scene/GameObject.h"
#include "Scene/ScenePicking.h"
#include "Utils/AssetLoadError.h"
#include "Utils/Utils.h"

#include <commdlg.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cwctype>
#include <expected>
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

[[nodiscard]] bool sceneContainsObject(const GameObject &node, const GameObject *target) noexcept
{
    if (&node == target)
        return true;
    for (const auto &child : node.GetChildren())
        if (child && sceneContainsObject(*child, target))
            return true;
    return false;
}

[[nodiscard]] bool isObjectInScene(const Scene &scene, const std::shared_ptr<GameObject> &target) noexcept
{
    if (!target)
        return false;
    for (const auto &root : scene.GetRootObjects())
        if (root && sceneContainsObject(*root, target.get()))
            return true;
    return false;
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
                     L"Hidden-line wireframe (depth prepass)"},
    RenderOptionMenu{ID_RENDER_BILINEAR, &RenderOptions::bilinearFiltering, L"Bilinear texture filtering"},
    RenderOptionMenu{ID_RENDER_MIPMAP, &RenderOptions::mipmapping, L"Mipmap texture filtering"},
    RenderOptionMenu{ID_RENDER_SRGB, &RenderOptions::srgbGamma, L"sRGB gamma correction"},
    RenderOptionMenu{ID_RENDER_SHADOWS, &RenderOptions::shadows, L"Shadow mapping (2x2 PCF)"},
    RenderOptionMenu{ID_RENDER_PBR, &RenderOptions::pbr, L"Physically Based Rendering (PBR)"},
    RenderOptionMenu{ID_RENDER_IBL, &RenderOptions::ibl, L"Image-Based Lighting (IBL)"},
    RenderOptionMenu{ID_RENDER_TONEMAP, &RenderOptions::tonemap, L"ACES Tone Mapping"}};
// 프레임마다 로드 완료분을 장면에 넣는 데 쓰는 최대 시간. 넘치면 다음 프레임으로 미룬다.
constexpr std::chrono::microseconds assetApplyBudget{500};
// 새 모델은 카메라 앞 이 거리에, 가장 긴 변이 이 크기가 되도록 놓는다(월드 단위).
constexpr float placementDistance = 5.f;
constexpr float placementSize = 2.f;

// 애니메이션 메뉴는 리소스(.rc)에 없고 시작 시 메뉴 막대 끝에 만든다. ID는 resource.h의 명령 범위 뒤를 쓴다.
constexpr int ID_ANIM_PLAYPAUSE = 32791;
constexpr int ID_ANIM_RESTART = 32792;
constexpr int ID_ANIM_LOOP = 32793;
constexpr int ID_ANIM_PREVCLIP = 32794;
constexpr int ID_ANIM_NEXTCLIP = 32795;
// 클립 선택 항목: ID_ANIM_CLIP_FIRST + 클립 번호. 메뉴에는 앞쪽 maxClipMenuItems개만 보인다([ ]로는 전부 고를 수 있다).
constexpr int ID_ANIM_CLIP_FIRST = 32800;
constexpr int maxClipMenuItems = 64;
struct AnimationMenuItem
{
    int commandId;
    const wchar_t *label;
};
constexpr std::array animationMenuItems{
    AnimationMenuItem{ID_ANIM_PLAYPAUSE, L"\uC7AC\uC0DD/\uC77C\uC2DC\uC815\uC9C0\tP"},
    AnimationMenuItem{ID_ANIM_RESTART, L"\uCC98\uC74C\uBD80\uD130\tHome"},
    AnimationMenuItem{ID_ANIM_LOOP, L"\uBC18\uBCF5\tL"},
    AnimationMenuItem{ID_ANIM_PREVCLIP, L"\uC774\uC804 \uD074\uB9BD\t["},
    AnimationMenuItem{ID_ANIM_NEXTCLIP, L"\uB2E4\uC74C \uD074\uB9BD\t]"}};

struct EditMenuItem
{
    int commandId;
    const wchar_t *label; // nullptr면 구분선
};
constexpr std::array editMenuItems{
    EditMenuItem{ID_EDIT_UNDO, L"\uC2E4\uD589 \uCDE8\uC18C(&U)\tCtrl+Z"},
    EditMenuItem{ID_EDIT_REDO, L"\uB2E4\uC2DC \uC2E4\uD589(&R)\tCtrl+Y"},
    EditMenuItem{0, nullptr},
    EditMenuItem{ID_EDIT_FOCUS, L"\uC120\uD0DD \uBB3C\uCCB4\uC5D0 \uCD08\uC810(&F)\tF"},
    EditMenuItem{ID_EDIT_DESELECT, L"\uC120\uD0DD \uD574\uC81C(&C)\tEsc"},
    EditMenuItem{0, nullptr},
    EditMenuItem{ID_EDIT_MODE_TRANSLATE, L"\uC774\uB3D9 \uBAA8\uB4DC(&T)\t1"},
    EditMenuItem{ID_EDIT_MODE_ROTATE, L"\uD68C\uC804 \uBAA8\uB4DC(&O)\t2"},
    EditMenuItem{ID_EDIT_MODE_SCALE, L"\uD06C\uAE30 \uBAA8\uB4DC(&S)\t3"}};
} // namespace

Win32App::Win32App(HINSTANCE hInstance, int nCmdShow)
    : m_loader(sr::io::BackgroundLoader::Options{
          .onCompleted = {},
          .onError = [this](const std::string &message) { m_pendingAssetErrors.push_back({message}); }}),
      m_projectSession(
          m_runner, m_loader, [this](AssetLoadError error) { m_pendingAssetErrors.push_back(std::move(error)); },
          [this]
          {
              if (m_editorState.HasSelection() &&
                  !isObjectInScene(m_runner.GetScene(), m_editorState.GetSelectedObject()))
              {
                  m_editorState.ClearSelection();
                  m_editorState.ClearHistory();
              }
              if (m_activeAnimator.expired())
              {
                  for (const auto &animator : m_runner.GetScene().GetAnimators())
                      if (animator && animator->GetClipCount() > 0)
                      {
                          m_activeAnimator = animator;
                          break;
                      }
              }
              refreshAnimationMenu();
              refreshEditMenu();
              refreshWindowTitle();
          })
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

    // 창 클래스는 프로세스 단위로 등록된다. 같은 프로세스의 두 번째 Win32App(테스트)은 기존 등록을 쓴다.
    if (!RegisterClassExW(&wcex) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        throw std::runtime_error("Failed to register window class");
    }

    m_hWnd = CreateWindowW(m_szWindowClass.data(), m_szTitle.data(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, 0,
                           CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, this);

    if (!m_hWnd)
    {
        throw std::runtime_error("Failed to create window");
    }
    m_windowTitle = m_szTitle.data();

    // 정렬/Hi-Z와 숨은 와이어는 기본 비활성화다. 기본 와이어는 빠른 X-ray 표시다.
    const HMENU algorithmsMenu = GetSubMenu(GetMenu(m_hWnd), 1);
    if (!algorithmsMenu)
        throw std::runtime_error("Failed to initialize rendering options menu");
    for (const auto &menu : renderOptionMenus)
        if (!AppendMenuW(algorithmsMenu, MF_STRING | MF_UNCHECKED, menu.commandId, menu.label))
            throw std::runtime_error("Failed to initialize rendering options menu");

    AppendMenuW(algorithmsMenu, MF_SEPARATOR, 0, nullptr);
    const HMENU shadingSubMenu = CreatePopupMenu();
    AppendMenuW(shadingSubMenu, MF_STRING | MF_CHECKED, ID_SHADING_AUTO, L"Auto (Material default)");
    AppendMenuW(shadingSubMenu, MF_STRING | MF_UNCHECKED, ID_SHADING_PHONG, L"Force Phong");
    AppendMenuW(shadingSubMenu, MF_STRING | MF_UNCHECKED, ID_SHADING_PBR, L"Force PBR (Cook-Torrance)");
    AppendMenuW(algorithmsMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(shadingSubMenu), L"Shading Model");

    // 파일 메뉴 맨 위에 모델 열기를 넣는다. 탐색기에서 창으로 끌어다 놓아도 같은 경로로 로드된다.
    const HMENU fileMenu = GetSubMenu(GetMenu(m_hWnd), 0);
    if (!fileMenu || !InsertMenuW(fileMenu, 0, MF_BYPOSITION | MF_STRING, ID_FILE_OPENMODEL,
                                  L"\uBAA8\uB378 \uC5F4\uAE30(&O)...\tCtrl+O") ||
        !InsertMenuW(fileMenu, 1, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr))
        throw std::runtime_error("Failed to initialize file menu");
    initializeProjectMenu(fileMenu, 2);
    AppendMenuW(fileMenu, MF_SEPARATOR, 0, nullptr);
    const HMENU sceneSubMenu = CreatePopupMenu();
    AppendMenuW(sceneSubMenu, MF_STRING, ID_SCENE_DEFAULT, L"0. 기본 데모 씬 (IronMan + Teapot)");
    AppendMenuW(sceneSubMenu, MF_STRING, ID_SCENE_PBR_GRID, L"1. PBR 구체 그리드 (5x5 룩뎁)");
    AppendMenuW(sceneSubMenu, MF_STRING, ID_SCENE_HELMET, L"2. 사이버네틱 헬멧 (PBR 쇼케이스)");
    AppendMenuW(sceneSubMenu, MF_STRING, ID_SCENE_TERRAIN, L"3. 고밀도 폴리곤 지형 (50k+ 트라이앵글)");
    AppendMenuW(sceneSubMenu, MF_STRING, ID_SCENE_OVERDRAW, L"4. 인스턴스 소행성군 (Hi-Z 오버드로)");
    AppendMenuW(sceneSubMenu, MF_STRING, ID_SCENE_ARENA, L"5. 멀티 라이트 아레나 (섀도우 & 광원)");
    AppendMenuW(fileMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(sceneSubMenu), L"벤치마크 씬(&B)");
    DragAcceptFiles(m_hWnd, TRUE);
    initializeEditMenu();
    initializeAnimationMenu();

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
    m_runner.SetFrameStartHook([this](FrameRunner &) { m_loader.DrainCompleted(assetApplyBudget); });
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
    // 매 프레임 지우고 그리므로 삼각형 ID를 화면 크기로 보관하지 않는다(픽셀당 8바이트 쓰기 절약, 화면 동일).
    if (!m_renderTarget.Attach(m_presenter.Surface(), false))
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

void Win32App::loadModelAsync(std::filesystem::path path)
{
    std::wstring extension = path.extension().wstring();
    std::ranges::transform(extension, extension.begin(),
                           [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    if (extension == L".gltf" || extension == L".glb")
    {
        // 프로젝트가 열려 있으면 Assets로 복사하고 에셋 표에 등록해 씬 저장/로드와 연동한다.
        if (m_projectSession.HasProject())
        {
            m_projectSession.ImportModel(
                std::move(path),
                [this](sr::gltf::LoadedScene &scene, const std::shared_ptr<sr::anim::Animator> &animator)
                    -> std::shared_ptr<GameObject>
                {
                    if (!animator)
                        return placeLoadedObject(std::move(scene.root), scene.bounds);
                    const AABB posedBounds = animator->GetBounds();
                    auto object = placeLoadedObject(std::move(scene.root),
                                                    posedBounds.IsValid() ? posedBounds : scene.bounds);
                    if (object && animator->GetClipCount() > 0)
                    {
                        m_activeAnimator = animator;
                        refreshAnimationMenu();
                    }
                    return object;
                });
            return;
        }
        // 파싱과 스킨 준비(Animator::Create)는 작업 스레드에서, 장면·재생기 추가는 프레임 시작 훅에서 한다.
        struct LoadedGltf
        {
            sr::gltf::LoadedScene scene;
            std::shared_ptr<sr::anim::Animator> animator;
        };
        using SceneResult = std::expected<LoadedGltf, AssetLoadError>;
        m_loader.Submit(
            [path](std::stop_token) -> SceneResult
            {
                auto scene = sr::gltf::LoadScene(path);
                if (!scene)
                    return std::unexpected(std::move(scene.error()));
                auto animator = sr::anim::Animator::Create(*scene);
                if (!animator)
                    return std::unexpected(AssetLoadError{"Animation setup failed: " + animator.error(), path});
                return LoadedGltf{std::move(*scene), std::move(*animator)};
            },
            [this, path](SceneResult result)
            {
                if (!result)
                {
                    m_pendingAssetErrors.push_back(std::move(result.error()));
                    return;
                }
                auto &[scene, animator] = *result;
                std::vector<std::string> warnings = scene.warnings;
                warnings.insert(warnings.end(), animator->GetWarnings().begin(), animator->GetWarnings().end());
                if (!warnings.empty())
                {
                    std::string message = "Loaded with warnings:";
                    for (const auto &warning : warnings)
                        message += "\n- " + warning;
                    m_pendingAssetErrors.push_back({std::move(message), path});
                }
                // 스킨 메시는 정적 AABB가 바인드 포즈 기준이라 실제 첫 포즈의 경계로 배치한다.
                const AABB posedBounds = animator->GetBounds();
                if (!placeLoadedObject(std::move(scene.root), posedBounds.IsValid() ? posedBounds : scene.bounds))
                    return;
                m_runner.GetScene().AddAnimator(animator);
                if (animator->GetClipCount() > 0)
                {
                    m_activeAnimator = animator;
                    refreshAnimationMenu();
                }
            });
        return;
    }
    if (extension != L".obj")
    {
        showAssetError({"Unsupported file type (.obj, .gltf, .glb)", path});
        return;
    }
    // 프로젝트가 열려 있으면 Assets로 복사해 씬 파일에 저장될 수 있게 한다.
    if (m_projectSession.HasProject())
    {
        m_projectSession.ImportModel(std::move(path), [this](std::unique_ptr<Model> model)
                                     { return placeLoadedModel(std::move(model)); });
        return;
    }
    using LoadResult = std::expected<std::unique_ptr<Model>, AssetLoadError>;
    m_loader.Submit([path](std::stop_token) { return ModelLoader::LoadOBJ(path); },
                    [this](LoadResult result)
                    {
                        if (!result)
                        {
                            m_pendingAssetErrors.push_back(std::move(result.error()));
                            return;
                        }
                        placeLoadedModel(std::move(*result));
                    });
}

std::shared_ptr<GameObject> Win32App::placeLoadedModel(std::unique_ptr<Model> model)
{
    const AABB bounds = model->GetLocalAABB();
    return placeLoadedObject(std::make_shared<GameObject>(SRMath::vec3(0.f, 0.f, 0.f), SRMath::vec3(0.f, 0.f, 0.f),
                                                          SRMath::vec3(1.f, 1.f, 1.f), std::move(model)),
                             bounds);
}

std::shared_ptr<GameObject> Win32App::placeLoadedObject(std::shared_ptr<GameObject> object, const AABB &bounds)
{
    // 모델 원점이 아니라 경계 상자 중심이 카메라 앞에 오도록 위치를 보정한다.
    const SRMath::vec3 extent = bounds.IsValid() ? bounds.max - bounds.min : SRMath::vec3(0.f, 0.f, 0.f);
    const float longest = std::max({extent.x, extent.y, extent.z});
    const float scale = longest > 0.f ? placementSize / longest : 1.f;
    const SRMath::vec3 center = bounds.IsValid() ? bounds.Center() : SRMath::vec3(0.f, 0.f, 0.f);
    const Camera &camera = m_runner.GetScene().GetCamera();
    object->SetPosition(camera.GetCameraPos() + camera.GetCameraForward() * placementDistance - center * scale);
    object->SetScale(SRMath::vec3(scale, scale, scale));
    if (!m_runner.GetScene().AddObject(object))
    {
        m_pendingAssetErrors.push_back({"Failed to add the model to the scene"});
        return nullptr;
    }
    return object;
}

void Win32App::initializeEditMenu()
{
    m_editMenu = CreatePopupMenu();
    if (!m_editMenu)
        throw std::runtime_error("Failed to initialize edit menu");
    for (const auto &item : editMenuItems)
    {
        const bool appended = item.label
                                  ? AppendMenuW(m_editMenu, MF_STRING, static_cast<UINT_PTR>(item.commandId), item.label)
                                  : AppendMenuW(m_editMenu, MF_SEPARATOR, 0, nullptr);
        if (!appended)
            throw std::runtime_error("Failed to initialize edit menu");
    }
    if (!AppendMenuW(GetMenu(m_hWnd), MF_POPUP, reinterpret_cast<UINT_PTR>(m_editMenu),
                     L"\uD3B8\uC9D1(&E)"))
        throw std::runtime_error("Failed to attach edit menu");
    refreshEditMenu();
}

void Win32App::refreshEditMenu()
{
    if (!m_editMenu)
        return;
    EnableMenuItem(m_editMenu, ID_EDIT_UNDO,
                   MF_BYCOMMAND | (m_editorState.CanUndo() ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(m_editMenu, ID_EDIT_REDO,
                   MF_BYCOMMAND | (m_editorState.CanRedo() ? MF_ENABLED : MF_GRAYED));
    EnableMenuItem(m_editMenu, ID_EDIT_DESELECT,
                   MF_BYCOMMAND | (m_editorState.HasSelection() ? MF_ENABLED : MF_GRAYED));

    UINT activeModeId = ID_EDIT_MODE_TRANSLATE;
    switch (m_editorState.GetTransformMode())
    {
    case sr::editor::TransformMode::Translate:
        activeModeId = ID_EDIT_MODE_TRANSLATE;
        break;
    case sr::editor::TransformMode::Rotate:
        activeModeId = ID_EDIT_MODE_ROTATE;
        break;
    case sr::editor::TransformMode::Scale:
        activeModeId = ID_EDIT_MODE_SCALE;
        break;
    }
    CheckMenuRadioItem(m_editMenu, ID_EDIT_MODE_TRANSLATE, ID_EDIT_MODE_SCALE, activeModeId, MF_BYCOMMAND);
    DrawMenuBar(m_hWnd);
}

bool Win32App::handleEditCommand(int commandId)
{
    switch (commandId)
    {
    case ID_EDIT_UNDO:
        if (m_editorState.Undo())
        {
            if (m_projectSession.HasProject())
                m_projectSession.MarkDirty();
            refreshEditMenu();
        }
        return true;
    case ID_EDIT_REDO:
        if (m_editorState.Redo())
        {
            if (m_projectSession.HasProject())
                m_projectSession.MarkDirty();
            refreshEditMenu();
        }
        return true;
     case ID_EDIT_FOCUS:
    {
        AABB targetBounds;
        if (const auto selected = m_editorState.GetSelectedObject())
        {
            targetBounds = sr::editor::ComputeObjectWorldBounds(*selected);
            if (!targetBounds.IsValid())
            {
                const SRMath::vec3 pos = m_editorState.GetGizmoOrigin();
                targetBounds.min = pos - SRMath::vec3{0.5f, 0.5f, 0.5f};
                targetBounds.max = pos + SRMath::vec3{0.5f, 0.5f, 0.5f};
            }
        }
        else
        {
            for (const auto &root : m_runner.GetScene().GetRootObjects())
                if (root)
                    targetBounds.Encapsulate(sr::editor::ComputeObjectWorldBounds(*root));
        }
        if (targetBounds.IsValid())
            m_runner.GetScene().GetCamera().FocusOnBounds(targetBounds);
        return true;
    }
    case ID_EDIT_DESELECT:
        m_editorState.ClearSelection();
        m_isDraggingGizmo = false;
        refreshEditMenu();
        return true;
    case ID_EDIT_MODE_TRANSLATE:
        m_editorState.SetTransformMode(sr::editor::TransformMode::Translate);
        refreshEditMenu();
        return true;
    case ID_EDIT_MODE_ROTATE:
        m_editorState.SetTransformMode(sr::editor::TransformMode::Rotate);
        refreshEditMenu();
        return true;
    case ID_EDIT_MODE_SCALE:
        m_editorState.SetTransformMode(sr::editor::TransformMode::Scale);
        refreshEditMenu();
        return true;
    default:
        return false;
    }
}

void Win32App::initializeAnimationMenu()
{
    m_animationMenu = CreatePopupMenu();
    if (!m_animationMenu)
        throw std::runtime_error("Failed to initialize animation menu");
    for (const auto &item : animationMenuItems)
        if (!AppendMenuW(m_animationMenu, MF_STRING, static_cast<UINT_PTR>(item.commandId), item.label))
            throw std::runtime_error("Failed to initialize animation menu");
    if (!AppendMenuW(m_animationMenu, MF_SEPARATOR, 0, nullptr) ||
        !AppendMenuW(GetMenu(m_hWnd), MF_POPUP, reinterpret_cast<UINT_PTR>(m_animationMenu),
                     L"\uC560\uB2C8\uBA54\uC774\uC158(&N)"))
        throw std::runtime_error("Failed to initialize animation menu");
    refreshAnimationMenu();
}

void Win32App::refreshAnimationMenu()
{
    if (!m_animationMenu)
        return;
    // 고정 항목 + 구분선 뒤의 클립 목록을 다시 만든다.
    const int fixedItems = static_cast<int>(animationMenuItems.size()) + 1;
    while (GetMenuItemCount(m_animationMenu) > fixedItems)
        DeleteMenu(m_animationMenu, fixedItems, MF_BYPOSITION);

    const auto animator = m_activeAnimator.lock();
    const UINT enabled = animator ? MF_ENABLED : MF_GRAYED;
    for (const auto &item : animationMenuItems)
        EnableMenuItem(m_animationMenu, static_cast<UINT>(item.commandId), MF_BYCOMMAND | enabled);
    checkMenuItem(ID_ANIM_PLAYPAUSE, animator && animator->IsPlaying());
    checkMenuItem(ID_ANIM_LOOP, animator && animator->IsLooping());
    if (!animator)
    {
        AppendMenuW(m_animationMenu, MF_STRING | MF_GRAYED, 0, L"(\uC560\uB2C8\uBA54\uC774\uC158\uC774 \uC788\uB294 glTF\uB97C \uC5EC\uC138\uC694)");
        DrawMenuBar(m_hWnd);
        return;
    }
    const std::size_t shown = std::min<std::size_t>(animator->GetClipCount(), maxClipMenuItems);
    for (std::size_t clip = 0; clip < shown; ++clip)
    {
        std::wstring label = utf8ToWide(std::string(animator->GetClipName(clip)));
        if (label.empty())
            label = L"Clip " + std::to_wstring(clip);
        AppendMenuW(m_animationMenu, MF_STRING, static_cast<UINT_PTR>(ID_ANIM_CLIP_FIRST + clip), label.c_str());
    }
    if (animator->GetClip() >= 0 && animator->GetClip() < maxClipMenuItems)
        CheckMenuRadioItem(m_animationMenu, ID_ANIM_CLIP_FIRST,
                           ID_ANIM_CLIP_FIRST + static_cast<UINT>(shown) - 1,
                           ID_ANIM_CLIP_FIRST + static_cast<UINT>(animator->GetClip()), MF_BYCOMMAND);
    DrawMenuBar(m_hWnd);
}

bool Win32App::handleAnimationCommand(int commandId)
{
    const bool clipItem = commandId >= ID_ANIM_CLIP_FIRST && commandId < ID_ANIM_CLIP_FIRST + maxClipMenuItems;
    if (!clipItem && std::ranges::none_of(animationMenuItems, [&](const auto &item) { return item.commandId == commandId; }))
        return false;
    const auto animator = m_activeAnimator.lock();
    if (!animator)
        return true;
    const std::size_t clipCount = animator->GetClipCount();
    const std::size_t current = animator->GetClip() >= 0 ? static_cast<std::size_t>(animator->GetClip()) : 0;
    switch (commandId)
    {
    case ID_ANIM_PLAYPAUSE:
        animator->SetPlaying(!animator->IsPlaying());
        break;
    case ID_ANIM_RESTART:
        animator->SetTime(0.f);
        animator->SetPlaying(true);
        break;
    case ID_ANIM_LOOP:
        animator->SetLooping(!animator->IsLooping());
        break;
    case ID_ANIM_PREVCLIP:
        if (clipCount > 0)
            animator->SetClip((current + clipCount - 1) % clipCount);
        break;
    case ID_ANIM_NEXTCLIP:
        if (clipCount > 0)
            animator->SetClip((current + 1) % clipCount);
        break;
    default:
        animator->SetClip(static_cast<std::size_t>(commandId - ID_ANIM_CLIP_FIRST));
        animator->SetPlaying(true);
        break;
    }
    refreshAnimationMenu();
    return true;
}

void Win32App::showOpenModelDialog()
{
    std::array<wchar_t, 4096> fileName{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = m_hWnd;
    dialog.lpstrFilter =
        L"3D models (*.obj;*.gltf;*.glb)\0*.obj;*.gltf;*.glb\0Wavefront OBJ (*.obj)\0*.obj\0"
        L"glTF 2.0 (*.gltf;*.glb)\0*.gltf;*.glb\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = fileName.data();
    dialog.nMaxFile = static_cast<DWORD>(fileName.size());
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    const bool chosen = GetOpenFileNameW(&dialog) != FALSE;
    // 모달 대화상자 동안의 WM_KEYUP은 이 창에 오지 않는다. 눌린 채 남은 키(Ctrl 등)를 지운다.
    m_keys.fill(false);
    if (chosen)
        loadModelAsync(fileName.data());
}

void Win32App::handleDroppedFiles(WPARAM dropHandle)
{
    const auto drop = reinterpret_cast<HDROP>(dropHandle);
    const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    for (UINT index = 0; index < count; ++index)
    {
        const UINT length = DragQueryFileW(drop, index, nullptr, 0);
        std::wstring path(static_cast<std::size_t>(length) + 1, L'\0');
        DragQueryFileW(drop, index, path.data(), length + 1);
        path.resize(length);
        if (!openProjectFile(path))
            loadModelAsync(path);
    }
    DragFinish(drop);
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
    showPendingAssetErrors();
}

void Win32App::showPendingAssetErrors()
{
    // 표시 중 다음 프레임이 새 오류를 넣을 수 있으므로 먼저 꺼낸다.
    const auto errors = std::exchange(m_pendingAssetErrors, {});
    for (const auto &error : errors)
        showAssetError(error);
}

void Win32App::updateFrame(float deltaTime)
{
    m_runner.Update(deltaTime, m_renderTarget.Width(), m_renderTarget.Height(), m_keys);
    m_gizmosSubmitted = false;
}

void Win32App::renderFrame()
{
    if (!m_gizmosSubmitted)
    {
        m_editorState.AppendEditorGizmos(m_runner.GetRenderQueue());
        m_gizmosSubmitted = true;
    }
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
    refreshWindowTitle();
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
    case WM_SYSKEYDOWN:
        handleKeyDown(wParam, (lParam & (1LL << 30)) != 0);
        break;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (wParam < m_keys.size())
            m_keys[wParam] = false;
        break;

    // Mouse Input
    case WM_LBUTTONDOWN:
        handleLeftMouseDown(lParam);
        break;
    case WM_LBUTTONUP:
        handleLeftMouseUp();
        break;
    case WM_MBUTTONDOWN:
        m_isMiddleMouseDown = true;
        m_lastMousePos = {static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam))};
        break;
    case WM_MBUTTONUP:
        m_isMiddleMouseDown = false;
        break;
    case WM_RBUTTONDOWN:
        m_isRightMouseDown = true;
        if (lParam != 0)
        {
            m_lastMousePos = {static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam))};
        }
        else
        {
            GetCursorPos(&m_lastMousePos);
            ScreenToClient(hWnd, &m_lastMousePos);
        }
        break;
    case WM_RBUTTONUP:
        m_isRightMouseDown = false;
        break;
    case WM_MOUSEMOVE:
        handleMouseMove(lParam);
        break;
    case WM_MOUSEWHEEL:
    {
        const short wheelDelta = GET_WHEEL_DELTA_WPARAM(wParam);
        m_runner.GetScene().GetCamera().Zoom(
            static_cast<float>(wheelDelta) / static_cast<float>(WHEEL_DELTA) * 0.5f);
        break;
    }

    case WM_DROPFILES:
        handleDroppedFiles(wParam);
        break;

    case WM_CLOSE:
        if (confirmDiscardChanges())
            DestroyWindow(hWnd);
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

    if (handleEditCommand(commandId))
        return true;

    if (handleAnimationCommand(commandId))
        return true;

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
    case ID_SHADING_AUTO:
    case ID_SHADING_PHONG:
    case ID_SHADING_PBR:
    {
        auto options = renderer.GetOptions();
        if (commandId == ID_SHADING_PHONG)
            options.shadingModel = EShadingModel::Phong;
        else if (commandId == ID_SHADING_PBR)
        {
            options.shadingModel = EShadingModel::PBR;
            options.pbr = true;
        }
        else
            options.shadingModel = EShadingModel::Auto;
        renderer.SetOptions(options);
        checkMenuItem(ID_SHADING_AUTO, options.shadingModel == EShadingModel::Auto);
        checkMenuItem(ID_SHADING_PHONG, options.shadingModel == EShadingModel::Phong);
        checkMenuItem(ID_SHADING_PBR, options.shadingModel == EShadingModel::PBR);
        break;
    }
    case ID_SCENE_DEFAULT:
        initializeScene();
        break;
    case ID_SCENE_PBR_GRID:
    {
        auto gridScene = sr::demo::CreatePbrSphereGridScene();
        const float aspectRatio =
            static_cast<float>(m_renderTarget.Width()) / std::max(1.0f, static_cast<float>(m_renderTarget.Height()));
        sr::demo::FitCameraToScene(gridScene, aspectRatio);
        m_runner.SetScene(std::move(gridScene));
        break;
    }
    case ID_SCENE_HELMET:
    {
        auto showcase = sr::demo::CreatePbrShowcaseScene();
        const float aspectRatio =
            static_cast<float>(m_renderTarget.Width()) / std::max(1.0f, static_cast<float>(m_renderTarget.Height()));
        sr::demo::FitCameraToScene(showcase, aspectRatio);
        m_runner.SetScene(std::move(showcase));
        break;
    }
    case ID_SCENE_TERRAIN:
    {
        auto terrain = sr::demo::CreatePolygonalTerrainScene();
        m_runner.SetScene(std::move(terrain));
        break;
    }
    case ID_SCENE_OVERDRAW:
    {
        auto overdraw = sr::demo::CreateInstancedFieldScene();
        m_runner.SetScene(std::move(overdraw));
        break;
    }
    case ID_SCENE_ARENA:
    {
        auto arena = sr::demo::CreateMultiLightArenaScene();
        m_runner.SetScene(std::move(arena));
        break;
    }
    case ID_FILE_OPENMODEL:
        showOpenModelDialog();
        break;
    case IDM_ABOUT:
        DialogBox(m_hInstance, MAKEINTRESOURCE(IDD_ABOUTBOX), m_hWnd, Win32App::About);
        break;
    case IDM_EXIT:
        if (confirmDiscardChanges())
            DestroyWindow(m_hWnd);
        break;
    default:
        return handleProjectCommand(commandId);
    }
    return true;
}

void Win32App::handleLeftMouseDown(LPARAM position)
{
    m_isLeftMouseDown = true;
    const POINT mousePos{static_cast<short>(LOWORD(position)), static_cast<short>(HIWORD(position))};
    m_lastMousePos = mousePos;

    const float width = static_cast<float>(m_renderTarget.Width());
    const float height = static_cast<float>(m_renderTarget.Height());
    if (width <= 0.f || height <= 0.f)
        return;

    const Ray ray = ScreenPointToRay(static_cast<float>(mousePos.x), static_cast<float>(mousePos.y), width,
                                     height, m_runner.GetScene().GetCamera().GetViewParams());
    if (m_editorState.HasSelection())
    {
        const GizmoAxis axis = m_editorState.PickGizmoAxis(ray);
        if (axis != GizmoAxis::None)
        {
            m_editorState.SetActiveAxis(axis);
            m_editorState.BeginTransformEdit();
            m_isDraggingGizmo = true;
            return;
        }
    }

    m_isDraggingGizmo = false;
    m_editorState.SetActiveAxis(GizmoAxis::None);
    if (const auto hit = PickSceneObject(m_runner.GetScene(), ray))
        m_editorState.Select(hit->object);
    else
        m_editorState.ClearSelection();
    refreshEditMenu();
}

void Win32App::handleLeftMouseUp()
{
    m_isLeftMouseDown = false;
    if (m_isDraggingGizmo)
    {
        m_isDraggingGizmo = false;
        const bool changed = m_editorState.CommitTransformEdit();
        m_editorState.SetActiveAxis(GizmoAxis::None);
        if (changed && m_projectSession.HasProject())
            m_projectSession.MarkDirty();
        refreshEditMenu();
    }
}

void Win32App::handleMouseMove(LPARAM position)
{
    // 마우스 좌표는 음수일 수 있으므로 16-bit signed 좌표로 해석한다.
    const POINT currentMousePos = {static_cast<short>(LOWORD(position)), static_cast<short>(HIWORD(position))};
    const auto deltaX = currentMousePos.x - m_lastMousePos.x;
    const auto deltaY = currentMousePos.y - m_lastMousePos.y;

    if (m_isDraggingGizmo && m_isLeftMouseDown && m_editorState.HasSelection())
    {
        const float amount = (static_cast<float>(deltaX) - static_cast<float>(deltaY)) * 0.01f;
        SRMath::vec3 axisDir{0.f, 0.f, 0.f};
        switch (m_editorState.GetActiveAxis())
        {
        case GizmoAxis::X:
            axisDir = {1.f, 0.f, 0.f};
            break;
        case GizmoAxis::Y:
            axisDir = {0.f, 1.f, 0.f};
            break;
        case GizmoAxis::Z:
            axisDir = {0.f, 0.f, 1.f};
            break;
        case GizmoAxis::None:
            break;
        }
        switch (m_editorState.GetTransformMode())
        {
        case TransformMode::Translate:
            m_editorState.ApplyTranslation(axisDir * amount);
            break;
        case TransformMode::Rotate:
            m_editorState.ApplyRotationEuler(axisDir * amount);
            break;
        case TransformMode::Scale:
        {
            const float factor = std::max(0.1f, 1.0f + amount);
            const SRMath::vec3 scaleFactor{axisDir.x > 0.f ? factor : 1.0f,
                                           axisDir.y > 0.f ? factor : 1.0f,
                                           axisDir.z > 0.f ? factor : 1.0f};
            m_editorState.ApplyScaleFactor(scaleFactor);
            break;
        }
        }
        if (const auto selected = m_editorState.GetSelectedObject())
            selected->UpdateTransform(0.f, false);
        m_lastMousePos = currentMousePos;
        return;
    }

    const bool altDown = m_keys[VK_MENU] || (GetKeyState(VK_MENU) & 0x8000) != 0;
    if (m_isMiddleMouseDown || (m_isRightMouseDown && altDown))
    {
        m_runner.GetScene().GetCamera().Orbit(-deltaX * 0.005f, -deltaY * 0.005f);
        m_lastMousePos = currentMousePos;
        return;
    }

    if (!m_isRightMouseDown)
        return;

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
    const bool controlDown = m_keys[VK_CONTROL] || (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shiftDown = m_keys[VK_SHIFT] || (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    if (key == VK_SPACE)
    {
        Scene &scene = m_runner.GetScene();
        scene.SetRotateMode(!scene.IsRotateMode());
    }
    else if (key == VK_F1)
        handleMenuCommand(ID_ANTIALIASING_NONE);
    else if (key == VK_F2)
        handleMenuCommand(ID_ANTIALIASING_FXAA);
    else if (controlDown)
    {
        if (key == 'O')
            handleMenuCommand(shiftDown ? ID_FILE_OPENPROJECT : ID_FILE_OPENMODEL);
        else if (key == 'S')
            handleMenuCommand(shiftDown ? ID_FILE_SAVESCENEAS : ID_FILE_SAVESCENE);
        else if (key == 'Z')
            handleMenuCommand(ID_EDIT_UNDO);
        else if (key == 'Y')
            handleMenuCommand(ID_EDIT_REDO);
    }
    else if (key == 'P')
        handleMenuCommand(ID_ANIM_PLAYPAUSE);
    else if (key == 'L')
        handleMenuCommand(ID_ANIM_LOOP);
    else if (key == VK_HOME)
        handleMenuCommand(ID_ANIM_RESTART);
    else if (key == VK_OEM_4) // [
        handleMenuCommand(ID_ANIM_PREVCLIP);
    else if (key == VK_OEM_6) // ]
        handleMenuCommand(ID_ANIM_NEXTCLIP);
    else if (key == 'F')
        handleMenuCommand(ID_EDIT_FOCUS);
    else if (key == VK_ESCAPE)
        handleMenuCommand(ID_EDIT_DESELECT);
    else if (key == '1')
        handleMenuCommand(ID_EDIT_MODE_TRANSLATE);
    else if (key == '2')
        handleMenuCommand(ID_EDIT_MODE_ROTATE);
    else if (key == '3')
        handleMenuCommand(ID_EDIT_MODE_SCALE);
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

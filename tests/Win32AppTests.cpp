// Win32App(창·메뉴·입력)과 숨긴 창 GDI 벤치마크 테스트. MSBuild 테스트 빌드에서만 컴파일된다.
#include "TestHarness.h"
#if SR_TEST_WIN32_APP
#include "App/Win32App.h"
#include "Animation/Animator.h"
#include "Graphics/GltfLoader.h"
#include "Core/DemoScene.h"
#include "Renderer/Renderer.h"
#include "resource.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string_view>
#include <vector>
#include "TestSupport.h"

using namespace sr::test;

struct Win32AppTestAccess
{
    // 애니메이션 메뉴·단축키: 재생기가 없으면 무시하고, 있으면 재생/정지·클립 전환과 메뉴 체크가 맞는다.
    static void animationMenu(const TestContext &context)
    {
        Win32App app(GetModuleHandleW(nullptr), SW_HIDE);
        const auto keyPress = [&](WPARAM key)
        {
            app.HandleMessage(app.m_hWnd, WM_KEYDOWN, key, 0);
            app.HandleMessage(app.m_hWnd, WM_KEYUP, key, 0);
        };
        keyPress('P'); // 재생기 없음: 아무 일 없음
        check((GetMenuState(app.m_animationMenu, 0, MF_BYPOSITION) & MF_GRAYED) != 0, "menu disabled without clips");

        auto scene = sr::gltf::LoadScene(context.root / "tests/fixtures/gltf/skinned-tube.glb");
        check(scene.has_value(), "fixture");
        auto animator = sr::anim::Animator::Create(*scene);
        check(animator.has_value(), "animator");
        check(app.placeLoadedObject(scene->root, (*animator)->GetBounds()) != nullptr, "place");
        app.m_runner.GetScene().AddAnimator(*animator);
        app.m_activeAnimator = *animator;
        app.refreshAnimationMenu();
        check((GetMenuState(app.m_animationMenu, 0, MF_BYPOSITION) & MF_CHECKED) != 0, "playing is checked");
        // 고정 5개 + 구분선 + 클립 2개
        check(GetMenuItemCount(app.m_animationMenu) == 8, "clip items listed");

        keyPress('P');
        check(!(*animator)->IsPlaying(), "P pauses");
        check((GetMenuState(app.m_animationMenu, 0, MF_BYPOSITION) & MF_CHECKED) == 0, "pause unchecks");
        keyPress(VK_OEM_6);
        check((*animator)->GetClip() == 1, "] selects the next clip");
        check((GetMenuState(app.m_animationMenu, 7, MF_BYPOSITION) & MF_CHECKED) != 0, "clip radio follows");
        keyPress(VK_OEM_4);
        check((*animator)->GetClip() == 0, "[ selects the previous clip");
        keyPress('L');
        check(!(*animator)->IsLooping(), "L toggles looping");
        DestroyWindow(app.m_hWnd);
    }

    static void benchmark(int frames, int width, int height, std::string_view mode,
                          std::string_view optimization, bool stats, bool hiddenWireframe);
    static void inputAndMenus()
    {
        Win32App app(GetModuleHandleW(nullptr), SW_HIDE);
        const bool initialRotation = app.m_runner.GetScene().IsRotateMode();
        app.HandleMessage(app.m_hWnd, WM_KEYDOWN, VK_SPACE, 0);
        check(app.m_runner.GetScene().IsRotateMode() != initialRotation, "space toggles rotation");
        app.HandleMessage(app.m_hWnd, WM_KEYDOWN, 'W', 0);
        check(app.m_runner.GetScene().IsRotateMode() != initialRotation, "held space does not toggle on another key");
        app.HandleMessage(app.m_hWnd, WM_KEYDOWN, VK_SPACE, static_cast<LPARAM>(1u << 30));
        check(app.m_runner.GetScene().IsRotateMode() != initialRotation, "autorepeat does not toggle");
        app.HandleMessage(app.m_hWnd, WM_KEYUP, VK_SPACE, 0);
        app.HandleMessage(app.m_hWnd, WM_KEYUP, 'W', 0);
        app.HandleMessage(app.m_hWnd, WM_SYSKEYDOWN, VK_MENU, 0);
        check(app.m_keys[VK_MENU], "Alt key recorded on WM_SYSKEYDOWN");
        app.HandleMessage(app.m_hWnd, WM_SYSKEYUP, VK_MENU, 0);
        check(!app.m_keys[VK_MENU], "Alt key cleared on WM_SYSKEYUP");
        app.HandleMessage(app.m_hWnd, WM_KEYDOWN, VK_F2, 0);
        check((GetMenuState(GetMenu(app.m_hWnd), ID_ANTIALIASING_FXAA, MF_BYCOMMAND) & MF_CHECKED) != 0,
              "FXAA menu state");
        app.HandleMessage(app.m_hWnd, WM_COMMAND, ID_LINEALGORITHM_DDA, 0);
        check((GetMenuState(GetMenu(app.m_hWnd), ID_LINEALGORITHM_DDA, MF_BYCOMMAND) & MF_CHECKED) != 0,
              "DDA menu state");
        app.HandleMessage(app.m_hWnd, WM_COMMAND, ID_RENDER_FRONTTOBACK, 0);
        check(app.m_runner.GetRenderer().GetOptions().frontToBack, "front-to-back menu toggle");
        app.HandleMessage(app.m_hWnd, WM_COMMAND, ID_RENDER_HIZ, 0);
        check(app.m_runner.GetRenderer().GetOptions().hiZ, "Hi-Z menu toggle");
        check(!app.m_runner.GetRenderer().GetOptions().hiddenWireframe, "wire defaults to X-ray");
        app.HandleMessage(app.m_hWnd, WM_COMMAND, ID_RENDER_HIDDEN_WIRE, 0);
        check(app.m_runner.GetRenderer().GetOptions().hiddenWireframe, "hidden-wire menu toggle on");
        check((GetMenuState(GetMenu(app.m_hWnd), ID_RENDER_HIDDEN_WIRE, MF_BYCOMMAND) & MF_CHECKED) != 0,
              "hidden-wire checked menu state");
        app.HandleMessage(app.m_hWnd, WM_COMMAND, ID_RENDER_HIDDEN_WIRE, 0);
        check(!app.m_runner.GetRenderer().GetOptions().hiddenWireframe, "hidden-wire menu toggle off");
        check((GetMenuState(GetMenu(app.m_hWnd), ID_RENDER_HIDDEN_WIRE, MF_BYCOMMAND) & MF_CHECKED) == 0,
              "hidden-wire unchecked menu state");
        app.HandleMessage(app.m_hWnd, WM_COMMAND, ID_RENDER_BILINEAR, 0);
        check(app.m_runner.GetRenderer().GetOptions().bilinearFiltering, "bilinear menu toggle");
        app.HandleMessage(app.m_hWnd, WM_COMMAND, ID_RENDER_MIPMAP, 0);
        check(app.m_runner.GetRenderer().GetOptions().mipmapping, "mipmap menu toggle");
        app.HandleMessage(app.m_hWnd, WM_COMMAND, ID_RENDER_SRGB, 0);
        check(app.m_runner.GetRenderer().GetOptions().srgbGamma, "sRGB menu toggle");
        app.updateFrame(0.f);
        app.renderFrame();
        app.HandleMessage(app.m_hWnd, WM_COMMAND, ID_DEBUG_NORMALVECTOR, 0);
        check(app.m_runner.GetDebugFlags().bShowNormal, "debug menu state");
        DestroyWindow(app.m_hWnd);
    }
    static void projectMenu()
    {
        Win32App app(GetModuleHandleW(nullptr), SW_HIDE);
        for (const int command : {ID_FILE_NEWPROJECT, ID_FILE_OPENPROJECT, ID_FILE_NEWSCENE, ID_FILE_OPENSCENE,
                                  ID_FILE_SAVESCENE, ID_FILE_SAVESCENEAS})
            check(GetMenuState(GetMenu(app.m_hWnd), command, MF_BYCOMMAND) != static_cast<UINT>(-1),
                  "project menu item exists");
        // 프로젝트 없이 저장하면 대화상자 대신 오류가 쌓인다(프레임 뒤에 표시).
        app.HandleMessage(app.m_hWnd, WM_COMMAND, ID_FILE_SAVESCENE, 0);
        check(app.m_pendingAssetErrors.size() == 1, "save without project reports error");
        app.m_pendingAssetErrors.clear();

        // 메뉴가 부르는 세션 흐름: 만들기 → 프레임 시작 훅에서 반영 → 제목에 프로젝트·씬 이름.
        const auto directory = std::filesystem::temp_directory_path() / L"sr-win32app-project-test";
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
        app.m_projectSession.CreateProject(directory / L"Demo" / L"Demo.srproj");
        for (int frame = 0; frame < 2000 && app.m_projectSession.IsBusy(); ++frame)
        {
            app.updateFrame(0.f);
            Sleep(1);
        }
        check(app.m_pendingAssetErrors.empty() && app.m_projectSession.HasProject(), "project created");
        check(app.m_windowTitle.find(L"Demo - Main") != std::wstring::npos, "title shows project and scene");
        check(app.m_runner.GetScene().GetRootObjects().empty(), "startup scene replaced demo scene");
        app.m_projectSession.MarkDirty();
        check(app.m_windowTitle.back() == L'*', "dirty mark in title");
        check(app.m_projectSession.SaveSceneNow().has_value() && app.m_windowTitle.back() != L'*', "saved");
        DestroyWindow(app.m_hWnd);
        std::filesystem::remove_all(directory, ignored);
    }

    static void editorMenuAndPicking(const TestContext &context)
    {
        Win32App app(GetModuleHandleW(nullptr), SW_HIDE);
        for (const int command : {ID_EDIT_UNDO, ID_EDIT_REDO, ID_EDIT_FOCUS, ID_EDIT_DESELECT,
                                  ID_EDIT_MODE_TRANSLATE, ID_EDIT_MODE_ROTATE, ID_EDIT_MODE_SCALE})
            check(GetMenuState(GetMenu(app.m_hWnd), command, MF_BYCOMMAND) != static_cast<UINT>(-1),
                  "edit menu item exists");

        // 장면을 비우고 카메라 정면(z=0)에 quad 하나를 배치한 뒤 마우스 클릭으로 피킹한다.
        Scene cleanScene;
        cleanScene.SetRotateMode(false);
        cleanScene.GetCamera().SetCameraPos({0.f, 0.f, -5.f});
        auto quad = loadObject(context.root / "tests/fixtures/quad.obj", {0.f, 0.f, 0.f});
        check(cleanScene.AddObject(quad), "add quad");
        app.m_runner.SetScene(std::move(cleanScene));
        app.updateFrame(0.f);

        const int centerX = app.m_renderTarget.Width() / 2;
        const int centerY = app.m_renderTarget.Height() / 2;
        const LPARAM centerParam = MAKELPARAM(centerX, centerY);
        app.HandleMessage(app.m_hWnd, WM_LBUTTONDOWN, MK_LBUTTON, centerParam);
        app.HandleMessage(app.m_hWnd, WM_LBUTTONUP, 0, centerParam);
        check(app.m_editorState.GetSelectedObject() == quad, "WM_LBUTTONDOWN picks center object");

        // 편집 모드 전환 단축키(1/2/3) 및 메뉴 체크 상태 확인
        app.HandleMessage(app.m_hWnd, WM_KEYDOWN, '2', 0);
        app.HandleMessage(app.m_hWnd, WM_KEYUP, '2', 0);
        check(app.m_editorState.GetMode() == sr::editor::TransformMode::Rotate, "2 switches to Rotate mode");
        check((GetMenuState(app.m_editMenu, ID_EDIT_MODE_ROTATE, MF_BYCOMMAND) & MF_CHECKED) != 0,
              "Rotate menu item checked");
        app.HandleMessage(app.m_hWnd, WM_COMMAND, ID_EDIT_MODE_SCALE, 0);
        check(app.m_editorState.GetMode() == sr::editor::TransformMode::Scale, "Scale menu command");
        app.HandleMessage(app.m_hWnd, WM_KEYDOWN, '1', 0);
        app.HandleMessage(app.m_hWnd, WM_KEYUP, '1', 0);
        check(app.m_editorState.GetMode() == sr::editor::TransformMode::Translate, "1 switches to Translate mode");

        // 변환 편집 후 Ctrl+Z / Ctrl+Y 및 메뉴 명령으로 Undo/Redo 확인
        app.m_editorState.BeginTransformEdit();
        app.m_editorState.ApplyTranslation({1.5f, 0.f, 0.f});
        check(app.m_editorState.CommitTransformEdit(), "commit translation");
        app.refreshEditMenu();
        check((GetMenuState(app.m_editMenu, ID_EDIT_UNDO, MF_BYCOMMAND) & MF_GRAYED) == 0, "Undo enabled");

        app.HandleMessage(app.m_hWnd, WM_KEYDOWN, VK_CONTROL, 0);
        app.HandleMessage(app.m_hWnd, WM_KEYDOWN, 'Z', 0);
        app.HandleMessage(app.m_hWnd, WM_KEYUP, 'Z', 0);
        app.HandleMessage(app.m_hWnd, WM_KEYUP, VK_CONTROL, 0);
        check(approximatelyEqual(quad->GetPosition().x, 0.f), "Ctrl+Z undoes translation");
        check((GetMenuState(app.m_editMenu, ID_EDIT_REDO, MF_BYCOMMAND) & MF_GRAYED) == 0, "Redo enabled");

        app.HandleMessage(app.m_hWnd, WM_COMMAND, ID_EDIT_REDO, 0);
        check(approximatelyEqual(quad->GetPosition().x, 1.5f), "ID_EDIT_REDO redoes translation");

        // F(초점 맞추기)와 Esc(선택 해제)
        app.updateFrame(0.f);
        app.HandleMessage(app.m_hWnd, WM_KEYDOWN, 'F', 0);
        app.HandleMessage(app.m_hWnd, WM_KEYUP, 'F', 0);
        check(approximatelyEqual(app.m_runner.GetScene().GetCamera().GetOrbitTarget().x, 1.5f, 1e-2f),
              "F focuses camera orbit target on selected object");

        app.HandleMessage(app.m_hWnd, WM_KEYDOWN, VK_ESCAPE, 0);
        app.HandleMessage(app.m_hWnd, WM_KEYUP, VK_ESCAPE, 0);
        check(!app.m_editorState.HasSelection(), "Esc clears selection");

        DestroyWindow(app.m_hWnd);
    }
};

// 고정 입력으로 실제 Win32App/GDI 경로를 측정한다. 숨겨진 창이므로 DWM 표시 지연은 대표하지 않는다.
void Win32AppTestAccess::benchmark(int frames, int width, int height, std::string_view mode,
                                    std::string_view optimization, bool stats, bool hiddenWireframe)
{
    check(frames > 0 && frames <= 100'000 && width > 0 && height > 0 && width <= 4096 && height <= 2160,
          "invalid benchmark dimensions/frame count");
    check(mode == "fill" || mode == "fxaa" || mode == "wire" || mode == "normal" || mode == "aabb",
          "unknown benchmark mode");
    check(optimization == "none" || optimization == "sort" || optimization == "hiz" || optimization == "both",
          "unknown benchmark optimization");
    Win32App app(GetModuleHandleW(nullptr), SW_HIDE);
    RECT rectangle{0, 0, width, height};
    check(AdjustWindowRectEx(&rectangle, WS_OVERLAPPEDWINDOW, TRUE, 0) != 0, "benchmark window bounds");
    check(SetWindowPos(app.m_hWnd, nullptr, 0, 0, rectangle.right - rectangle.left,
                       rectangle.bottom - rectangle.top, SWP_NOZORDER | SWP_NOACTIVATE) != 0,
          "benchmark window resize");
    check(app.m_renderTarget.Width() == width && app.m_renderTarget.Height() == height,
          "benchmark surface size");
    auto &flags = app.m_runner.GetDebugFlags();
    flags = {};
    flags.bShowWireframe = mode == "wire";
    flags.bShowNormal = mode == "normal";
    flags.bShowAABB = mode == "aabb";
    app.m_runner.GetRenderer().SetAAAlgorithm(mode == "fxaa" ? EAAAlgorithm::FXAA : EAAAlgorithm::None);
    app.m_runner.GetRenderer().SetOptions({optimization == "sort" || optimization == "both",
                                optimization == "hiz" || optimization == "both", stats, hiddenWireframe});
    // 두 모델의 초기 경계를 60도 수직 FOV에 맞춘다. 모든 실행에서 카메라는 동일하다(srcli와 같은 시점).
    sr::demo::FitCameraToScene(app.m_runner.GetScene(), static_cast<float>(width) / height);
    constexpr float deltaTime = 1.f / 60.f;
    for (int frame = 0; frame < 60; ++frame)
    {
        app.updateFrame(deltaTime);
        app.renderFrame();
    }
    using Clock = std::chrono::steady_clock;
    const auto milliseconds = [](auto duration)
    { return std::chrono::duration<double, std::milli>(duration).count(); };
    std::vector<double> frameTimes;
    frameTimes.reserve(frames);
    double updateTime = 0, renderTime = 0, presentTime = 0;
    for (int frame = 0; frame < frames; ++frame)
    {
        const auto begin = Clock::now();
        app.updateFrame(deltaTime);
        const auto updated = Clock::now();
        app.renderFrame();
        const auto rendered = Clock::now();
        const HDC dc = GetDC(app.m_hWnd);
        check(dc != nullptr, "benchmark screen DC");
        app.m_presenter.Present(dc);
        ReleaseDC(app.m_hWnd, dc);
        const auto end = Clock::now();
        updateTime += milliseconds(updated - begin);
        renderTime += milliseconds(rendered - updated);
        presentTime += milliseconds(end - rendered);
        frameTimes.push_back(milliseconds(end - begin));
    }
    std::uint64_t pixelHash = 14695981039346656037ull;
    for (const auto pixel : app.m_renderTarget.Surface().pixels)
        pixelHash = (pixelHash ^ pixel) * 1099511628211ull;
    std::ranges::sort(frameTimes);
    std::cout << "BENCH mode=" << mode << " options=" << optimization << " stats=" << stats
              << " hidden_wire=" << hiddenWireframe << " size=" << width << 'x' << height
              << " frames=" << frames << " update_ms=" << updateTime / frames
              << " render_ms=" << renderTime / frames << " present_ms=" << presentTime / frames
              << " mean_ms=" << (updateTime + renderTime + presentTime) / frames
              << " median_ms=" << frameTimes[frameTimes.size() / 2]
              << " p95_ms=" << frameTimes[(frameTimes.size() - 1) * 95 / 100]
              << " max_ms=" << frameTimes.back() << " pixel_hash=" << pixelHash << '\n';
    if (stats)
    {
        const auto &counters = app.m_runner.GetRenderer().GetStats();
        std::cout << "STATS prepared=" << counters.preparedTriangles << " refs=" << counters.binReferences
                  << " coverage=" << counters.coveragePassed << " depth_rejected=" << counters.depthRejected
                  << " shaded=" << counters.shadedPixels << " filled=" << counters.finalFilledPixels
                  << " hiz_tests=" << counters.hiZTests << " hiz_rejected=" << counters.hiZRejected
                  << " line_rejected=" << counters.lineDepthRejected
                  << " live_triangle_bytes=" << counters.liveTriangleBytes
                  << " retained_triangle_bytes=" << counters.retainedTriangleBytes
                  << " retained_bin_bytes=" << counters.retainedBinBytes
                  << " live_debug_line_bytes=" << counters.liveDebugLineBytes
                  << " retained_debug_line_bytes=" << counters.retainedDebugLineBytes
                  << " prepare_ms=" << counters.prepareMs << " bin_ms=" << counters.binMs
                  << " raster_ms=" << counters.rasterMs << " line_ms=" << counters.lineMs
                  << " post_ms=" << counters.postMs << '\n';
    }
    DestroyWindow(app.m_hWnd);
}

void sr::test::RegisterWin32AppTests(TestRegistry &registry)
{
    registry.Add("win32app.inputAndMenus", Win32AppTestAccess::inputAndMenus);
    registry.Add("win32app.animationMenu", Win32AppTestAccess::animationMenu);
    registry.Add("win32app.projectMenu", Win32AppTestAccess::projectMenu);
    registry.Add("win32app.editorMenuAndPicking", Win32AppTestAccess::editorMenuAndPicking);
}

void RunWin32AppBenchmark(int frames, int width, int height, std::string_view mode, std::string_view optimization,
                           bool stats, bool hiddenWireframe)
{
    Win32AppTestAccess::benchmark(frames, width, height, mode, optimization, stats, hiddenWireframe);
}
#endif

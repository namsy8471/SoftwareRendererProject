// Win32 Framework(창·메뉴·입력)와 숨긴 창 GDI 벤치마크 테스트. MSBuild 테스트 빌드에서만 컴파일된다.
#include "TestHarness.h"
#if SR_TEST_WIN32_APP
#include "Core/Framework.h"
#include "Renderer/Renderer.h"
#include "resource.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <string_view>
#include <vector>
#include "TestSupport.h"

using namespace sr::test;

struct FrameworkTestAccess
{
    static void benchmark(int frames, int width, int height, std::string_view mode,
                          std::string_view optimization, bool stats, bool hiddenWireframe);
    static void inputAndMenus()
    {
        Framework app(GetModuleHandleW(nullptr), SW_HIDE);
        const bool initialRotation = app.m_scene.IsRotateMode();
        app.HandleMessage(app.m_hWnd, WM_KEYDOWN, VK_SPACE, 0);
        check(app.m_scene.IsRotateMode() != initialRotation, "space toggles rotation");
        app.HandleMessage(app.m_hWnd, WM_KEYDOWN, 'W', 0);
        check(app.m_scene.IsRotateMode() != initialRotation, "held space does not toggle on another key");
        app.HandleMessage(app.m_hWnd, WM_KEYDOWN, VK_SPACE, static_cast<LPARAM>(1u << 30));
        check(app.m_scene.IsRotateMode() != initialRotation, "autorepeat does not toggle");
        app.HandleMessage(app.m_hWnd, WM_KEYUP, VK_SPACE, 0);
        app.HandleMessage(app.m_hWnd, WM_KEYUP, 'W', 0);
        app.HandleMessage(app.m_hWnd, WM_KEYDOWN, VK_F2, 0);
        check((GetMenuState(GetMenu(app.m_hWnd), ID_ANTIALIASING_FXAA, MF_BYCOMMAND) & MF_CHECKED) != 0,
              "FXAA menu state");
        app.HandleMessage(app.m_hWnd, WM_COMMAND, ID_LINEALGORITHM_DDA, 0);
        check((GetMenuState(GetMenu(app.m_hWnd), ID_LINEALGORITHM_DDA, MF_BYCOMMAND) & MF_CHECKED) != 0,
              "DDA menu state");
        app.HandleMessage(app.m_hWnd, WM_COMMAND, ID_RENDER_FRONTTOBACK, 0);
        check(app.m_renderer->GetOptions().frontToBack, "front-to-back menu toggle");
        app.HandleMessage(app.m_hWnd, WM_COMMAND, ID_RENDER_HIZ, 0);
        check(app.m_renderer->GetOptions().hiZ, "Hi-Z menu toggle");
        check(!app.m_renderer->GetOptions().hiddenWireframe, "wire defaults to X-ray");
        app.HandleMessage(app.m_hWnd, WM_COMMAND, ID_RENDER_HIDDEN_WIRE, 0);
        check(app.m_renderer->GetOptions().hiddenWireframe, "hidden-wire menu toggle on");
        check((GetMenuState(GetMenu(app.m_hWnd), ID_RENDER_HIDDEN_WIRE, MF_BYCOMMAND) & MF_CHECKED) != 0,
              "hidden-wire checked menu state");
        app.HandleMessage(app.m_hWnd, WM_COMMAND, ID_RENDER_HIDDEN_WIRE, 0);
        check(!app.m_renderer->GetOptions().hiddenWireframe, "hidden-wire menu toggle off");
        check((GetMenuState(GetMenu(app.m_hWnd), ID_RENDER_HIDDEN_WIRE, MF_BYCOMMAND) & MF_CHECKED) == 0,
              "hidden-wire unchecked menu state");
        app.Update(0.f);
        app.Render();
        app.HandleMessage(app.m_hWnd, WM_COMMAND, ID_DEBUG_NORMALVECTOR, 0);
        check(app.m_scene.GetDebugFlags().bShowNormal, "debug menu state");
        DestroyWindow(app.m_hWnd);
    }
};

// 고정 입력으로 실제 Framework/GDI 경로를 측정한다. 숨겨진 창이므로 DWM 표시 지연은 대표하지 않는다.
void FrameworkTestAccess::benchmark(int frames, int width, int height, std::string_view mode,
                                    std::string_view optimization, bool stats, bool hiddenWireframe)
{
    check(frames > 0 && frames <= 100'000 && width > 0 && height > 0 && width <= 4096 && height <= 2160,
          "invalid benchmark dimensions/frame count");
    check(mode == "fill" || mode == "fxaa" || mode == "wire" || mode == "normal" || mode == "aabb",
          "unknown benchmark mode");
    check(optimization == "none" || optimization == "sort" || optimization == "hiz" || optimization == "both",
          "unknown benchmark optimization");
    Framework app(GetModuleHandleW(nullptr), SW_HIDE);
    RECT rectangle{0, 0, width, height};
    check(AdjustWindowRectEx(&rectangle, WS_OVERLAPPEDWINDOW, TRUE, 0) != 0, "benchmark window bounds");
    check(SetWindowPos(app.m_hWnd, nullptr, 0, 0, rectangle.right - rectangle.left,
                       rectangle.bottom - rectangle.top, SWP_NOZORDER | SWP_NOACTIVATE) != 0,
          "benchmark window resize");
    check(app.m_renderTarget.Width() == width && app.m_renderTarget.Height() == height,
          "benchmark surface size");
    auto &flags = app.m_scene.GetDebugFlags();
    flags = {};
    flags.bShowWireframe = mode == "wire";
    flags.bShowNormal = mode == "normal";
    flags.bShowAABB = mode == "aabb";
    app.m_renderer->SetAAAlgorithm(mode == "fxaa" ? EAAAlgorithm::FXAA : EAAAlgorithm::None);
    app.m_renderer->SetOptions({optimization == "sort" || optimization == "both",
                                optimization == "hiz" || optimization == "both", stats, hiddenWireframe});
    // 두 모델의 초기 경계를 60도 수직 FOV에 맞춘다. 모든 실행에서 카메라는 동일하다(srcli와 같은 시점).
    app.m_scene.FitCameraToScene(static_cast<float>(width) / height);
    constexpr float deltaTime = 1.f / 60.f;
    for (int frame = 0; frame < 60; ++frame)
    {
        app.Update(deltaTime);
        app.Render();
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
        app.Update(deltaTime);
        const auto updated = Clock::now();
        app.Render();
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
        const auto &counters = app.m_renderer->GetStats();
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

void sr::test::RegisterFrameworkTests(TestRegistry &registry)
{
    registry.Add("framework.inputAndMenus", FrameworkTestAccess::inputAndMenus);
}

void RunFrameworkBenchmark(int frames, int width, int height, std::string_view mode, std::string_view optimization,
                           bool stats, bool hiddenWireframe)
{
    FrameworkTestAccess::benchmark(frames, width, height, mode, optimization, stats, hiddenWireframe);
}
#endif

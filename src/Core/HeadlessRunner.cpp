#include "Core/HeadlessRunner.h"
#include "Core/DemoScene.h"
#include "Core/FrameRunner.h"
#include "Graphics/Model.h"
#include "Graphics/ModelLoader.h"
#include "IO/BackgroundLoader.h"
#include "Renderer/Renderer.h"
#include "Renderer/RenderTarget.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <vector>
#include <tbb/global_control.h>
#include <utility>

namespace sr::headless
{
std::optional<ViewMode> ParseViewMode(std::string_view text) noexcept
{
    if (text == "fill")
        return ViewMode::Fill;
    if (text == "wire")
        return ViewMode::Wire;
    if (text == "normal")
        return ViewMode::Normal;
    if (text == "aabb")
        return ViewMode::AABB;
    if (text == "fxaa")
        return ViewMode::FXAA;
    return std::nullopt;
}

std::string_view ToString(ViewMode mode) noexcept
{
    switch (mode)
    {
    case ViewMode::Fill:
        return "fill";
    case ViewMode::Wire:
        return "wire";
    case ViewMode::Normal:
        return "normal";
    case ViewMode::AABB:
        return "aabb";
    case ViewMode::FXAA:
        return "fxaa";
    }
    return "unknown";
}

std::expected<RunResult, std::string> Run(const RunConfig &config)
{
    if (config.width <= 0 || config.height <= 0 || config.width > 8192 || config.height > 8192)
        return std::unexpected("invalid size");
    if (config.warmupFrames < 0 || config.measuredFrames <= 0)
        return std::unexpected("invalid frame count");
    if (config.backgroundLoadInterval < 0)
        return std::unexpected("invalid background load interval");

    std::optional<tbb::global_control> threadLimit;
    if (config.threads > 0)
        threadLimit.emplace(tbb::global_control::max_allowed_parallelism,
                            static_cast<std::size_t>(config.threads));

    auto loaded = sr::demo::LoadScene(config.assetDirectory);
    if (!loaded)
    {
        std::string message = loaded.error().message;
        if (!loaded.error().path.empty())
            message += " (" + loaded.error().path.string() + ")";
        return std::unexpected(message);
    }
    sr::demo::FitCameraToScene(*loaded, static_cast<float>(config.width) / static_cast<float>(config.height));

    FrameRunner runner;
    runner.SetScene(std::move(*loaded));
    auto &flags = runner.GetDebugFlags();
    flags.bShowWireframe = config.mode == ViewMode::Wire;
    flags.bShowNormal = config.mode == ViewMode::Normal;
    flags.bShowAABB = config.mode == ViewMode::AABB;

    Renderer &renderer = runner.GetRenderer();
    renderer.SetOptions(config.options);
    renderer.SetLineAlgorithm(config.lineAlgorithm);
    renderer.SetAAAlgorithm(config.mode == ViewMode::FXAA ? EAAAlgorithm::FXAA : EAAAlgorithm::None);

    std::vector<unsigned int> pixels(static_cast<std::size_t>(config.width) * config.height);
    RenderTarget target;
    if (!target.Attach({pixels, config.width, config.height, static_cast<std::size_t>(config.width)}))
        return std::unexpected("render target allocation failed");

    using Clock = std::chrono::steady_clock;
    const auto milliseconds = [](Clock::duration duration)
    { return std::chrono::duration<double, std::milli>(duration).count(); };

    // Win32App과 같은 구조: 작업 스레드가 파싱하고 프레임 시작 훅이 0.5ms 예산 안에서 결과를 받는다.
    // loader는 runner보다 늦게 만들어 먼저 파괴된다.
    int loadsSubmitted = 0, loadsApplied = 0;
    std::optional<sr::io::BackgroundLoader> loader;
    if (config.backgroundLoadInterval > 0)
    {
        loader.emplace();
        runner.SetFrameStartHook([&](FrameRunner &)
                                 { loader->DrainCompleted(std::chrono::microseconds{500}); });
    }
    const auto loadPath = config.assetDirectory / "teapot.obj";

    std::vector<double> frameTimes;
    frameTimes.reserve(static_cast<std::size_t>(config.measuredFrames));
    double updateTotal = 0, renderTotal = 0;
    for (int frame = -config.warmupFrames; frame < config.measuredFrames; ++frame)
    {
        if (loader && frame >= 0 && frame % config.backgroundLoadInterval == 0)
        {
            loader->Submit([&loadPath](std::stop_token) { return ModelLoader::LoadOBJ(loadPath); },
                           [&loadsApplied](std::expected<std::unique_ptr<Model>, AssetLoadError> model)
                           { loadsApplied += model.has_value() ? 1 : 0; });
            ++loadsSubmitted;
        }
        const auto begin = Clock::now();
        runner.Update(config.deltaTime, config.width, config.height);
        const auto updated = Clock::now();
        runner.Render(target);
        const auto end = Clock::now();
        if (frame < 0)
            continue;
        updateTotal += milliseconds(updated - begin);
        renderTotal += milliseconds(end - updated);
        frameTimes.push_back(milliseconds(end - begin));
    }

    RunResult result;
    const double count = static_cast<double>(frameTimes.size());
    double total = 0;
    for (const double time : frameTimes)
        total += time;
    std::ranges::sort(frameTimes);
    result.timings.meanMs = total / count;
    result.timings.medianMs = frameTimes[frameTimes.size() / 2];
    result.timings.p95Ms = frameTimes[(frameTimes.size() - 1) * 95 / 100];
    result.timings.minMs = frameTimes.front();
    result.timings.maxMs = frameTimes.back();
    result.timings.updateMeanMs = updateTotal / count;
    result.timings.renderMeanMs = renderTotal / count;
    result.stats = renderer.GetStats();
    result.lastFrame = sr::image::CopySurface(std::as_const(target).Surface());
    result.pixelHash = sr::image::HashPixels(result.lastFrame);
    result.backgroundLoadsSubmitted = loadsSubmitted;
    result.backgroundLoadsApplied = loadsApplied;
    result.threads = static_cast<int>(
        tbb::global_control::active_value(tbb::global_control::max_allowed_parallelism));
    return result;
}
} // namespace sr::headless

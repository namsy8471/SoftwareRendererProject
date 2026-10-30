#include "Core/HeadlessRunner.h"
#include "Animation/Animator.h"
#include "Animation/Skinning.h"
#include "Graphics/GltfLoader.h"
#include "Scene/GameObject.h"
#include "Core/DemoScene.h"
#include "Core/FrameRunner.h"
#include "Graphics/Model.h"
#include "Graphics/ModelLoader.h"
#include "IO/BackgroundLoader.h"
#include "Renderer/Renderer.h"
#include "Renderer/RenderTarget.h"
#include "Utils/Jobs.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <vector>
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

    std::optional<sr::jobs::ConcurrencyLimit> threadLimit;
    if (config.threads > 0)
        threadLimit.emplace(static_cast<std::size_t>(config.threads));

    const auto describe = [](const AssetLoadError &error)
    {
        std::string message = error.message;
        if (!error.path.empty())
            message += " (" + error.path.string() + ")";
        return message;
    };
    const float aspectRatio = static_cast<float>(config.width) / static_cast<float>(config.height);
    std::expected<Scene, AssetLoadError> loaded = std::unexpected(AssetLoadError{});
    std::shared_ptr<sr::anim::Animator> animator;
    if (config.scenePath.empty())
    {
        loaded = sr::demo::LoadScene(config.assetDirectory);
        if (!loaded)
            return std::unexpected(describe(loaded.error()));
        sr::demo::FitCameraToScene(*loaded, aspectRatio);
    }
    else if (config.scenePath == "grid" || config.scenePath == "pbrgrid")
    {
        loaded = sr::demo::CreatePbrSphereGridScene();
        sr::demo::FitCameraToScene(*loaded, aspectRatio);
    }
    else if (config.scenePath == "showcase" || config.scenePath == "pbrshowcase" || config.scenePath == "helmet")
    {
        loaded = sr::demo::CreatePbrShowcaseScene();
        sr::demo::FitCameraToScene(*loaded, aspectRatio);
    }
    else if (config.scenePath == "terrain" || config.scenePath == "geometry")
    {
        loaded = sr::demo::CreatePolygonalTerrainScene();
    }
    else if (config.scenePath == "overdraw" || config.scenePath == "instances")
    {
        loaded = sr::demo::CreateInstancedFieldScene();
    }
    else if (config.scenePath == "arena" || config.scenePath == "lighting" || config.scenePath == "shadows")
    {
        loaded = sr::demo::CreateMultiLightArenaScene();
    }
    else
    {
        auto gltfScene = sr::gltf::LoadScene(config.scenePath);
        if (!gltfScene)
            return std::unexpected(describe(gltfScene.error()));
        auto created = sr::anim::Animator::Create(*gltfScene);
        if (!created)
            return std::unexpected("animation setup failed: " + created.error());
        animator = std::move(*created);
        if (animator->GetClipCount() > 0)
        {
            if (config.clip < 0 || static_cast<std::size_t>(config.clip) >= animator->GetClipCount())
                return std::unexpected("clip " + std::to_string(config.clip) + " does not exist");
            animator->SetClip(static_cast<std::size_t>(config.clip));
        }
        // 모델 단위(cm, m)가 제각각이고 카메라 far는 100이므로, 첫 포즈 경계의 가장 긴 변이 normalizedSize가
        // 되도록 root 크기를 맞추고 중심을 원점에 둔다. 그다음 데모와 같은 카메라·광원 규칙을 쓴다.
        constexpr float normalizedSize = 4.f;
        if (const AABB bounds = animator->GetBounds(); bounds.IsValid())
        {
            const SRMath::vec3 extent = bounds.max - bounds.min;
            const float longest = std::max({extent.x, extent.y, extent.z});
            const float scale = longest > 0.f ? normalizedSize / longest : 1.f;
            gltfScene->root->SetScale(SRMath::vec3(scale, scale, scale));
            gltfScene->root->SetPosition(bounds.Center() * -scale);
            gltfScene->root->Update(0.f, false);
            animator->UpdateSkins();
        }
        // 자동 회전은 끄고 애니메이션만 움직인다.
        Scene scene;
        (void)scene.AddObject(gltfScene->root);
        scene.AddAnimator(animator);
        scene.SetCamera(Camera(SRMath::vec3(0.f, 0.f, -5.f)));
        scene.AddLight(DirectionalLight());
        scene.SetRotateMode(false);
        sr::demo::FitCameraToBounds(scene, animator->GetBounds(), aspectRatio);
        loaded = std::move(scene);
    }

    FrameRunner runner;
    runner.SetScene(std::move(*loaded));
    auto &flags = runner.GetDebugFlags();
    flags.bShowWireframe = config.mode == ViewMode::Wire;
    flags.bShowNormal = config.mode == ViewMode::Normal;
    flags.bShowAABB = config.mode == ViewMode::AABB;
    flags.bDisableLod = config.disableLod;

    Renderer &renderer = runner.GetRenderer();
    renderer.SetOptions(config.options);
    renderer.SetLineAlgorithm(config.lineAlgorithm);
    renderer.SetAAAlgorithm(config.mode == ViewMode::FXAA ? EAAAlgorithm::FXAA : EAAAlgorithm::None);

    std::vector<unsigned int> pixels(static_cast<std::size_t>(config.width) * config.height);
    RenderTarget target;
    // 매 프레임 지우고 그리므로 삼각형 ID를 화면 크기로 보관하지 않는다(픽셀당 8바이트 쓰기 절약, 화면 동일).
    if (!target.Attach({pixels, config.width, config.height, static_cast<std::size_t>(config.width)}, false))
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
    if (animator)
    {
        // 측정 프레임과 같은 dt로 애니메이션 단계(포즈 → 계층 transform → 스키닝)만 다시 잰다. 렌더는 하지 않으므로
        // 아래에서 복사하는 마지막 프레임 픽셀은 측정 루프가 그린 그대로다.
        const auto &roots = runner.GetScene().GetRootObjects();
        const auto begin = Clock::now();
        for (int frame = 0; frame < config.measuredFrames; ++frame)
        {
            animator->ApplyPose(config.deltaTime);
            for (const auto &root : roots)
                root->Update(config.deltaTime, false);
            animator->UpdateSkins();
        }
        result.timings.animationMeanMs = milliseconds(Clock::now() - begin) / config.measuredFrames;
    }
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
    if (animator)
    {
        for (const auto &object : animator->GetSkinnedObjects())
        {
            result.skinnedVertices += object->GetSkinnedMesh()->GetVertexCount();
            result.skinJoints += object->GetSkinnedMesh()->GetJointCount();
        }
        if (animator->GetClip() >= 0)
            result.clipName = animator->GetClipName(static_cast<std::size_t>(animator->GetClip()));
    }
    result.threads = static_cast<int>(sr::jobs::MaxConcurrency());
    return result;
}
} // namespace sr::headless

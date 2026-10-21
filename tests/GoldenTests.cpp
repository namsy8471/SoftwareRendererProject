// 골든 이미지 테스트: headless 경로로 데모 장면을 작게 렌더링해 tests/golden/*.png와 비교한다.
// 실패하면 <out>/golden/<name>.actual.png, <name>.diff.png(다른 픽셀=빨강)를 남긴다.
// 의도한 화면 변경이면 `RendererTests <root> --update-golden --filter golden`으로 갱신하고 PNG를 같이 커밋한다.
#include "Core/HeadlessRunner.h"
#include "TestHarness.h"
#include "TestSupport.h"

#include <array>
#include <iostream>
#include <string>

using namespace sr::test;

namespace
{
// MSVC/GCC 간 부동소수점 차이를 흡수할 만큼만 허용한다. 실제 회귀(물체 누락, 색 반전,
// 선 위치 이동)는 수백 픽셀 이상 바뀌므로 이 기준으로 잡힌다.
constexpr int channelTolerance = 4;
constexpr double maxDifferentRatio = 0.002;
constexpr int goldenWidth = 320, goldenHeight = 180;
// 회전이 반영된 시점을 고정한다. 마지막(warmup + 1번째) 프레임을 비교한다.
constexpr int goldenWarmupFrames = 30;

struct GoldenCase
{
    const char *name;
    sr::headless::ViewMode mode;
    bool hiddenWire;
    // 비어 있지 않으면 데모 대신 이 glTF 장면(저장소 루트 기준)과 첫 클립을 그린다.
    const char *scene = "";
};

constexpr std::array goldenCases{
    GoldenCase{"fill", sr::headless::ViewMode::Fill, false},
    GoldenCase{"wire", sr::headless::ViewMode::Wire, false},
    GoldenCase{"wire-hidden", sr::headless::ViewMode::Wire, true},
    GoldenCase{"normal", sr::headless::ViewMode::Normal, false},
    GoldenCase{"aabb", sr::headless::ViewMode::AABB, false},
    GoldenCase{"fxaa", sr::headless::ViewMode::FXAA, false},
    // 스킨 메시: 30프레임(0.5초) 뒤 Bend 관절이 45도 휜 원통. 법선 모드는 변형된 법선 선까지 본다.
    GoldenCase{"anim", sr::headless::ViewMode::Fill, false, "tests/fixtures/gltf/skinned-tube.glb"},
    GoldenCase{"anim-normal", sr::headless::ViewMode::Normal, false, "tests/fixtures/gltf/skinned-tube.glb"},
};

void runGolden(const GoldenCase &golden, const TestContext &context)
{
    sr::headless::RunConfig config;
    config.assetDirectory = context.root / "assets";
    config.width = goldenWidth;
    config.height = goldenHeight;
    config.warmupFrames = goldenWarmupFrames;
    config.measuredFrames = 1;
    config.mode = golden.mode;
    config.options.hiddenWireframe = golden.hiddenWire;
    if (*golden.scene != '\0')
        config.scenePath = context.root / golden.scene;
    const auto result = sr::headless::Run(config);
    check(result.has_value(), result ? "" : "headless run failed: " + result.error());

    const auto goldenPath = context.root / "tests/golden" / (std::string(golden.name) + ".png");
    if (context.updateGolden)
    {
        const auto written = sr::image::WritePng(goldenPath, result->lastFrame);
        check(written.has_value(), written ? "" : written.error());
        std::cout << "  updated " << goldenPath.generic_string() << '\n';
        return;
    }

    const auto expected = sr::image::ReadPng(goldenPath);
    check(expected.has_value(), "missing golden " + goldenPath.generic_string() +
                                    " (run with --update-golden to create it)");
    const auto diff = sr::image::Compare(*expected, result->lastFrame, channelTolerance);
    if (!diff.sizeMismatch && diff.differentRatio <= maxDifferentRatio)
        return;

    const auto outDirectory = context.outputDirectory / "golden";
    const auto actualPath = outDirectory / (std::string(golden.name) + ".actual.png");
    const auto diffPath = outDirectory / (std::string(golden.name) + ".diff.png");
    (void)sr::image::WritePng(actualPath, result->lastFrame);
    if (!diff.sizeMismatch)
        (void)sr::image::WritePng(diffPath, diff.visualization);
    check(false, std::string("golden mismatch: ") +
                     (diff.sizeMismatch ? "size differs"
                                        : std::to_string(diff.differentPixels) + " pixels differ (" +
                                              std::to_string(diff.differentRatio * 100.0) + "%, max channel delta " +
                                              std::to_string(diff.maxChannelDelta) + ")") +
                     "; actual: " + actualPath.generic_string() + ", diff: " + diffPath.generic_string());
}
} // namespace

void sr::test::RegisterGoldenTests(TestRegistry &registry)
{
    for (const auto &golden : goldenCases)
        registry.Add(std::string("golden.") + golden.name,
                     [&golden](const TestContext &context) { runGolden(golden, context); });
}

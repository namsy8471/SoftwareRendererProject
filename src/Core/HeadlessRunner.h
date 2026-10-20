#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include "Renderer/RenderTypes.h"
#include "Utils/ImageFile.h"

// 창 없이 DemoScene을 FrameRunner로 메모리 타깃에 렌더링한다. srcli(벤치마크·PNG 덤프)와
// 골든 이미지 테스트가 같은 경로를 쓰므로 측정 조건과 화면 기준이 일치한다.
namespace sr::headless
{
// Win32 앱의 디버그 메뉴 조합과 같다. fxaa는 채움 + FXAA다.
enum class ViewMode
{
    Fill,
    Wire,
    Normal,
    AABB,
    FXAA
};

[[nodiscard]] std::optional<ViewMode> ParseViewMode(std::string_view text) noexcept;
[[nodiscard]] std::string_view ToString(ViewMode mode) noexcept;

struct RunConfig
{
    std::filesystem::path assetDirectory;
    int width = 1920;
    int height = 1080;
    // warmup 프레임은 장면을 갱신하고 그리지만 시간에 넣지 않는다.
    int warmupFrames = 60;
    int measuredFrames = 600;
    // 고정 시간 간격이라 같은 설정이면 마지막 프레임 픽셀이 실행마다 같다.
    float deltaTime = 1.f / 60.f;
    ViewMode mode = ViewMode::Fill;
    RenderOptions options{};
    ELineAlgorithm lineAlgorithm = ELineAlgorithm::Bresenham;
    // 0이면 oneTBB 기본값(모든 코어).
    int threads = 0;
};

struct FrameTimings
{
    // 한 프레임 = 장면 갱신 + 타깃 Clear + RenderScene. 화면 표시(GDI)는 포함하지 않는다.
    double meanMs = 0, medianMs = 0, p95Ms = 0, minMs = 0, maxMs = 0;
    double updateMeanMs = 0, renderMeanMs = 0;
};

struct RunResult
{
    FrameTimings timings;
    RenderStats stats;          // options.collectStats일 때만 의미가 있다(마지막 프레임 값).
    sr::image::Image lastFrame; // 마지막 측정 프레임
    std::uint64_t pixelHash = 0;
    int threads = 0;            // 실제 사용한 최대 병렬 수
};

[[nodiscard]] std::expected<RunResult, std::string> Run(const RunConfig &config);
} // namespace sr::headless

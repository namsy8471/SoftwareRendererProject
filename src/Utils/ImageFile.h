#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <vector>
#include "Renderer/RenderTarget.h"

namespace sr::image
{
// 렌더 결과와 같은 0x00RRGGBB 정수 픽셀을 촘촘하게(stride == width) 담는다.
struct Image
{
    int width = 0;
    int height = 0;
    std::vector<std::uint32_t> pixels;
};

// stride를 제거한 복사본을 만든다. 렌더 타깃 수명과 무관하게 저장·비교할 때 쓴다.
[[nodiscard]] Image CopySurface(ConstPixelSurfaceView surface);

// 8-bit RGB PNG로 저장한다. 상위 폴더가 없으면 만든다.
[[nodiscard]] std::expected<void, std::string> WritePng(const std::filesystem::path &path, const Image &image);
[[nodiscard]] std::expected<Image, std::string> ReadPng(const std::filesystem::path &path);

// 렌더 결과 해시(FNV-1a). 벤치마크 출력과 같은 계산이라 실행 간 픽셀 동일성 확인에 쓴다.
[[nodiscard]] std::uint64_t HashPixels(const Image &image) noexcept;

struct ImageDiff
{
    bool sizeMismatch = false;
    std::size_t differentPixels = 0; // 어느 채널이든 tolerance보다 크게 다른 픽셀 수
    int maxChannelDelta = 0;
    double differentRatio = 0.0;
    Image visualization; // 다른 픽셀은 빨강, 같은 픽셀은 어둡게 한 기대 이미지
};

// 채널별 절댓값 차이가 channelTolerance 이하이면 같은 픽셀로 본다.
[[nodiscard]] ImageDiff Compare(const Image &expected, const Image &actual, int channelTolerance);
} // namespace sr::image

#include "Renderer/FXAA.h"

#include "Math/SRMath.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

// oneTBB는 구현 파일 안에서만 사용하며 FXAA의 공개 API는 비소유 surface만 받는다.
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

namespace
{
constexpr float edgeThreshold = 0.08f;
constexpr float minimumEdgeThreshold = 0.03125f;
constexpr int maximumSpan = 16;
constexpr float epsilon = 1e-6f;
constexpr float blendStrength = 0.2f;

[[nodiscard]] SRMath::Color unpackColor(sr::fxaa::Pixel pixel) noexcept
{
    // 기존 FXAA의 하위 바이트=red 해석을 의도적으로 보존한다. DIB BGRX의 red는
    // 상위 바이트이므로 휘도 가중치에는 기존 불일치가 있지만 구조 분리와 함께 바꾸지 않는다.
    constexpr float byteToUnit = 1.0f / 255.0f;
    return {static_cast<float>(pixel & 0xffu) * byteToUnit,
            static_cast<float>((pixel >> 8u) & 0xffu) * byteToUnit,
            static_cast<float>((pixel >> 16u) & 0xffu) * byteToUnit};
}

[[nodiscard]] sr::fxaa::Pixel packColor(const SRMath::Color &color) noexcept
{
    const auto toByte = [](float channel) noexcept
    { return static_cast<sr::fxaa::Pixel>(std::clamp(channel, 0.0f, 1.0f) * 255.0f); };

    const auto red = toByte(color.r);
    const auto green = toByte(color.g);
    const auto blue = toByte(color.b);
    return (blue << 16u) | (green << 8u) | red;
}

[[nodiscard]] constexpr SRMath::Color lerpColor(const SRMath::Color &from, const SRMath::Color &to,
                                                float amount) noexcept
{
    return {from.r + (to.r - from.r) * amount, from.g + (to.g - from.g) * amount,
            from.b + (to.b - from.b) * amount};
}

[[nodiscard]] constexpr float luma(const SRMath::Color &color) noexcept
{
    // 색상이 달라도 지각적 명암 경계가 비슷하도록 NTSC 휘도 가중치를 쓴다.
    return color.r * 0.299f + color.g * 0.587f + color.b * 0.114f;
}

template <class Surface> [[nodiscard]] bool validSurface(const Surface &surface) noexcept
{
    if (surface.width <= 0 || surface.height <= 0)
        return false;
    const auto width = static_cast<std::size_t>(surface.width);
    const auto height = static_cast<std::size_t>(surface.height);
    if (surface.stridePixels < width)
        return false;
    constexpr auto maximum = std::numeric_limits<std::size_t>::max();
    if (height - 1 > (maximum - width) / surface.stridePixels)
        return false;
    const auto requiredStorage = (height - 1) * surface.stridePixels + width;
    return requiredStorage <= maximum / sizeof(sr::fxaa::Pixel) && surface.pixels.size() >= requiredStorage;
}
} // namespace

void sr::fxaa::Apply(std::span<const Pixel> input, std::span<Pixel> output, int width, int height)
{
    const auto stride = width > 0 ? static_cast<std::size_t>(width) : 0;
    Apply(ConstPixelSurfaceView{input, width, height, stride}, PixelSurfaceView{output, width, height, stride});
}

void sr::fxaa::Apply(ConstPixelSurfaceView input, PixelSurfaceView output)
{
    if (input.width != output.width || input.height != output.height || !validSurface(input) ||
        !validSurface(output))
        return;

    const int width = input.width;
    const int height = input.height;
    if (width <= 0 || height <= 0)
        return;

    const auto rowSize = static_cast<std::size_t>(width);
    const auto rowCount = static_cast<std::size_t>(height);
    if (rowSize > std::numeric_limits<std::size_t>::max() / rowCount)
        return;

    const std::size_t pixelCount = rowSize * rowCount;
    // FXAA는 한 픽셀 이웃이 필요하다. 내부 영역이 없는 작은 이미지는 그대로 복사한다.
    if (width < 3 || height < 3)
    {
        for (int y = 0; y < height; ++y)
        {
            const auto row = static_cast<std::size_t>(y);
            std::ranges::copy(input.pixels.subspan(row * input.stridePixels, rowSize),
                              output.pixels.subspan(row * output.stridePixels, rowSize).begin());
        }
        return;
    }

    std::vector<float> lumaBuffer(pixelCount);

    // 1차 패스가 서로 다른 행의 휘도를 완성한 후 2차 패스가 읽는다.
    // 이 장벽으로 인접 행의 중복 쓰기와 읽기/쓰기 경합을 방지한다.
    tbb::parallel_for(tbb::blocked_range<int>{0, height},
                      [&](const tbb::blocked_range<int> &rows)
                      {
                          for (int y = rows.begin(); y != rows.end(); ++y)
                          {
                              const std::size_t row = static_cast<std::size_t>(y) * rowSize;
                              const std::size_t inputRow = static_cast<std::size_t>(y) * input.stridePixels;
                              for (int x = 0; x < width; ++x)
                              {
                                  const std::size_t index = row + static_cast<std::size_t>(x);
                                  lumaBuffer[index] =
                                      luma(unpackColor(input.pixels[inputRow + static_cast<std::size_t>(x)]));
                              }
                          }
                      });

    // 2차 패스 역시 서로 다른 출력 행만 쓰고, 완성된 휘도 버퍼를 읽는다.
    tbb::parallel_for(
        tbb::blocked_range<int>{1, height - 1},
        [&](const tbb::blocked_range<int> &rows)
        {
            for (int y = rows.begin(); y != rows.end(); ++y)
            {
                for (int x = 1; x < width - 1; ++x)
                {
                    const std::size_t index = static_cast<std::size_t>(y) * rowSize + x;
                    const std::size_t inputIndex = static_cast<std::size_t>(y) * input.stridePixels + x;
                    const std::size_t outputIndex = static_cast<std::size_t>(y) * output.stridePixels + x;
                    const float center = lumaBuffer[index];
                    const float north = lumaBuffer[index - rowSize];
                    const float south = lumaBuffer[index + rowSize];
                    const float west = lumaBuffer[index - 1];
                    const float east = lumaBuffer[index + 1];

                    const float minimum = std::min({center, north, south, west, east});
                    const float maximum = std::max({center, north, south, west, east});
                    const float contrast = maximum - minimum;
                    const float threshold = std::max(minimumEdgeThreshold, maximum * edgeThreshold);
                    if (contrast < threshold)
                    {
                        output.pixels[outputIndex] = input.pixels[inputIndex];
                        continue;
                    }

                    const float horizontalDifference = std::abs(west - east);
                    const float verticalDifference = std::abs(north - south);
                    const bool verticalEdge = verticalDifference >= horizontalDifference;
                    const float gradient = verticalEdge ? verticalDifference : horizontalDifference;
                    if (gradient <= epsilon)
                    {
                        output.pixels[outputIndex] = input.pixels[inputIndex];
                        continue;
                    }

                    float negativeDistance = 0.0f;
                    float positiveDistance = 0.0f;
                    for (int offset = 0; offset < maximumSpan; ++offset)
                    {
                        const int sampleX = verticalEdge ? x : x - offset - 1;
                        const int sampleY = verticalEdge ? y - offset - 1 : y;
                        if (sampleX < 0 || sampleY < 0)
                            break;

                        const std::size_t sampleIndex = static_cast<std::size_t>(sampleY) * rowSize + sampleX;
                        if (std::abs(lumaBuffer[sampleIndex] - center) / gradient >= 0.4f)
                            break;
                        negativeDistance = static_cast<float>(offset + 1);
                    }

                    for (int offset = 0; offset < maximumSpan; ++offset)
                    {
                        const int sampleX = verticalEdge ? x : x + offset + 1;
                        const int sampleY = verticalEdge ? y + offset + 1 : y;
                        if (sampleX >= width || sampleY >= height)
                            break;

                        const std::size_t sampleIndex = static_cast<std::size_t>(sampleY) * rowSize + sampleX;
                        if (std::abs(lumaBuffer[sampleIndex] - center) / gradient >= 0.4f)
                            break;
                        positiveDistance = static_cast<float>(offset + 1);
                    }

                    const float edgeLength = positiveDistance + negativeDistance;
                    if (edgeLength < 1.0f)
                    {
                        output.pixels[outputIndex] = input.pixels[inputIndex];
                        continue;
                    }

                    const float pixelOffset = (positiveDistance - negativeDistance) / edgeLength;
                    const float blendFactor = blendStrength * std::abs(pixelOffset);
                    const std::size_t neighborIndex =
                        verticalEdge
                            ? (pixelOffset > 0.0f ? inputIndex - input.stridePixels : inputIndex + input.stridePixels)
                            : (pixelOffset > 0.0f ? inputIndex - 1 : inputIndex + 1);

                    output.pixels[outputIndex] =
                        packColor(lerpColor(unpackColor(input.pixels[inputIndex]),
                                            unpackColor(input.pixels[neighborIndex]), blendFactor));
                }
            }
        });

    // 이웃을 읽을 수 없는 바깥 한 픽셀 테두리는 입력을 유지한다.
    for (int y = 0; y < height; ++y)
    {
        const std::size_t inputRow = static_cast<std::size_t>(y) * input.stridePixels;
        const std::size_t outputRow = static_cast<std::size_t>(y) * output.stridePixels;
        output.pixels[outputRow] = input.pixels[inputRow];
        output.pixels[outputRow + rowSize - 1] = input.pixels[inputRow + rowSize - 1];
    }
    std::ranges::copy(input.pixels.first(rowSize), output.pixels.begin());
    std::ranges::copy(input.pixels.subspan((rowCount - 1) * input.stridePixels, rowSize),
                      output.pixels.subspan((rowCount - 1) * output.stridePixels, rowSize).begin());
}

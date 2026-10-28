#include "Renderer/Baker/DilationFilter.h"
#include "Math/SRMath.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace sr::baker
{

namespace
{

constexpr int kNeighborDx[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
constexpr int kNeighborDy[8] = {-1, -1, -1, 0, 0, 1, 1, 1};

} // namespace

void DilateRGB(int width, int height, std::span<std::uint32_t> pixels,
               std::span<std::uint8_t> mask, int radius)
{
    if (width <= 0 || height <= 0 || radius <= 0)
        return;

    const std::size_t total = static_cast<std::size_t>(width) * height;
    if (pixels.size() < total || mask.size() < total)
        return;

    std::vector<std::uint32_t> nextPixels(pixels.begin(), pixels.end());
    std::vector<std::uint8_t> nextMask(mask.begin(), mask.end());

    for (int iter = 0; iter < radius; ++iter)
    {
        bool changed = false;
        for (int y = 0; y < height; ++y)
        {
            const int rowOffset = y * width;
            for (int x = 0; x < width; ++x)
            {
                const int idx = rowOffset + x;
                if (mask[idx] != 0)
                    continue;

                std::uint32_t sumR = 0, sumG = 0, sumB = 0;
                int count = 0;

                for (int n = 0; n < 8; ++n)
                {
                    const int nx = x + kNeighborDx[n];
                    const int ny = y + kNeighborDy[n];
                    if (nx >= 0 && nx < width && ny >= 0 && ny < height)
                    {
                        const int nIdx = ny * width + nx;
                        if (mask[nIdx] != 0)
                        {
                            const std::uint32_t px = pixels[nIdx];
                            sumR += (px >> 16) & 0xFF;
                            sumG += (px >> 8) & 0xFF;
                            sumB += px & 0xFF;
                            ++count;
                        }
                    }
                }

                if (count > 0)
                {
                    const std::uint32_t avgR = (sumR + (count / 2)) / count;
                    const std::uint32_t avgG = (sumG + (count / 2)) / count;
                    const std::uint32_t avgB = (sumB + (count / 2)) / count;
                    nextPixels[idx] = (avgR << 16) | (avgG << 8) | avgB;
                    nextMask[idx] = 1;
                    changed = true;
                }
            }
        }

        if (!changed)
            break;

        std::copy(nextPixels.begin(), nextPixels.end(), pixels.begin());
        std::copy(nextMask.begin(), nextMask.end(), mask.begin());
    }
}

void DilateNormalMap(int width, int height, std::span<std::uint32_t> pixels,
                     std::span<std::uint8_t> mask, int radius)
{
    if (width <= 0 || height <= 0 || radius <= 0)
        return;

    const std::size_t total = static_cast<std::size_t>(width) * height;
    if (pixels.size() < total || mask.size() < total)
        return;

    std::vector<std::uint32_t> nextPixels(pixels.begin(), pixels.end());
    std::vector<std::uint8_t> nextMask(mask.begin(), mask.end());

    for (int iter = 0; iter < radius; ++iter)
    {
        bool changed = false;
        for (int y = 0; y < height; ++y)
        {
            const int rowOffset = y * width;
            for (int x = 0; x < width; ++x)
            {
                const int idx = rowOffset + x;
                if (mask[idx] != 0)
                    continue;

                SRMath::vec3 normalSum{0.f, 0.f, 0.f};
                int count = 0;

                for (int n = 0; n < 8; ++n)
                {
                    const int nx = x + kNeighborDx[n];
                    const int ny = y + kNeighborDy[n];
                    if (nx >= 0 && nx < width && ny >= 0 && ny < height)
                    {
                        const int nIdx = ny * width + nx;
                        if (mask[nIdx] != 0)
                        {
                            const std::uint32_t px = pixels[nIdx];
                            const float vx = (((px >> 16) & 0xFF) / 255.0f) * 2.0f - 1.0f;
                            const float vy = (((px >> 8) & 0xFF) / 255.0f) * 2.0f - 1.0f;
                            const float vz = ((px & 0xFF) / 255.0f) * 2.0f - 1.0f;
                            normalSum += SRMath::vec3{vx, vy, vz};
                            ++count;
                        }
                    }
                }

                if (count > 0)
                {
                    SRMath::vec3 avgN = normalSum;
                    if (SRMath::lengthSq(avgN) > 1e-10f)
                        avgN = SRMath::normalize(avgN);
                    else
                        avgN = {0.f, 0.f, 1.f};

                    const auto r = static_cast<std::uint32_t>(
                        std::clamp(static_cast<int>(std::round((avgN.x * 0.5f + 0.5f) * 255.0f)), 0, 255));
                    const auto g = static_cast<std::uint32_t>(
                        std::clamp(static_cast<int>(std::round((avgN.y * 0.5f + 0.5f) * 255.0f)), 0, 255));
                    const auto b = static_cast<std::uint32_t>(
                        std::clamp(static_cast<int>(std::round((avgN.z * 0.5f + 0.5f) * 255.0f)), 0, 255));

                    nextPixels[idx] = (r << 16) | (g << 8) | b;
                    nextMask[idx] = 1;
                    changed = true;
                }
            }
        }

        if (!changed)
            break;

        std::copy(nextPixels.begin(), nextPixels.end(), pixels.begin());
        std::copy(nextMask.begin(), nextMask.end(), mask.begin());
    }
}

void DilateGrayscale(int width, int height, std::span<std::uint8_t> values,
                     std::span<std::uint8_t> mask, int radius)
{
    if (width <= 0 || height <= 0 || radius <= 0)
        return;

    const std::size_t total = static_cast<std::size_t>(width) * height;
    if (values.size() < total || mask.size() < total)
        return;

    std::vector<std::uint8_t> nextValues(values.begin(), values.end());
    std::vector<std::uint8_t> nextMask(mask.begin(), mask.end());

    for (int iter = 0; iter < radius; ++iter)
    {
        bool changed = false;
        for (int y = 0; y < height; ++y)
        {
            const int rowOffset = y * width;
            for (int x = 0; x < width; ++x)
            {
                const int idx = rowOffset + x;
                if (mask[idx] != 0)
                    continue;

                std::uint32_t sum = 0;
                int count = 0;

                for (int n = 0; n < 8; ++n)
                {
                    const int nx = x + kNeighborDx[n];
                    const int ny = y + kNeighborDy[n];
                    if (nx >= 0 && nx < width && ny >= 0 && ny < height)
                    {
                        const int nIdx = ny * width + nx;
                        if (mask[nIdx] != 0)
                        {
                            sum += values[nIdx];
                            ++count;
                        }
                    }
                }

                if (count > 0)
                {
                    nextValues[idx] = static_cast<std::uint8_t>((sum + (count / 2)) / count);
                    nextMask[idx] = 1;
                    changed = true;
                }
            }
        }

        if (!changed)
            break;

        std::copy(nextValues.begin(), nextValues.end(), values.begin());
        std::copy(nextMask.begin(), nextMask.end(), mask.begin());
    }
}

} // namespace sr::baker

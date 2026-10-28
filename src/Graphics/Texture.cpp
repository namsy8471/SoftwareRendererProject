#include "Texture.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <smmintrin.h>
#include <utility>

Texture::Texture() = default;
Texture::Texture(int width, int height, std::vector<unsigned char> rgba)
{
    if (width <= 0 || height <= 0 ||
        rgba.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4)
        return;
    m_pixels = std::move(rgba);
    m_width = width;
    m_height = height;
}
Texture::~Texture() = default;

namespace
{
// 가장자리 고정은 아래 clamp가 처리하므로 그대로 둔다.
float wrapCoordinate(float t, TextureWrap mode) noexcept
{
    switch (mode)
    {
    case TextureWrap::Repeat:
        return t - std::floor(t);
    case TextureWrap::MirroredRepeat:
    {
        const float period = t - 2.0f * std::floor(t * 0.5f); // [0, 2)
        return period > 1.0f ? 2.0f - period : period;
    }
    case TextureWrap::ClampToEdge:
        break;
    }
    return t;
}

int wrapTexelIndex(int coord, int size, TextureWrap mode) noexcept
{
    if (size <= 1)
        return 0;
    if (mode == TextureWrap::Repeat)
    {
        const int wrapped = coord % size;
        return wrapped < 0 ? wrapped + size : wrapped;
    }
    return std::clamp(coord, 0, size - 1);
}
} // namespace

SRMath::Color Texture::Sample(float u, float v) const noexcept
{
    if (m_pixels.empty())
        return {};

    const float wu = std::isfinite(u) ? std::clamp(wrapCoordinate(u, m_wrapU), 0.0f, 1.0f) : 0.0f;
    const float wv = std::isfinite(v) ? std::clamp(wrapCoordinate(v, m_wrapV), 0.0f, 1.0f) : 0.0f;
    const int x = std::clamp(static_cast<int>(std::floor(wu * static_cast<float>(m_width))), 0, m_width - 1);
    const int y = std::clamp(static_cast<int>(std::floor(wv * static_cast<float>(m_height))), 0, m_height - 1);
    const std::size_t offset = (static_cast<std::size_t>(y) * m_width + x) * 4;
    const auto *pixels = m_pixels.data();
    constexpr float byteToUnit = 1.0f / 255.0f;

    return {pixels[offset] * byteToUnit, pixels[offset + 1] * byteToUnit, pixels[offset + 2] * byteToUnit};
}

void Texture::GenerateMipmaps()
{
    m_mips.clear();
    if (m_pixels.empty() || m_width <= 0 || m_height <= 0)
        return;

    int prevWidth = m_width;
    int prevHeight = m_height;
    const std::vector<unsigned char> *prevPixels = &m_pixels;

    while (prevWidth > 1 || prevHeight > 1)
    {
        const int nextWidth = std::max(1, prevWidth / 2);
        const int nextHeight = std::max(1, prevHeight / 2);
        std::vector<unsigned char> nextPixels(
            static_cast<std::size_t>(nextWidth) * static_cast<std::size_t>(nextHeight) * 4);

        const unsigned char *src = prevPixels->data();
        unsigned char *dst = nextPixels.data();

        for (int y = 0; y < nextHeight; ++y)
        {
            const int y0 = std::min(y * 2, prevHeight - 1);
            const int y1 = std::min(y * 2 + 1, prevHeight - 1);
            const std::size_t row0 = static_cast<std::size_t>(y0) * static_cast<std::size_t>(prevWidth);
            const std::size_t row1 = static_cast<std::size_t>(y1) * static_cast<std::size_t>(prevWidth);
            const std::size_t dstRow = static_cast<std::size_t>(y) * static_cast<std::size_t>(nextWidth);

            for (int x = 0; x < nextWidth; ++x)
            {
                const int x0 = std::min(x * 2, prevWidth - 1);
                const int x1 = std::min(x * 2 + 1, prevWidth - 1);
                const std::size_t o00 = (row0 + static_cast<std::size_t>(x0)) * 4;
                const std::size_t o10 = (row0 + static_cast<std::size_t>(x1)) * 4;
                const std::size_t o01 = (row1 + static_cast<std::size_t>(x0)) * 4;
                const std::size_t o11 = (row1 + static_cast<std::size_t>(x1)) * 4;
                const std::size_t dstOffset = (dstRow + static_cast<std::size_t>(x)) * 4;

                for (std::size_t c = 0; c < 4; ++c)
                {
                    const int sum = static_cast<int>(src[o00 + c]) + static_cast<int>(src[o10 + c]) +
                                    static_cast<int>(src[o01 + c]) + static_cast<int>(src[o11 + c]);
                    dst[dstOffset + c] = static_cast<unsigned char>((sum + 2) >> 2);
                }
            }
        }

        m_mips.push_back(MipLevel{nextWidth, nextHeight, std::move(nextPixels)});
        prevWidth = nextWidth;
        prevHeight = nextHeight;
        prevPixels = &m_mips.back().pixels;
    }
}

int Texture::GetMipWidth(int mipLevel) const noexcept
{
    if (m_pixels.empty())
        return 0;
    if (mipLevel <= 0 || m_mips.empty())
        return m_width;
    const int clamped = std::min(mipLevel, static_cast<int>(m_mips.size()));
    return m_mips[static_cast<std::size_t>(clamped - 1)].width;
}

int Texture::GetMipHeight(int mipLevel) const noexcept
{
    if (m_pixels.empty())
        return 0;
    if (mipLevel <= 0 || m_mips.empty())
        return m_height;
    const int clamped = std::min(mipLevel, static_cast<int>(m_mips.size()));
    return m_mips[static_cast<std::size_t>(clamped - 1)].height;
}

SRMath::Color Texture::SampleNearest(float u, float v, int mipLevel) const noexcept
{
    if (m_pixels.empty())
        return {};
    if (mipLevel <= 0 || m_mips.empty())
        return Sample(u, v);

    const int clampedLevel = std::min(mipLevel, static_cast<int>(m_mips.size()));
    const MipLevel &mip = m_mips[static_cast<std::size_t>(clampedLevel - 1)];
    const float wu = std::isfinite(u) ? std::clamp(wrapCoordinate(u, m_wrapU), 0.0f, 1.0f) : 0.0f;
    const float wv = std::isfinite(v) ? std::clamp(wrapCoordinate(v, m_wrapV), 0.0f, 1.0f) : 0.0f;
    const int x = std::clamp(static_cast<int>(std::floor(wu * static_cast<float>(mip.width))), 0, mip.width - 1);
    const int y = std::clamp(static_cast<int>(std::floor(wv * static_cast<float>(mip.height))), 0, mip.height - 1);
    const std::size_t offset = (static_cast<std::size_t>(y) * mip.width + x) * 4;
    const auto *pixels = mip.pixels.data();
    constexpr float byteToUnit = 1.0f / 255.0f;

    return {pixels[offset] * byteToUnit, pixels[offset + 1] * byteToUnit, pixels[offset + 2] * byteToUnit};
}

SRMath::Color Texture::SampleBilinear(float u, float v, int mipLevel) const noexcept
{
    if (m_pixels.empty())
        return {};

    int width = m_width;
    int height = m_height;
    const unsigned char *pixels = m_pixels.data();
    if (mipLevel > 0 && !m_mips.empty())
    {
        const int clampedLevel = std::min(mipLevel, static_cast<int>(m_mips.size()));
        const MipLevel &mip = m_mips[static_cast<std::size_t>(clampedLevel - 1)];
        width = mip.width;
        height = mip.height;
        pixels = mip.pixels.data();
    }

    const float wu = std::isfinite(u) ? std::clamp(wrapCoordinate(u, m_wrapU), 0.0f, 1.0f) : 0.0f;
    const float wv = std::isfinite(v) ? std::clamp(wrapCoordinate(v, m_wrapV), 0.0f, 1.0f) : 0.0f;
    const float tx = wu * static_cast<float>(width) - 0.5f;
    const float ty = wv * static_cast<float>(height) - 0.5f;
    const float floorX = std::floor(tx);
    const float floorY = std::floor(ty);
    const float fx = tx - floorX;
    const float fy = ty - floorY;

    const int baseX = static_cast<int>(floorX);
    const int baseY = static_cast<int>(floorY);
    const int x0 = wrapTexelIndex(baseX, width, m_wrapU);
    const int x1 = wrapTexelIndex(baseX + 1, width, m_wrapU);
    const int y0 = wrapTexelIndex(baseY, height, m_wrapV);
    const int y1 = wrapTexelIndex(baseY + 1, height, m_wrapV);

    const auto loadTexel = [pixels, width](int x, int y) noexcept -> __m128
    {
        const std::size_t offset =
            (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4;
        std::int32_t rgba = 0;
        std::memcpy(&rgba, pixels + offset, sizeof(rgba));
        return _mm_cvtepi32_ps(_mm_cvtepu8_epi32(_mm_cvtsi32_si128(rgba)));
    };

    const __m128 c00 = loadTexel(x0, y0);
    const __m128 c10 = loadTexel(x1, y0);
    const __m128 c01 = loadTexel(x0, y1);
    const __m128 c11 = loadTexel(x1, y1);
    const __m128 vfx = _mm_set1_ps(fx);
    const __m128 vfy = _mm_set1_ps(fy);
    const __m128 top = _mm_add_ps(c00, _mm_mul_ps(_mm_sub_ps(c10, c00), vfx));
    const __m128 bottom = _mm_add_ps(c01, _mm_mul_ps(_mm_sub_ps(c11, c01), vfx));
    const __m128 blended = _mm_add_ps(top, _mm_mul_ps(_mm_sub_ps(bottom, top), vfy));
    const __m128 scaled = _mm_mul_ps(blended, _mm_set1_ps(1.0f / 255.0f));

    SRMath::Color result;
    result.m128 = _mm_insert_ps(scaled, scaled, 0x08);
    return result;
}

SRMath::Color Texture::Sample(float u, float v, float lod) const noexcept
{
    if (m_pixels.empty())
        return {};
    const int maxLevel = static_cast<int>(m_mips.size());
    const float clampedLod = std::isfinite(lod) ? std::clamp(lod, 0.0f, static_cast<float>(maxLevel)) : 0.0f;

    if (m_filter == TextureFilter::Nearest)
    {
        if (m_mips.empty())
            return Sample(u, v);
        return SampleNearest(u, v, std::clamp(static_cast<int>(std::round(clampedLod)), 0, maxLevel));
    }
    if (m_filter == TextureFilter::Bilinear || m_mips.empty())
    {
        return SampleBilinear(u, v, std::clamp(static_cast<int>(std::round(clampedLod)), 0, maxLevel));
    }
    const int level0 = static_cast<int>(std::floor(clampedLod));
    const int level1 = std::min(level0 + 1, maxLevel);
    const float frac = clampedLod - static_cast<float>(level0);
    const SRMath::Color c0 = SampleBilinear(u, v, level0);
    if (level0 == level1 || frac <= 0.0f)
        return c0;
    const SRMath::Color c1 = SampleBilinear(u, v, level1);
    return c0 + (c1 - c0) * frac;
}

SRMath::Color4 Texture::SampleRGBA(float u, float v) const noexcept
{
    if (m_pixels.empty())
        return {};

    const float wu = std::isfinite(u) ? std::clamp(wrapCoordinate(u, m_wrapU), 0.0f, 1.0f) : 0.0f;
    const float wv = std::isfinite(v) ? std::clamp(wrapCoordinate(v, m_wrapV), 0.0f, 1.0f) : 0.0f;
    const int x = std::clamp(static_cast<int>(std::floor(wu * static_cast<float>(m_width))), 0, m_width - 1);
    const int y = std::clamp(static_cast<int>(std::floor(wv * static_cast<float>(m_height))), 0, m_height - 1);
    const std::size_t offset = (static_cast<std::size_t>(y) * m_width + x) * 4;
    const auto *pixels = m_pixels.data();
    constexpr float byteToUnit = 1.0f / 255.0f;

    return {pixels[offset] * byteToUnit, pixels[offset + 1] * byteToUnit, pixels[offset + 2] * byteToUnit,
            pixels[offset + 3] * byteToUnit};
}

SRMath::Color4 Texture::SampleNearestRGBA(float u, float v, int mipLevel) const noexcept
{
    if (m_pixels.empty())
        return {};
    if (mipLevel <= 0 || m_mips.empty())
        return SampleRGBA(u, v);

    const int clampedLevel = std::min(mipLevel, static_cast<int>(m_mips.size()));
    const MipLevel &mip = m_mips[static_cast<std::size_t>(clampedLevel - 1)];
    const float wu = std::isfinite(u) ? std::clamp(wrapCoordinate(u, m_wrapU), 0.0f, 1.0f) : 0.0f;
    const float wv = std::isfinite(v) ? std::clamp(wrapCoordinate(v, m_wrapV), 0.0f, 1.0f) : 0.0f;
    const int x = std::clamp(static_cast<int>(std::floor(wu * static_cast<float>(mip.width))), 0, mip.width - 1);
    const int y = std::clamp(static_cast<int>(std::floor(wv * static_cast<float>(mip.height))), 0, mip.height - 1);
    const std::size_t offset = (static_cast<std::size_t>(y) * mip.width + x) * 4;
    const auto *pixels = mip.pixels.data();
    constexpr float byteToUnit = 1.0f / 255.0f;

    return {pixels[offset] * byteToUnit, pixels[offset + 1] * byteToUnit, pixels[offset + 2] * byteToUnit,
            pixels[offset + 3] * byteToUnit};
}

SRMath::Color4 Texture::SampleBilinearRGBA(float u, float v, int mipLevel) const noexcept
{
    if (m_pixels.empty())
        return {};

    int width = m_width;
    int height = m_height;
    const unsigned char *pixels = m_pixels.data();
    if (mipLevel > 0 && !m_mips.empty())
    {
        const int clampedLevel = std::min(mipLevel, static_cast<int>(m_mips.size()));
        const MipLevel &mip = m_mips[static_cast<std::size_t>(clampedLevel - 1)];
        width = mip.width;
        height = mip.height;
        pixels = mip.pixels.data();
    }

    const float wu = std::isfinite(u) ? std::clamp(wrapCoordinate(u, m_wrapU), 0.0f, 1.0f) : 0.0f;
    const float wv = std::isfinite(v) ? std::clamp(wrapCoordinate(v, m_wrapV), 0.0f, 1.0f) : 0.0f;
    const float tx = wu * static_cast<float>(width) - 0.5f;
    const float ty = wv * static_cast<float>(height) - 0.5f;
    const float floorX = std::floor(tx);
    const float floorY = std::floor(ty);
    const float fx = tx - floorX;
    const float fy = ty - floorY;

    const int baseX = static_cast<int>(floorX);
    const int baseY = static_cast<int>(floorY);
    const int x0 = wrapTexelIndex(baseX, width, m_wrapU);
    const int x1 = wrapTexelIndex(baseX + 1, width, m_wrapU);
    const int y0 = wrapTexelIndex(baseY, height, m_wrapV);
    const int y1 = wrapTexelIndex(baseY + 1, height, m_wrapV);

    const auto loadTexel = [pixels, width](int x, int y) noexcept -> __m128
    {
        const std::size_t offset =
            (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4;
        std::int32_t rgba = 0;
        std::memcpy(&rgba, pixels + offset, sizeof(rgba));
        return _mm_cvtepi32_ps(_mm_cvtepu8_epi32(_mm_cvtsi32_si128(rgba)));
    };

    const __m128 c00 = loadTexel(x0, y0);
    const __m128 c10 = loadTexel(x1, y0);
    const __m128 c01 = loadTexel(x0, y1);
    const __m128 c11 = loadTexel(x1, y1);
    const __m128 vfx = _mm_set1_ps(fx);
    const __m128 vfy = _mm_set1_ps(fy);
    const __m128 top = _mm_add_ps(c00, _mm_mul_ps(_mm_sub_ps(c10, c00), vfx));
    const __m128 bottom = _mm_add_ps(c01, _mm_mul_ps(_mm_sub_ps(c11, c01), vfx));
    const __m128 blended = _mm_add_ps(top, _mm_mul_ps(_mm_sub_ps(bottom, top), vfy));
    const __m128 scaled = _mm_mul_ps(blended, _mm_set1_ps(1.0f / 255.0f));

    return SRMath::Color4(scaled);
}

SRMath::Color4 Texture::SampleRGBA(float u, float v, float lod) const noexcept
{
    if (m_pixels.empty())
        return {};
    const int maxLevel = static_cast<int>(m_mips.size());
    const float clampedLod = std::isfinite(lod) ? std::clamp(lod, 0.0f, static_cast<float>(maxLevel)) : 0.0f;

    if (m_filter == TextureFilter::Nearest)
    {
        if (m_mips.empty())
            return SampleRGBA(u, v);
        return SampleNearestRGBA(u, v, std::clamp(static_cast<int>(std::round(clampedLod)), 0, maxLevel));
    }
    if (m_filter == TextureFilter::Bilinear || m_mips.empty())
    {
        return SampleBilinearRGBA(u, v, std::clamp(static_cast<int>(std::round(clampedLod)), 0, maxLevel));
    }
    const int level0 = static_cast<int>(std::floor(clampedLod));
    const int level1 = std::min(level0 + 1, maxLevel);
    const float frac = clampedLod - static_cast<float>(level0);
    const SRMath::Color4 c0 = SampleBilinearRGBA(u, v, level0);
    if (level0 == level1 || frac <= 0.0f)
        return c0;
    const SRMath::Color4 c1 = SampleBilinearRGBA(u, v, level1);
    return c0 + (c1 - c0) * frac;
}

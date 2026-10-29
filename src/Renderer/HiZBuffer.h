#pragma once
#include "Math/SRMath.h"
#include "Math/AABB.h"
#include "Renderer/Tile.h"
#include <vector>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <limits>
#include <xmmintrin.h>
#include <emmintrin.h>

// Vectorized computation of minimum depth among 256 pixels in a 16x16 tile.
// Uses portable SSE instructions supported on all x64 platforms (GCC, Clang, MSVC).
inline float ComputeTileMinDepth(const std::array<float, 256> &depth) noexcept
{
    __m128 minVal0 = _mm_loadu_ps(&depth[0]);
    __m128 minVal1 = _mm_loadu_ps(&depth[4]);
    __m128 minVal2 = _mm_loadu_ps(&depth[8]);
    __m128 minVal3 = _mm_loadu_ps(&depth[12]);

    for (int i = 16; i < 256; i += 16)
    {
        minVal0 = _mm_min_ps(minVal0, _mm_loadu_ps(&depth[i]));
        minVal1 = _mm_min_ps(minVal1, _mm_loadu_ps(&depth[i + 4]));
        minVal2 = _mm_min_ps(minVal2, _mm_loadu_ps(&depth[i + 8]));
        minVal3 = _mm_min_ps(minVal3, _mm_loadu_ps(&depth[i + 12]));
    }

    minVal0 = _mm_min_ps(minVal0, minVal1);
    minVal2 = _mm_min_ps(minVal2, minVal3);
    minVal0 = _mm_min_ps(minVal0, minVal2);

    __m128 m2 = _mm_min_ps(minVal0, _mm_movehl_ps(minVal0, minVal0));
    __m128 m1 = _mm_min_ss(m2, _mm_shuffle_ps(m2, m2, 1));
    return _mm_cvtss_f32(m1);
}

// Coarse 16x16 Software Hierarchical Z-Buffer (Coarse Hi-Z Pyramid Level 0)
// For 1080p (1920x1080), resolution is 120x68 tiles (8,160 floats = ~32 KB).
// Stores conservative maximum distance (minimum 1/w) for each tile.
// If any pixel in the tile is uncovered/empty, depth is set to 0.0f (never occluding).
class HiZBuffer
{
public:
    HiZBuffer() = default;

    void Resize(int width, int height)
    {
        m_width = width;
        m_height = height;
        m_tilesX = (width + tileSize - 1) / tileSize;
        m_tilesY = (height + tileSize - 1) / tileSize;
        const std::size_t count = static_cast<std::size_t>(m_tilesX) * m_tilesY;
        if (m_tileDepth.size() != count)
        {
            m_tileDepth.assign(count, 0.0f);
            m_valid = false;
        }
    }

    void Clear() noexcept
    {
        std::ranges::fill(m_tileDepth, 0.0f);
        m_valid = false;
    }

    void SetTileDepthByIndex(std::size_t tileIndex, float minQ) noexcept
    {
        if (tileIndex < m_tileDepth.size())
            m_tileDepth[tileIndex] = minQ;
    }

    [[nodiscard]] float GetTileDepthByIndex(std::size_t tileIndex) const noexcept
    {
        return m_tileDepth[tileIndex];
    }

    [[nodiscard]] int GetTilesX() const noexcept { return m_tilesX; }
    [[nodiscard]] int GetTilesY() const noexcept { return m_tilesY; }
    [[nodiscard]] int GetWidth() const noexcept { return m_width; }
    [[nodiscard]] int GetHeight() const noexcept { return m_height; }

    [[nodiscard]] bool IsValid() const noexcept { return m_valid; }
    void SetValid(bool valid) noexcept { m_valid = valid; }

    void SetViewProjection(const SRMath::mat4 &vp) noexcept { m_viewProjection = vp; }
    [[nodiscard]] const SRMath::mat4 &GetViewProjection() const noexcept { return m_viewProjection; }

    // Conservative submesh AABB occlusion test:
    // Returns true ONLY if:
    // 1. All 8 corners are strictly in front of the camera near plane (w > 0.001f).
    // 2. The screen-space AABB footprint is fully inside the screen boundary [0, width] x [0, height].
    // 3. Every tile overlapping the footprint has a valid, fully covered occluder depth (tileDepth > 0.0f).
    // 4. The closest point of the AABB (maximum 1/w) is strictly behind the furthest pixel of every overlapping tile.
    [[nodiscard]] bool IsAABBOccluded(const AABB &worldAABB, const SRMath::mat4 &viewProjection) const noexcept
    {
        if (!m_valid || m_tileDepth.empty() || !worldAABB.IsValid())
            return false;

        const auto corners = worldAABB.Corners();
        float maxOneOverW = 0.0f;
        float minSX = std::numeric_limits<float>::infinity();
        float maxSX = -std::numeric_limits<float>::infinity();
        float minSY = std::numeric_limits<float>::infinity();
        float maxSY = -std::numeric_limits<float>::infinity();

        const float halfW = static_cast<float>(m_width) * 0.5f;
        const float halfH = static_cast<float>(m_height) * 0.5f;

        for (const auto &corner : corners)
        {
            const SRMath::vec4 clip = viewProjection * SRMath::vec4(corner, 1.0f);
            // Conservative near plane check: if any corner is at or behind near plane, cannot cull
            if (clip.w <= 0.001f || !std::isfinite(clip.w))
                return false;

            const float invW = 1.0f / clip.w;
            if (invW > maxOneOverW)
                maxOneOverW = invW;

            const float ndcX = clip.x * invW;
            const float ndcY = clip.y * invW;

            const float sx = (ndcX + 1.0f) * halfW;
            const float sy = (1.0f - ndcY) * halfH;

            minSX = std::min(minSX, sx);
            maxSX = std::max(maxSX, sx);
            minSY = std::min(minSY, sy);
            maxSY = std::max(maxSY, sy);
        }

        if (!std::isfinite(maxOneOverW) || maxOneOverW <= 0.0f)
            return false;

        // Conservative boundary check: if the AABB extends beyond the screen edges,
        // it cannot be guaranteed to be occluded solely by on-screen tiles.
        if (minSX < 0.0f || maxSX >= static_cast<float>(m_width) ||
            minSY < 0.0f || maxSY >= static_cast<float>(m_height))
            return false;

        // Conservative inflation for floating point tolerance (same ratio as Rasterizer::IsOccluded)
        const float testDepth = maxOneOverW * (1.0f + 1e-4f);

        int tileMinX = static_cast<int>(std::floor(minSX)) >> 4;
        int tileMaxX = static_cast<int>(std::ceil(maxSX)) >> 4;
        int tileMinY = static_cast<int>(std::floor(minSY)) >> 4;
        int tileMaxY = static_cast<int>(std::ceil(maxSY)) >> 4;

        tileMinX = std::clamp(tileMinX, 0, m_tilesX - 1);
        tileMaxX = std::clamp(tileMaxX, 0, m_tilesX - 1);
        tileMinY = std::clamp(tileMinY, 0, m_tilesY - 1);
        tileMaxY = std::clamp(tileMaxY, 0, m_tilesY - 1);

        if (tileMinX > tileMaxX || tileMinY > tileMaxY)
            return false;

        // Check all tiles covered by the AABB
        for (int ty = tileMinY; ty <= tileMaxY; ++ty)
        {
            const std::size_t rowOffset = static_cast<std::size_t>(ty) * m_tilesX;
            for (int tx = tileMinX; tx <= tileMaxX; ++tx)
            {
                const float tileDepth = m_tileDepth[rowOffset + tx];
                // If any tile is not fully covered (tileDepth <= 0.0f) or the AABB is in front of the tile's furthest depth:
                if (tileDepth <= 0.0f || testDepth >= tileDepth)
                    return false;
            }
        }

        return true;
    }

private:
    int m_width = 0;
    int m_height = 0;
    int m_tilesX = 0;
    int m_tilesY = 0;
    std::vector<float> m_tileDepth;
    bool m_valid = false;
    SRMath::mat4 m_viewProjection{};
};

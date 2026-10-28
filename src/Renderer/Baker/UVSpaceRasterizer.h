#pragma once

#include "Graphics/Mesh.h"
#include "Graphics/Model.h"
#include "Math/SRMath.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace sr::baker
{

// Interpolated low-poly surface attributes at a UV texel
struct UVTexelSample
{
    SRMath::vec3 position{0.0f, 0.0f, 0.0f};  // Interpolated surface position P(u, v)
    SRMath::vec3 normal{0.0f, 1.0f, 0.0f};    // Interpolated surface normal N(u, v) (normalized)
    SRMath::vec3 tangent{1.0f, 0.0f, 0.0f};   // Interpolated surface tangent T(u, v) (orthonormalized)
    SRMath::vec3 bitangent{0.0f, 0.0f, 1.0f}; // Interpolated surface bitangent B(u, v) (orthonormalized)
    float coverage = 0.0f;                    // 1.0f if covered by low-poly triangle, 0.0f otherwise
    bool valid = false;                       // True if inside low-poly mesh UV triangle
    std::uint32_t triangleIndex = 0;          // Source triangle index in mesh
};

// 2D grid of UV texel surface samples
struct UVBuffer
{
    int width = 0;
    int height = 0;
    std::vector<UVTexelSample> samples;

    [[nodiscard]] const UVTexelSample &At(int x, int y) const noexcept
    {
        return samples[static_cast<std::size_t>(y) * width + x];
    }
    [[nodiscard]] UVTexelSample &At(int x, int y) noexcept
    {
        return samples[static_cast<std::size_t>(y) * width + x];
    }
    [[nodiscard]] bool IsValid(int x, int y) const noexcept
    {
        if (x < 0 || x >= width || y < 0 || y >= height)
            return false;
        return samples[static_cast<std::size_t>(y) * width + x].valid;
    }
};

// Rasterize low-poly mesh triangles into UV texture space [0, 1]^2
[[nodiscard]] UVBuffer RasterizeMeshUV(const Mesh &mesh, int width, int height);

// Rasterize all meshes in a model into a single UVBuffer
[[nodiscard]] UVBuffer RasterizeModelUV(const Model &model, int width, int height);

} // namespace sr::baker

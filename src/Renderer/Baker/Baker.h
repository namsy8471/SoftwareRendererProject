#pragma once

#include "Graphics/Mesh.h"
#include "Graphics/Model.h"
#include "Renderer/Baker/DilationFilter.h"
#include "Renderer/Baker/UVSpaceRasterizer.h"
#include "Utils/ImageFile.h"

#include <cstdint>
#include <string>

namespace sr::baker
{

struct BakeOptions
{
    int width = 1024;
    int height = 1024;
    int aoSamples = 64;          // Hemisphere cosine-weighted sample count (32~64)
    float cageDistance = 0.05f;  // Bidirectional cage ray distance along low-poly normal
    float aoMaxDistance = 10.0f; // Max ray distance for ambient occlusion occlusion test
    int dilationRadius = 8;      // Bleed border radius in pixels (4~8)
    bool bakeNormal = true;
    bool bakeAO = true;
};

struct BakeResult
{
    sr::image::Image normalMap; // 0x00RRGGBB normal map
    sr::image::Image aoMap;     // 0x00RRGGBB grayscale AO map
    int width = 0;
    int height = 0;
};

// High-poly surface raycast query result
struct HighPolyRayHit
{
    bool hit = false;
    float distance = 0.0f;
    SRMath::vec3 point{0.f, 0.f, 0.f};
    SRMath::vec3 normal{0.f, 1.f, 0.f};
};

// High-level baking function taking low-poly and high-poly models
[[nodiscard]] BakeResult Bake(const Model &highPolyModel, const Model &lowPolyModel,
                             const BakeOptions &options = {});

// Mesh-level baking function taking low-poly and high-poly meshes
[[nodiscard]] BakeResult Bake(const Mesh &highPolyMesh, const Mesh &lowPolyMesh,
                             const BakeOptions &options = {});

} // namespace sr::baker

#pragma once

#include "Math/SRMath.h"
#include "Graphics/Material.h"
#include "Renderer/RenderTypes.h"
#include "Renderer/Rasterizer.h"
#include <immintrin.h>

struct Lanes8
{
    __m256 x;
    __m256 y;
    __m256 z;
};

namespace sr::renderer::avx2
{
// 8-lane parallel Cook-Torrance GGX PBR, Fresnel-Schlick, Smith masking/shadowing,
// IBL Diffuse SH L2 (using _mm256_fmadd_ps when available), IBL Specular cubemap sampling,
// and ACES tone mapping in __m256.
void ShadeLanes8PBR(const Lanes8 &normal, const Lanes8 &position, const float (&u)[8], const float (&v)[8],
                    const float (&lod)[8], const Material *const (&material)[8], const RenderFrameContext &frame,
                    SRMath::vec3 (&colors)[8]) noexcept;

// 8-lane parallel Blinn-Phong shading in __m256.
void ShadeLanes8Phong(const Lanes8 &normal, const Lanes8 &position, const float (&u)[8], const float (&v)[8],
                      const float (&lod)[8], const Material *const (&material)[8], const RenderFrameContext &frame,
                      SRMath::vec3 (&colors)[8]) noexcept;

// Dispatches to ShadeLanes8PBR or ShadeLanes8Phong based on frame.shadingModel / material isPbr flags.
void ShadeLanes8(const Lanes8 &normal, const Lanes8 &position, const float (&u)[8], const float (&v)[8],
                 const float (&lod)[8], const Material *const (&material)[8], const RenderFrameContext &frame,
                 SRMath::vec3 (&colors)[8]) noexcept;

// 8-lane surface sample shading kernel.
void ShadeSurface8(const Rasterizer::SurfaceSample (&surface)[8], const Material *const (&material)[8],
                   const RenderFrameContext &frame, SRMath::vec3 (&colors)[8]) noexcept;

// 8-pixel batch shading for tile rasterization owners.
void ShadeOwner8(const PreparedTriangle *const (&triangle)[8],
                 const Rasterizer::OwnerSample *const (&sample)[8], const RenderFrameContext &frame,
                 SRMath::vec3 (&colors)[8]) noexcept;
} // namespace sr::renderer::avx2

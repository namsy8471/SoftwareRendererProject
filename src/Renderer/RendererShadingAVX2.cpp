// AVX2 8-lane SIMD shading kernel.
// 이 파일은 /arch:AVX2(-mavx2 -mfma)로 컴파일되며, 런타임 CPUID(avx2_available) 확인 후에만 진입한다.
#include "Renderer/RendererShadingAVX2.h"
#include "Graphics/IBL.h"
#include "Graphics/Light.h"
#include "Graphics/Material.h"
#include "Graphics/Mesh.h"
#include "Graphics/Texture.h"
#include "Math/SIMD.h"
#include "Platform/Compiler.h"
#include "Renderer/Rasterizer.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/ShadowMap.h"

#include <algorithm>
#include <cmath>
#include <immintrin.h>

namespace sr::renderer::avx2
{
namespace
{
SR_FORCEINLINE SRMath::vec3 sampleTexture(const Texture &tex, float u, float v, float lod,
                                          const RenderFrameContext &frame)
{
    const auto c = Rasterizer::SampleTexture(tex, u, v, lod, frame);
    return {c.r, c.g, c.b};
}

SRMath::vec3 perturbNormal(const SRMath::vec3 &normalWorld, const SRMath::vec3 &tangentWorld,
                           const SRMath::vec3 &bitangentWorld, float u, float v, float lod,
                           const Material &material, const RenderFrameContext &frame)
{
    if (!material.normalTexture)
        return normalWorld;
    const SRMath::vec3 n = SRMath::normalize(normalWorld);
    SRMath::vec3 t = tangentWorld - n * SRMath::dot(n, tangentWorld);
    if (SRMath::length(t) < 1e-5f)
    {
        const SRMath::vec3 axis =
            std::abs(n.x) < 0.9f ? SRMath::vec3(1.f, 0.f, 0.f) : SRMath::vec3(0.f, 1.f, 0.f);
        t = SRMath::normalize(SRMath::cross(n, axis));
    }
    else
        t = SRMath::normalize(t);
    SRMath::vec3 b = bitangentWorld - n * SRMath::dot(n, bitangentWorld) - t * SRMath::dot(t, bitangentWorld);
    if (SRMath::length(b) < 1e-5f)
        b = SRMath::normalize(SRMath::cross(n, t));
    else
        b = SRMath::normalize(b);

    const SRMath::vec3 mapSample = sampleTexture(*material.normalTexture, u, v, lod, frame);
    const float scale = material.normalScale;
    const SRMath::vec3 nTS = SRMath::normalize(
        SRMath::vec3((mapSample.x * 2.0f - 1.0f) * scale, (mapSample.y * 2.0f - 1.0f) * scale,
                     mapSample.z * 2.0f - 1.0f));
    return SRMath::normalize(t * nTS.x + b * nTS.y + n * nTS.z);
}

SR_FORCEINLINE float sRGBToLinear(float c) noexcept
{
    c = std::clamp(c, 0.0f, 1.0f);
    return (c <= 0.04045f) ? (c / 12.92f) : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

inline Lanes8 Transpose8(const SRMath::vec3 &v0, const SRMath::vec3 &v1, const SRMath::vec3 &v2, const SRMath::vec3 &v3,
                         const SRMath::vec3 &v4, const SRMath::vec3 &v5, const SRMath::vec3 &v6, const SRMath::vec3 &v7) noexcept
{
    __m128 r0 = v0.m128, r1 = v1.m128, r2 = v2.m128, r3 = v3.m128;
    _MM_TRANSPOSE4_PS(r0, r1, r2, r3);

    __m128 r4 = v4.m128, r5 = v5.m128, r6 = v6.m128, r7 = v7.m128;
    _MM_TRANSPOSE4_PS(r4, r5, r6, r7);

    return {
        _mm256_set_m128(r4, r0),
        _mm256_set_m128(r5, r1),
        _mm256_set_m128(r6, r2)
    };
}

inline void Unpack8(const Lanes8 &lanes, SRMath::vec3 (&colors)[8]) noexcept
{
    __m128 c0 = _mm256_castps256_ps128(lanes.x);
    __m128 c1 = _mm256_castps256_ps128(lanes.y);
    __m128 c2 = _mm256_castps256_ps128(lanes.z);
    __m128 c3 = _mm_setzero_ps();
    _MM_TRANSPOSE4_PS(c0, c1, c2, c3);
    colors[0].m128 = c0;
    colors[1].m128 = c1;
    colors[2].m128 = c2;
    colors[3].m128 = c3;

    __m128 c4 = _mm256_extractf128_ps(lanes.x, 1);
    __m128 c5 = _mm256_extractf128_ps(lanes.y, 1);
    __m128 c6 = _mm256_extractf128_ps(lanes.z, 1);
    __m128 c7 = _mm_setzero_ps();
    _MM_TRANSPOSE4_PS(c4, c5, c6, c7);
    colors[4].m128 = c4;
    colors[5].m128 = c5;
    colors[6].m128 = c6;
    colors[7].m128 = c7;
}

inline Lanes8 Broadcast8(const SRMath::vec3 &v) noexcept
{
    return {_mm256_set1_ps(v.x), _mm256_set1_ps(v.y), _mm256_set1_ps(v.z)};
}

inline __m256 Dot8(const Lanes8 &a, const Lanes8 &b) noexcept
{
    const __m256 xy = _mm256_add_ps(_mm256_mul_ps(a.x, b.x), _mm256_mul_ps(a.y, b.y));
    return _mm256_add_ps(xy, _mm256_mul_ps(a.z, b.z));
}

inline Lanes8 Normalize8(const Lanes8 &v) noexcept
{
    const __m256 length = _mm256_sqrt_ps(Dot8(v, v));
    const __m256 keep = _mm256_cmp_ps(length, _mm256_set1_ps(1e-5f), _CMP_LT_OQ);
    const __m256 reciprocal = _mm256_div_ps(_mm256_set1_ps(1.0f), length);
    return {_mm256_blendv_ps(_mm256_mul_ps(v.x, reciprocal), v.x, keep),
            _mm256_blendv_ps(_mm256_mul_ps(v.y, reciprocal), v.y, keep),
            _mm256_blendv_ps(_mm256_mul_ps(v.z, reciprocal), v.z, keep)};
}

inline __m256 GGX_D8(__m256 NdotH, __m256 alpha2) noexcept
{
    const __m256 one = _mm256_set1_ps(1.0f);
    const __m256 pi = _mm256_set1_ps(3.14159265358979323846f);
    const __m256 NdotH2 = _mm256_mul_ps(NdotH, NdotH);
    const __m256 d = _mm256_add_ps(_mm256_mul_ps(NdotH2, _mm256_sub_ps(alpha2, one)), one);
    const __m256 denom = _mm256_mul_ps(pi, _mm256_mul_ps(d, d));
    return _mm256_div_ps(alpha2, denom);
}

inline __m256 SmithGGX_V8(__m256 NdotL, __m256 NdotV, __m256 alpha2) noexcept
{
    const __m256 one = _mm256_set1_ps(1.0f);
    const __m256 half = _mm256_set1_ps(0.5f);
    const __m256 eps = _mm256_set1_ps(1e-7f);
    const __m256 oneMinusAlpha2 = _mm256_sub_ps(one, alpha2);

    const __m256 termL = _mm256_add_ps(_mm256_mul_ps(_mm256_mul_ps(NdotL, NdotL), oneMinusAlpha2), alpha2);
    const __m256 termV = _mm256_add_ps(_mm256_mul_ps(_mm256_mul_ps(NdotV, NdotV), oneMinusAlpha2), alpha2);
    const __m256 denomL = _mm256_mul_ps(NdotV, _mm256_sqrt_ps(termL));
    const __m256 denomV = _mm256_mul_ps(NdotL, _mm256_sqrt_ps(termV));
    const __m256 denom = _mm256_max_ps(_mm256_add_ps(denomL, denomV), eps);
    return _mm256_div_ps(half, denom);
}

inline Lanes8 Schlick_F8(__m256 VdotH, const Lanes8 &F0) noexcept
{
    const __m256 one = _mm256_set1_ps(1.0f);
    const __m256 f = _mm256_sub_ps(one, VdotH);
    const __m256 f2 = _mm256_mul_ps(f, f);
    const __m256 f5 = _mm256_mul_ps(_mm256_mul_ps(f2, f2), f);
    return {_mm256_add_ps(F0.x, _mm256_mul_ps(_mm256_sub_ps(one, F0.x), f5)),
            _mm256_add_ps(F0.y, _mm256_mul_ps(_mm256_sub_ps(one, F0.y), f5)),
            _mm256_add_ps(F0.z, _mm256_mul_ps(_mm256_sub_ps(one, F0.z), f5))};
}

inline void EnvBRDFApprox8(__m256 NdotV, __m256 roughness, __m256 &scaleA, __m256 &biasB) noexcept
{
    const __m256 r_x = _mm256_add_ps(_mm256_mul_ps(roughness, _mm256_set1_ps(-1.0f)), _mm256_set1_ps(1.0f));
    const __m256 r_y = _mm256_add_ps(_mm256_mul_ps(roughness, _mm256_set1_ps(-0.0275f)), _mm256_set1_ps(0.0425f));
    const __m256 r_z = _mm256_add_ps(_mm256_mul_ps(roughness, _mm256_set1_ps(-0.572f)), _mm256_set1_ps(1.04f));
    const __m256 r_w = _mm256_add_ps(_mm256_mul_ps(roughness, _mm256_set1_ps(0.022f)), _mm256_set1_ps(-0.04f));

    alignas(32) float nv[8];
    _mm256_store_ps(nv, NdotV);
    alignas(32) float e[8];
    for (int i = 0; i < 8; ++i)
        e[i] = std::exp2(-9.28f * nv[i]);
    const __m256 expTerm = _mm256_load_ps(e);

    const __m256 r_x2 = _mm256_mul_ps(r_x, r_x);
    const __m256 minTerm = _mm256_min_ps(r_x2, expTerm);
    const __m256 a004 = _mm256_add_ps(_mm256_mul_ps(minTerm, r_x), r_y);

    scaleA = _mm256_max_ps(_mm256_setzero_ps(), _mm256_add_ps(_mm256_mul_ps(_mm256_set1_ps(-1.04f), a004), r_z));
    biasB = _mm256_max_ps(_mm256_setzero_ps(), _mm256_add_ps(_mm256_mul_ps(_mm256_set1_ps(0.04f), a004), r_w));
}

inline Lanes8 ACESFilm8(const Lanes8 &v) noexcept
{
    const __m256 zero = _mm256_setzero_ps();
    const __m256 one = _mm256_set1_ps(1.0f);
    const __m256 a = _mm256_set1_ps(2.51f);
    const __m256 b = _mm256_set1_ps(0.03f);
    const __m256 c = _mm256_set1_ps(2.43f);
    const __m256 d = _mm256_set1_ps(0.59f);
    const __m256 e = _mm256_set1_ps(0.14f);

    auto mapLane = [&](__m256 x) {
        x = _mm256_max_ps(zero, x);
        const __m256 num = _mm256_mul_ps(x, _mm256_add_ps(_mm256_mul_ps(a, x), b));
        const __m256 den = _mm256_add_ps(_mm256_mul_ps(x, _mm256_add_ps(_mm256_mul_ps(c, x), d)), e);
        return _mm256_max_ps(zero, _mm256_min_ps(one, _mm256_div_ps(num, den)));
    };

    return Lanes8{mapLane(v.x), mapLane(v.y), mapLane(v.z)};
}

// 8-lane evaluation of L2 Spherical Harmonics for diffuse IBL.
// Uses _mm256_fmadd_ps for fused multiply-add operations when FMA is available.
inline Lanes8 EvaluateSH8(const sr::SphericalHarmonicsL2 &sh, const Lanes8 &normal, bool useFma) noexcept
{
    constexpr float c1 = 0.429043f;
    constexpr float c2 = 0.511664f;
    constexpr float c3 = 0.743125f;
    constexpr float c4 = 0.886227f;
    constexpr float c5 = 0.247708f;

    const __m256 nx = normal.x;
    const __m256 ny = normal.y;
    const __m256 nz = normal.z;

    const __m256 term8 = _mm256_mul_ps(_mm256_set1_ps(c1), _mm256_sub_ps(_mm256_mul_ps(nx, nx), _mm256_mul_ps(nz, nz)));
    const __m256 term6 = _mm256_sub_ps(_mm256_mul_ps(_mm256_set1_ps(c3), _mm256_mul_ps(ny, ny)), _mm256_set1_ps(c5));
    const __m256 term0 = _mm256_set1_ps(c4);
    const __m256 term4 = _mm256_mul_ps(_mm256_set1_ps(2.0f * c1), _mm256_mul_ps(nx, nz));
    const __m256 term7 = _mm256_mul_ps(_mm256_set1_ps(2.0f * c1), _mm256_mul_ps(nx, ny));
    const __m256 term5 = _mm256_mul_ps(_mm256_set1_ps(2.0f * c1), _mm256_mul_ps(ny, nz));
    const __m256 term3 = _mm256_mul_ps(_mm256_set1_ps(2.0f * c2), nx);
    const __m256 term1 = _mm256_mul_ps(_mm256_set1_ps(2.0f * c2), nz);
    const __m256 term2 = _mm256_mul_ps(_mm256_set1_ps(2.0f * c2), ny);

    auto evalChannelFMA = [&](float a8, float a6, float a0, float a4, float a7, float a5, float a3, float a1, float a2)
    {
        __m256 acc = _mm256_mul_ps(_mm256_set1_ps(a8), term8);
        acc = _mm256_fmadd_ps(_mm256_set1_ps(a6), term6, acc);
        acc = _mm256_fmadd_ps(_mm256_set1_ps(a0), term0, acc);
        acc = _mm256_fmadd_ps(_mm256_set1_ps(a4), term4, acc);
        acc = _mm256_fmadd_ps(_mm256_set1_ps(a7), term7, acc);
        acc = _mm256_fmadd_ps(_mm256_set1_ps(a5), term5, acc);
        acc = _mm256_fmadd_ps(_mm256_set1_ps(a3), term3, acc);
        acc = _mm256_fmadd_ps(_mm256_set1_ps(a1), term1, acc);
        acc = _mm256_fmadd_ps(_mm256_set1_ps(a2), term2, acc);
        return _mm256_max_ps(_mm256_setzero_ps(), acc);
    };

    auto evalChannelMulAdd = [&](float a8, float a6, float a0, float a4, float a7, float a5, float a3, float a1, float a2)
    {
        __m256 acc = _mm256_mul_ps(_mm256_set1_ps(a8), term8);
        acc = _mm256_add_ps(acc, _mm256_mul_ps(_mm256_set1_ps(a6), term6));
        acc = _mm256_add_ps(acc, _mm256_mul_ps(_mm256_set1_ps(a0), term0));
        acc = _mm256_add_ps(acc, _mm256_mul_ps(_mm256_set1_ps(a4), term4));
        acc = _mm256_add_ps(acc, _mm256_mul_ps(_mm256_set1_ps(a7), term7));
        acc = _mm256_add_ps(acc, _mm256_mul_ps(_mm256_set1_ps(a5), term5));
        acc = _mm256_add_ps(acc, _mm256_mul_ps(_mm256_set1_ps(a3), term3));
        acc = _mm256_add_ps(acc, _mm256_mul_ps(_mm256_set1_ps(a1), term1));
        acc = _mm256_add_ps(acc, _mm256_mul_ps(_mm256_set1_ps(a2), term2));
        return _mm256_max_ps(_mm256_setzero_ps(), acc);
    };

    Lanes8 irradiance;
    if (useFma)
    {
        irradiance.x = evalChannelFMA(sh.coeffs[8].x, sh.coeffs[6].x, sh.coeffs[0].x, sh.coeffs[4].x, sh.coeffs[7].x, sh.coeffs[5].x, sh.coeffs[3].x, sh.coeffs[1].x, sh.coeffs[2].x);
        irradiance.y = evalChannelFMA(sh.coeffs[8].y, sh.coeffs[6].y, sh.coeffs[0].y, sh.coeffs[4].y, sh.coeffs[7].y, sh.coeffs[5].y, sh.coeffs[3].y, sh.coeffs[1].y, sh.coeffs[2].y);
        irradiance.z = evalChannelFMA(sh.coeffs[8].z, sh.coeffs[6].z, sh.coeffs[0].z, sh.coeffs[4].z, sh.coeffs[7].z, sh.coeffs[5].z, sh.coeffs[3].z, sh.coeffs[1].z, sh.coeffs[2].z);
    }
    else
    {
        irradiance.x = evalChannelMulAdd(sh.coeffs[8].x, sh.coeffs[6].x, sh.coeffs[0].x, sh.coeffs[4].x, sh.coeffs[7].x, sh.coeffs[5].x, sh.coeffs[3].x, sh.coeffs[1].x, sh.coeffs[2].x);
        irradiance.y = evalChannelMulAdd(sh.coeffs[8].y, sh.coeffs[6].y, sh.coeffs[0].y, sh.coeffs[4].y, sh.coeffs[7].y, sh.coeffs[5].y, sh.coeffs[3].y, sh.coeffs[1].y, sh.coeffs[2].y);
        irradiance.z = evalChannelMulAdd(sh.coeffs[8].z, sh.coeffs[6].z, sh.coeffs[0].z, sh.coeffs[4].z, sh.coeffs[7].z, sh.coeffs[5].z, sh.coeffs[3].z, sh.coeffs[1].z, sh.coeffs[2].z);
    }
    return irradiance;
}
} // namespace

void ShadeLanes8PBR(const Lanes8 &normal, const Lanes8 &position, const float (&u)[8], const float (&v)[8],
                    const float (&lod)[8], const Material *const (&material)[8], const RenderFrameContext &frame,
                    SRMath::vec3 (&colors)[8]) noexcept
{
    SRMath::vec3 baseColor[8];
    float roughness[8], metallic[8], occlusion[8];
    SRMath::vec3 emissive[8], ambientMat[8];

    for (int i = 0; i < 8; ++i)
    {
        const Material &m = *material[i];
        if (m.isPbr)
        {
            baseColor[i] = {m.baseColor.x, m.baseColor.y, m.baseColor.z};
            roughness[i] = m.roughness;
            metallic[i] = m.metallic;
        }
        else
        {
            baseColor[i] = m.diffuse;
            metallic[i] = 0.0f;
            roughness[i] = std::clamp(std::sqrt(2.0f / (m.shininess + 2.0f)), 0.04f, 1.0f);
        }
        if (m.diffuseTexture)
        {
            const auto diff = Rasterizer::SampleTexture(*m.diffuseTexture, u[i], v[i], lod[i], frame);
            baseColor[i].x *= sRGBToLinear(diff.r);
            baseColor[i].y *= sRGBToLinear(diff.g);
            baseColor[i].z *= sRGBToLinear(diff.b);
        }
        if (m.metallicRoughnessTexture)
        {
            const auto mr = Rasterizer::SampleTexture(*m.metallicRoughnessTexture, u[i], v[i], lod[i], frame);
            roughness[i] *= mr.g;
            metallic[i] *= mr.b;
        }
        roughness[i] = std::clamp(roughness[i], 0.04f, 1.0f);
        metallic[i] = std::clamp(metallic[i], 0.0f, 1.0f);

        occlusion[i] = 1.0f;
        if (m.occlusionTexture)
        {
            const auto occ = Rasterizer::SampleTexture(*m.occlusionTexture, u[i], v[i], lod[i], frame);
            occlusion[i] = std::clamp(1.0f + m.occlusionStrength * (occ.r - 1.0f), 0.0f, 1.0f);
        }

        emissive[i] = m.emissive;
        if (m.emissiveTexture)
        {
            const auto em = Rasterizer::SampleTexture(*m.emissiveTexture, u[i], v[i], lod[i], frame);
            emissive[i].x *= sRGBToLinear(em.r);
            emissive[i].y *= sRGBToLinear(em.g);
            emissive[i].z *= sRGBToLinear(em.b);
        }

        ambientMat[i] = m.ambient;
    }

    const Lanes8 base = Transpose8(baseColor[0], baseColor[1], baseColor[2], baseColor[3],
                                   baseColor[4], baseColor[5], baseColor[6], baseColor[7]);
    const __m256 r = _mm256_setr_ps(roughness[0], roughness[1], roughness[2], roughness[3],
                                    roughness[4], roughness[5], roughness[6], roughness[7]);
    const __m256 m = _mm256_setr_ps(metallic[0], metallic[1], metallic[2], metallic[3],
                                    metallic[4], metallic[5], metallic[6], metallic[7]);
    const __m256 occ = _mm256_setr_ps(occlusion[0], occlusion[1], occlusion[2], occlusion[3],
                                      occlusion[4], occlusion[5], occlusion[6], occlusion[7]);
    const Lanes8 em = Transpose8(emissive[0], emissive[1], emissive[2], emissive[3],
                                 emissive[4], emissive[5], emissive[6], emissive[7]);
    const Lanes8 ambMat = Transpose8(ambientMat[0], ambientMat[1], ambientMat[2], ambientMat[3],
                                     ambientMat[4], ambientMat[5], ambientMat[6], ambientMat[7]);

    const __m256 alpha = _mm256_mul_ps(r, r);
    const __m256 alpha2 = _mm256_mul_ps(alpha, alpha);

    const __m256 zero = _mm256_setzero_ps();
    const __m256 one = _mm256_set1_ps(1.0f);
    const __m256 pointZeroFour = _mm256_set1_ps(0.04f);
    const __m256 pi = _mm256_set1_ps(3.14159265358979323846f);

    const __m256 oneMinusM = _mm256_sub_ps(one, m);
    const Lanes8 F0{
        _mm256_add_ps(_mm256_mul_ps(pointZeroFour, oneMinusM), _mm256_mul_ps(base.x, m)),
        _mm256_add_ps(_mm256_mul_ps(pointZeroFour, oneMinusM), _mm256_mul_ps(base.y, m)),
        _mm256_add_ps(_mm256_mul_ps(pointZeroFour, oneMinusM), _mm256_mul_ps(base.z, m))
    };

    const Lanes8 camera = Broadcast8(frame.cameraPosition);
    const Lanes8 viewDir = Normalize8({_mm256_sub_ps(camera.x, position.x),
                                       _mm256_sub_ps(camera.y, position.y),
                                       _mm256_sub_ps(camera.z, position.z)});

    const __m256 NdotV = _mm256_max_ps(_mm256_set1_ps(1e-4f), _mm256_min_ps(one, Dot8(normal, viewDir)));

    Lanes8 totalDirect{zero, zero, zero};

    for (const auto &light : frame.lights)
    {
        const Lanes8 lightDir = Broadcast8(light.directionToLight);
        const Lanes8 lightColor = Broadcast8(light.color);

        const __m256 NdotL = _mm256_max_ps(zero, Dot8(normal, lightDir));
        const __m256 activeMask = _mm256_cmp_ps(NdotL, zero, _CMP_GT_OQ);
        if (_mm256_movemask_ps(activeMask) == 0)
            continue;

        const Lanes8 H = Normalize8({_mm256_add_ps(lightDir.x, viewDir.x),
                                     _mm256_add_ps(lightDir.y, viewDir.y),
                                     _mm256_add_ps(lightDir.z, viewDir.z)});
        const __m256 NdotH = _mm256_max_ps(zero, _mm256_min_ps(one, Dot8(normal, H)));
        const __m256 VdotH = _mm256_max_ps(zero, _mm256_min_ps(one, Dot8(viewDir, H)));

        const __m256 D = GGX_D8(NdotH, alpha2);
        const __m256 V = SmithGGX_V8(NdotL, NdotV, alpha2);
        const Lanes8 F = Schlick_F8(VdotH, F0);

        const __m256 piDV = _mm256_mul_ps(pi, _mm256_mul_ps(D, V));
        const Lanes8 specular{_mm256_mul_ps(F.x, piDV), _mm256_mul_ps(F.y, piDV), _mm256_mul_ps(F.z, piDV)};

        const __m256 kD_scale = oneMinusM;
        const Lanes8 diffuse{_mm256_mul_ps(_mm256_mul_ps(_mm256_sub_ps(one, F.x), kD_scale), base.x),
                             _mm256_mul_ps(_mm256_mul_ps(_mm256_sub_ps(one, F.y), kD_scale), base.y),
                             _mm256_mul_ps(_mm256_mul_ps(_mm256_sub_ps(one, F.z), kD_scale), base.z)};

        __m256 factorX = _mm256_mul_ps(_mm256_add_ps(diffuse.x, specular.x), _mm256_mul_ps(lightColor.x, NdotL));
        __m256 factorY = _mm256_mul_ps(_mm256_add_ps(diffuse.y, specular.y), _mm256_mul_ps(lightColor.y, NdotL));
        __m256 factorZ = _mm256_mul_ps(_mm256_add_ps(diffuse.z, specular.z), _mm256_mul_ps(lightColor.z, NdotL));

        if (frame.shadowMap != nullptr && frame.shadowMap->lightType == ShadowMap::LightType::Directional)
        {
            alignas(32) float vis[8];
            alignas(32) float px[8], py[8], pz[8], nx[8], ny[8], nz[8];
            _mm256_store_ps(px, position.x); _mm256_store_ps(py, position.y); _mm256_store_ps(pz, position.z);
            _mm256_store_ps(nx, normal.x); _mm256_store_ps(ny, normal.y); _mm256_store_ps(nz, normal.z);
            for (int i = 0; i < 8; ++i)
            {
                vis[i] = frame.shadowMap->SampleVisibility({px[i], py[i], pz[i]}, {nx[i], ny[i], nz[i]}, light.directionToLight);
            }
            const __m256 shadowVis = _mm256_load_ps(vis);
            factorX = _mm256_mul_ps(factorX, shadowVis);
            factorY = _mm256_mul_ps(factorY, shadowVis);
            factorZ = _mm256_mul_ps(factorZ, shadowVis);
        }

        totalDirect.x = _mm256_add_ps(totalDirect.x, _mm256_and_ps(activeMask, factorX));
        totalDirect.y = _mm256_add_ps(totalDirect.y, _mm256_and_ps(activeMask, factorY));
        totalDirect.z = _mm256_add_ps(totalDirect.z, _mm256_and_ps(activeMask, factorZ));
    }

    if (!frame.pointLights.empty())
    {
        for (const auto &light : frame.pointLights)
        {
            const Lanes8 lightPos = Broadcast8(light.position);
            const Lanes8 lightColor = Broadcast8(light.color);
            const Lanes8 toLight{_mm256_sub_ps(lightPos.x, position.x), _mm256_sub_ps(lightPos.y, position.y),
                                 _mm256_sub_ps(lightPos.z, position.z)};
            const __m256 distSq = Dot8(toLight, toLight);
            const __m256 dist = _mm256_sqrt_ps(distSq);
            const Lanes8 lightDir = Normalize8(toLight);

            const __m256 ratio = _mm256_mul_ps(dist, _mm256_set1_ps(light.invRange));
            const __m256 ratioSq = _mm256_mul_ps(ratio, ratio);
            const __m256 window = _mm256_max_ps(zero, _mm256_sub_ps(one, _mm256_mul_ps(ratioSq, ratioSq)));
            const __m256 denom = _mm256_add_ps(_mm256_set1_ps(light.constant),
                                               _mm256_add_ps(_mm256_mul_ps(_mm256_set1_ps(light.linear), dist),
                                                             _mm256_mul_ps(_mm256_set1_ps(light.quadratic), distSq)));
            const __m256 atten = _mm256_div_ps(_mm256_mul_ps(window, window), _mm256_max_ps(denom, _mm256_set1_ps(1e-4f)));

            const __m256 NdotL = _mm256_max_ps(zero, Dot8(normal, lightDir));
            const __m256 activeMask = _mm256_and_ps(_mm256_cmp_ps(atten, zero, _CMP_GT_OQ), _mm256_cmp_ps(NdotL, zero, _CMP_GT_OQ));
            if (_mm256_movemask_ps(activeMask) == 0)
                continue;

            const Lanes8 H = Normalize8({_mm256_add_ps(lightDir.x, viewDir.x),
                                         _mm256_add_ps(lightDir.y, viewDir.y),
                                         _mm256_add_ps(lightDir.z, viewDir.z)});
            const __m256 NdotH = _mm256_max_ps(zero, _mm256_min_ps(one, Dot8(normal, H)));
            const __m256 VdotH = _mm256_max_ps(zero, _mm256_min_ps(one, Dot8(viewDir, H)));

            const __m256 D = GGX_D8(NdotH, alpha2);
            const __m256 V = SmithGGX_V8(NdotL, NdotV, alpha2);
            const Lanes8 F = Schlick_F8(VdotH, F0);

            const __m256 piDV = _mm256_mul_ps(pi, _mm256_mul_ps(D, V));
            const Lanes8 specular{_mm256_mul_ps(F.x, piDV), _mm256_mul_ps(F.y, piDV), _mm256_mul_ps(F.z, piDV)};

            const __m256 kD_scale = oneMinusM;
            const Lanes8 diffuse{_mm256_mul_ps(_mm256_mul_ps(_mm256_sub_ps(one, F.x), kD_scale), base.x),
                                 _mm256_mul_ps(_mm256_mul_ps(_mm256_sub_ps(one, F.y), kD_scale), base.y),
                                 _mm256_mul_ps(_mm256_mul_ps(_mm256_sub_ps(one, F.z), kD_scale), base.z)};

            const __m256 lightScale = _mm256_mul_ps(atten, NdotL);
            const __m256 factorX = _mm256_mul_ps(_mm256_add_ps(diffuse.x, specular.x), _mm256_mul_ps(lightColor.x, lightScale));
            const __m256 factorY = _mm256_mul_ps(_mm256_add_ps(diffuse.y, specular.y), _mm256_mul_ps(lightColor.y, lightScale));
            const __m256 factorZ = _mm256_mul_ps(_mm256_add_ps(diffuse.z, specular.z), _mm256_mul_ps(lightColor.z, lightScale));

            totalDirect.x = _mm256_add_ps(totalDirect.x, _mm256_and_ps(activeMask, factorX));
            totalDirect.y = _mm256_add_ps(totalDirect.y, _mm256_and_ps(activeMask, factorY));
            totalDirect.z = _mm256_add_ps(totalDirect.z, _mm256_and_ps(activeMask, factorZ));
        }
    }

    if (!frame.spotLights.empty())
    {
        for (const auto &light : frame.spotLights)
        {
            const Lanes8 lightPos = Broadcast8(light.position);
            const Lanes8 lightColor = Broadcast8(light.color);
            const Lanes8 spotDir = Broadcast8(light.directionToLight);
            const Lanes8 toLight{_mm256_sub_ps(lightPos.x, position.x), _mm256_sub_ps(lightPos.y, position.y),
                                 _mm256_sub_ps(lightPos.z, position.z)};
            const __m256 distSq = Dot8(toLight, toLight);
            const __m256 dist = _mm256_sqrt_ps(distSq);
            const Lanes8 lightDir = Normalize8(toLight);

            const __m256 ratio = _mm256_mul_ps(dist, _mm256_set1_ps(light.invRange));
            const __m256 ratioSq = _mm256_mul_ps(ratio, ratio);
            const __m256 window = _mm256_max_ps(zero, _mm256_sub_ps(one, _mm256_mul_ps(ratioSq, ratioSq)));
            const __m256 denom = _mm256_add_ps(_mm256_set1_ps(light.constant),
                                               _mm256_add_ps(_mm256_mul_ps(_mm256_set1_ps(light.linear), dist),
                                                             _mm256_mul_ps(_mm256_set1_ps(light.quadratic), distSq)));
            const __m256 distAtten = _mm256_div_ps(_mm256_mul_ps(window, window), _mm256_max_ps(denom, _mm256_set1_ps(1e-4f)));

            const __m256 cosTheta = Dot8(lightDir, spotDir);
            const __m256 rawT = _mm256_mul_ps(_mm256_sub_ps(cosTheta, _mm256_set1_ps(light.outerConeCos)),
                                              _mm256_set1_ps(light.invConeCosDiff));
            const __m256 spotT = _mm256_max_ps(zero, _mm256_min_ps(one, rawT));
            const __m256 spotFactor = _mm256_mul_ps(_mm256_mul_ps(spotT, spotT),
                                                    _mm256_sub_ps(_mm256_set1_ps(3.0f), _mm256_mul_ps(_mm256_set1_ps(2.0f), spotT)));
            const __m256 atten = _mm256_mul_ps(distAtten, spotFactor);

            const __m256 NdotL = _mm256_max_ps(zero, Dot8(normal, lightDir));
            const __m256 activeMask = _mm256_and_ps(_mm256_cmp_ps(atten, zero, _CMP_GT_OQ), _mm256_cmp_ps(NdotL, zero, _CMP_GT_OQ));
            if (_mm256_movemask_ps(activeMask) == 0)
                continue;

            const Lanes8 H = Normalize8({_mm256_add_ps(lightDir.x, viewDir.x),
                                         _mm256_add_ps(lightDir.y, viewDir.y),
                                         _mm256_add_ps(lightDir.z, viewDir.z)});
            const __m256 NdotH = _mm256_max_ps(zero, _mm256_min_ps(one, Dot8(normal, H)));
            const __m256 VdotH = _mm256_max_ps(zero, _mm256_min_ps(one, Dot8(viewDir, H)));

            const __m256 D = GGX_D8(NdotH, alpha2);
            const __m256 V = SmithGGX_V8(NdotL, NdotV, alpha2);
            const Lanes8 F = Schlick_F8(VdotH, F0);

            const __m256 piDV = _mm256_mul_ps(pi, _mm256_mul_ps(D, V));
            const Lanes8 specular{_mm256_mul_ps(F.x, piDV), _mm256_mul_ps(F.y, piDV), _mm256_mul_ps(F.z, piDV)};

            const __m256 kD_scale = oneMinusM;
            const Lanes8 diffuse{_mm256_mul_ps(_mm256_mul_ps(_mm256_sub_ps(one, F.x), kD_scale), base.x),
                                 _mm256_mul_ps(_mm256_mul_ps(_mm256_sub_ps(one, F.y), kD_scale), base.y),
                                 _mm256_mul_ps(_mm256_mul_ps(_mm256_sub_ps(one, F.z), kD_scale), base.z)};

            const __m256 lightScale = _mm256_mul_ps(atten, NdotL);
            __m256 factorX = _mm256_mul_ps(_mm256_add_ps(diffuse.x, specular.x), _mm256_mul_ps(lightColor.x, lightScale));
            __m256 factorY = _mm256_mul_ps(_mm256_add_ps(diffuse.y, specular.y), _mm256_mul_ps(lightColor.y, lightScale));
            __m256 factorZ = _mm256_mul_ps(_mm256_add_ps(diffuse.z, specular.z), _mm256_mul_ps(lightColor.z, lightScale));

            if (frame.shadowMap != nullptr && frame.shadowMap->lightType == ShadowMap::LightType::Spot)
            {
                alignas(32) float vis[8];
                alignas(32) float px[8], py[8], pz[8], nx[8], ny[8], nz[8], lx[8], ly[8], lz[8];
                _mm256_store_ps(px, position.x); _mm256_store_ps(py, position.y); _mm256_store_ps(pz, position.z);
                _mm256_store_ps(nx, normal.x); _mm256_store_ps(ny, normal.y); _mm256_store_ps(nz, normal.z);
                _mm256_store_ps(lx, lightDir.x); _mm256_store_ps(ly, lightDir.y); _mm256_store_ps(lz, lightDir.z);
                for (int i = 0; i < 8; ++i)
                {
                    vis[i] = frame.shadowMap->SampleVisibility({px[i], py[i], pz[i]}, {nx[i], ny[i], nz[i]}, {lx[i], ly[i], lz[i]});
                }
                const __m256 shadowVis = _mm256_load_ps(vis);
                factorX = _mm256_mul_ps(factorX, shadowVis);
                factorY = _mm256_mul_ps(factorY, shadowVis);
                factorZ = _mm256_mul_ps(factorZ, shadowVis);
            }

            totalDirect.x = _mm256_add_ps(totalDirect.x, _mm256_and_ps(activeMask, factorX));
            totalDirect.y = _mm256_add_ps(totalDirect.y, _mm256_and_ps(activeMask, factorY));
            totalDirect.z = _mm256_add_ps(totalDirect.z, _mm256_and_ps(activeMask, factorZ));
        }
    }

    Lanes8 ambient{_mm256_mul_ps(_mm256_mul_ps(base.x, ambMat.x), occ),
                   _mm256_mul_ps(_mm256_mul_ps(base.y, ambMat.y), occ),
                   _mm256_mul_ps(_mm256_mul_ps(base.z, ambMat.z), occ)};

    if (frame.ibl && frame.iblMap != nullptr)
    {
        const auto *ibl = static_cast<const sr::IBLMap *>(frame.iblMap);

        // 1. Diffuse IBL (Spherical Harmonics L2 evaluation across 8 lanes)
        const bool useFma = SRMath::SIMD::fma_available();
        const Lanes8 irradiance = EvaluateSH8(ibl->sh, normal, useFma);

        const __m256 oneMinusNdotV = _mm256_sub_ps(one, NdotV);
        const __m256 f2 = _mm256_mul_ps(oneMinusNdotV, oneMinusNdotV);
        const __m256 f5 = _mm256_mul_ps(_mm256_mul_ps(f2, f2), oneMinusNdotV);
        const __m256 oneMinusR = _mm256_sub_ps(one, r);
        const Lanes8 maxF{_mm256_max_ps(oneMinusR, F0.x), _mm256_max_ps(oneMinusR, F0.y), _mm256_max_ps(oneMinusR, F0.z)};
        const Lanes8 F_rough{
            _mm256_add_ps(F0.x, _mm256_mul_ps(_mm256_sub_ps(maxF.x, F0.x), f5)),
            _mm256_add_ps(F0.y, _mm256_mul_ps(_mm256_sub_ps(maxF.y, F0.y), f5)),
            _mm256_add_ps(F0.z, _mm256_mul_ps(_mm256_sub_ps(maxF.z, F0.z), f5))
        };
        const __m256 kD_scale = _mm256_sub_ps(one, m);
        const Lanes8 diffuseIBL{
            _mm256_mul_ps(_mm256_mul_ps(_mm256_mul_ps(_mm256_sub_ps(one, F_rough.x), kD_scale), base.x), irradiance.x),
            _mm256_mul_ps(_mm256_mul_ps(_mm256_mul_ps(_mm256_sub_ps(one, F_rough.y), kD_scale), base.y), irradiance.y),
            _mm256_mul_ps(_mm256_mul_ps(_mm256_mul_ps(_mm256_sub_ps(one, F_rough.z), kD_scale), base.z), irradiance.z)
        };

        // 2. Specular IBL (Prefiltered cubemap sampling and Karis EnvBRDF approximate split-sum)
        const __m256 twoNdotV = _mm256_mul_ps(_mm256_set1_ps(2.0f), NdotV);
        const Lanes8 R = Normalize8({
            _mm256_sub_ps(_mm256_mul_ps(twoNdotV, normal.x), viewDir.x),
            _mm256_sub_ps(_mm256_mul_ps(twoNdotV, normal.y), viewDir.y),
            _mm256_sub_ps(_mm256_mul_ps(twoNdotV, normal.z), viewDir.z)
        });

        alignas(32) float rx[8], ry[8], rz[8], rough[8];
        _mm256_store_ps(rx, R.x); _mm256_store_ps(ry, R.y); _mm256_store_ps(rz, R.z);
        _mm256_store_ps(rough, r);
        alignas(32) float specR[8], specG[8], specB[8];
        for (int i = 0; i < 8; ++i)
        {
            const auto sp = ibl->SampleSpecular({rx[i], ry[i], rz[i]}, rough[i]);
            specR[i] = sp.x; specG[i] = sp.y; specB[i] = sp.z;
        }
        const Lanes8 prefilteredColor{_mm256_load_ps(specR), _mm256_load_ps(specG), _mm256_load_ps(specB)};

        __m256 scaleA, biasB;
        EnvBRDFApprox8(NdotV, r, scaleA, biasB);

        const Lanes8 specularIBL{
            _mm256_mul_ps(prefilteredColor.x, _mm256_add_ps(_mm256_mul_ps(F0.x, scaleA), biasB)),
            _mm256_mul_ps(prefilteredColor.y, _mm256_add_ps(_mm256_mul_ps(F0.y, scaleA), biasB)),
            _mm256_mul_ps(prefilteredColor.z, _mm256_add_ps(_mm256_mul_ps(F0.z, scaleA), biasB))
        };

        ambient.x = _mm256_mul_ps(_mm256_add_ps(diffuseIBL.x, specularIBL.x), occ);
        ambient.y = _mm256_mul_ps(_mm256_add_ps(diffuseIBL.y, specularIBL.y), occ);
        ambient.z = _mm256_mul_ps(_mm256_add_ps(diffuseIBL.z, specularIBL.z), occ);
    }

    Lanes8 finalColor{
        _mm256_add_ps(_mm256_add_ps(ambient.x, totalDirect.x), em.x),
        _mm256_add_ps(_mm256_add_ps(ambient.y, totalDirect.y), em.y),
        _mm256_add_ps(_mm256_add_ps(ambient.z, totalDirect.z), em.z)
    };

    if (frame.tonemap)
    {
        finalColor = ACESFilm8(finalColor);
    }
    else
    {
        finalColor.x = _mm256_max_ps(zero, _mm256_min_ps(one, finalColor.x));
        finalColor.y = _mm256_max_ps(zero, _mm256_min_ps(one, finalColor.y));
        finalColor.z = _mm256_max_ps(zero, _mm256_min_ps(one, finalColor.z));
    }

    Unpack8(finalColor, colors);

    if (frame.srgbGamma)
    {
        constexpr float invGamma = 1.0f / 2.2f;
        for (int i = 0; i < 8; ++i)
        {
            colors[i].x = std::pow(colors[i].x, invGamma);
            colors[i].y = std::pow(colors[i].y, invGamma);
            colors[i].z = std::pow(colors[i].z, invGamma);
        }
    }
}

void ShadeLanes8Phong(const Lanes8 &normal, const Lanes8 &position, const float (&u)[8], const float (&v)[8],
                      const float (&lod)[8], const Material *const (&material)[8], const RenderFrameContext &frame,
                      SRMath::vec3 (&colors)[8]) noexcept
{
    bool sameMaterial = true;
    for (int i = 1; i < 8; ++i)
    {
        if (material[i] != material[0])
        {
            sameMaterial = false;
            break;
        }
    }

    SRMath::vec3 baseColor[8];
    for (int i = 0; i < 8; ++i)
    {
        const Material &m = *material[i];
        if (m.diffuseTexture)
        {
            baseColor[i] = sampleTexture(*m.diffuseTexture, u[i], v[i], lod[i], frame);
            baseColor[i] *= m.diffuse;
        }
        else
            baseColor[i] = m.diffuse;
    }

    const Lanes8 base = Transpose8(baseColor[0], baseColor[1], baseColor[2], baseColor[3],
                                   baseColor[4], baseColor[5], baseColor[6], baseColor[7]);
    const Lanes8 ambient = sameMaterial ? Broadcast8(material[0]->ambient)
                                        : Transpose8(material[0]->ambient, material[1]->ambient,
                                                     material[2]->ambient, material[3]->ambient,
                                                     material[4]->ambient, material[5]->ambient,
                                                     material[6]->ambient, material[7]->ambient);
    const Lanes8 specular = sameMaterial ? Broadcast8(material[0]->specular)
                                         : Transpose8(material[0]->specular, material[1]->specular,
                                                      material[2]->specular, material[3]->specular,
                                                      material[4]->specular, material[5]->specular,
                                                      material[6]->specular, material[7]->specular);
    float exponent[8];
    for (int i = 0; i < 8; ++i)
        exponent[i] = std::max(1.0f, material[i]->shininess);

    const Lanes8 camera = Broadcast8(frame.cameraPosition);
    const Lanes8 viewDir = Normalize8({_mm256_sub_ps(camera.x, position.x),
                                       _mm256_sub_ps(camera.y, position.y),
                                       _mm256_sub_ps(camera.z, position.z)});

    const __m256 zero = _mm256_setzero_ps();
    const __m256 one = _mm256_set1_ps(1.0f);
    Lanes8 totalDiffuse{zero, zero, zero};
    Lanes8 totalSpecular{zero, zero, zero};

    for (const auto &light : frame.lights)
    {
        const Lanes8 lightDir = Broadcast8(light.directionToLight);
        const Lanes8 lightColor = Broadcast8(light.color);

        const __m256 diffuseIntensity = _mm256_max_ps(Dot8(normal, lightDir), zero);
        __m256 diffX = _mm256_mul_ps(_mm256_mul_ps(base.x, diffuseIntensity), lightColor.x);
        __m256 diffY = _mm256_mul_ps(_mm256_mul_ps(base.y, diffuseIntensity), lightColor.y);
        __m256 diffZ = _mm256_mul_ps(_mm256_mul_ps(base.z, diffuseIntensity), lightColor.z);

        const Lanes8 incident = Broadcast8(-1.0f * light.directionToLight);
        const __m256 twice = _mm256_mul_ps(_mm256_set1_ps(2.0f), Dot8(normal, incident));
        const Lanes8 reflectDirection{_mm256_sub_ps(incident.x, _mm256_mul_ps(normal.x, twice)),
                                      _mm256_sub_ps(incident.y, _mm256_mul_ps(normal.y, twice)),
                                      _mm256_sub_ps(incident.z, _mm256_mul_ps(normal.z, twice))};
        const __m256 specularBase = _mm256_max_ps(Dot8(viewDir, reflectDirection), zero);
        const int needPow = _mm256_movemask_ps(
            _mm256_and_ps(_mm256_cmp_ps(diffuseIntensity, zero, _CMP_GT_OQ), _mm256_cmp_ps(specularBase, zero, _CMP_NEQ_OQ)));
        alignas(32) float specularFactor[8] = {};
        if (needPow)
        {
            alignas(32) float baseLane[8];
            _mm256_store_ps(baseLane, specularBase);
            for (int i = 0; i < 8; ++i)
                if (needPow & (1 << i))
                    specularFactor[i] = std::pow(baseLane[i], exponent[i]);
        }
        const __m256 factor = _mm256_load_ps(specularFactor);
        __m256 specX = _mm256_mul_ps(_mm256_mul_ps(specular.x, factor), lightColor.x);
        __m256 specY = _mm256_mul_ps(_mm256_mul_ps(specular.y, factor), lightColor.y);
        __m256 specZ = _mm256_mul_ps(_mm256_mul_ps(specular.z, factor), lightColor.z);

        if (frame.shadowMap != nullptr && frame.shadowMap->lightType == ShadowMap::LightType::Directional)
        {
            alignas(32) float px[8], py[8], pz[8], nx[8], ny[8], nz[8], vis[8];
            _mm256_store_ps(px, position.x); _mm256_store_ps(py, position.y); _mm256_store_ps(pz, position.z);
            _mm256_store_ps(nx, normal.x); _mm256_store_ps(ny, normal.y); _mm256_store_ps(nz, normal.z);
            for (int i = 0; i < 8; ++i)
                vis[i] = frame.shadowMap->SampleVisibility({px[i], py[i], pz[i]}, {nx[i], ny[i], nz[i]}, light.directionToLight);
            const __m256 shadowVis = _mm256_load_ps(vis);
            diffX = _mm256_mul_ps(diffX, shadowVis);
            diffY = _mm256_mul_ps(diffY, shadowVis);
            diffZ = _mm256_mul_ps(diffZ, shadowVis);
            specX = _mm256_mul_ps(specX, shadowVis);
            specY = _mm256_mul_ps(specY, shadowVis);
            specZ = _mm256_mul_ps(specZ, shadowVis);
        }

        totalDiffuse.x = _mm256_add_ps(totalDiffuse.x, diffX);
        totalDiffuse.y = _mm256_add_ps(totalDiffuse.y, diffY);
        totalDiffuse.z = _mm256_add_ps(totalDiffuse.z, diffZ);
        totalSpecular.x = _mm256_add_ps(totalSpecular.x, specX);
        totalSpecular.y = _mm256_add_ps(totalSpecular.y, specY);
        totalSpecular.z = _mm256_add_ps(totalSpecular.z, specZ);
    }

    if (!frame.pointLights.empty())
    {
        for (const auto &light : frame.pointLights)
        {
            const Lanes8 lightPos = Broadcast8(light.position);
            const Lanes8 toLight{_mm256_sub_ps(lightPos.x, position.x), _mm256_sub_ps(lightPos.y, position.y),
                                 _mm256_sub_ps(lightPos.z, position.z)};
            const __m256 distSq = Dot8(toLight, toLight);
            const __m256 dist = _mm256_sqrt_ps(distSq);
            const Lanes8 lightDir = Normalize8(toLight);

            const __m256 ratio = _mm256_mul_ps(dist, _mm256_set1_ps(light.invRange));
            const __m256 ratioSq = _mm256_mul_ps(ratio, ratio);
            const __m256 window = _mm256_max_ps(zero, _mm256_sub_ps(one, _mm256_mul_ps(ratioSq, ratioSq)));
            const __m256 denom = _mm256_add_ps(_mm256_add_ps(_mm256_set1_ps(light.constant),
                                                             _mm256_mul_ps(_mm256_set1_ps(light.linear), dist)),
                                              _mm256_mul_ps(_mm256_set1_ps(light.quadratic), distSq));
            const __m256 atten = _mm256_div_ps(_mm256_mul_ps(window, window), _mm256_max_ps(denom, _mm256_set1_ps(1e-4f)));
            const Lanes8 lightColor = Broadcast8(light.color);
            const Lanes8 attenuatedColor{_mm256_mul_ps(lightColor.x, atten), _mm256_mul_ps(lightColor.y, atten),
                                         _mm256_mul_ps(lightColor.z, atten)};

            const __m256 diffuseIntensity = _mm256_max_ps(Dot8(normal, lightDir), zero);
            __m256 diffX = _mm256_mul_ps(_mm256_mul_ps(base.x, diffuseIntensity), attenuatedColor.x);
            __m256 diffY = _mm256_mul_ps(_mm256_mul_ps(base.y, diffuseIntensity), attenuatedColor.y);
            __m256 diffZ = _mm256_mul_ps(_mm256_mul_ps(base.z, diffuseIntensity), attenuatedColor.z);

            const Lanes8 incident{_mm256_mul_ps(_mm256_set1_ps(-1.0f), lightDir.x),
                                  _mm256_mul_ps(_mm256_set1_ps(-1.0f), lightDir.y),
                                  _mm256_mul_ps(_mm256_set1_ps(-1.0f), lightDir.z)};
            const __m256 twice = _mm256_mul_ps(_mm256_set1_ps(2.0f), Dot8(normal, incident));
            const Lanes8 reflectDirection{_mm256_sub_ps(incident.x, _mm256_mul_ps(normal.x, twice)),
                                          _mm256_sub_ps(incident.y, _mm256_mul_ps(normal.y, twice)),
                                          _mm256_sub_ps(incident.z, _mm256_mul_ps(normal.z, twice))};
            const __m256 specularBase = _mm256_max_ps(Dot8(viewDir, reflectDirection), zero);
            const int needPow = _mm256_movemask_ps(
                _mm256_and_ps(_mm256_cmp_ps(diffuseIntensity, zero, _CMP_GT_OQ), _mm256_cmp_ps(specularBase, zero, _CMP_NEQ_OQ)));
            alignas(32) float specularFactor[8] = {};
            if (needPow)
            {
                alignas(32) float baseLane[8];
                _mm256_store_ps(baseLane, specularBase);
                for (int i = 0; i < 8; ++i)
                    if (needPow & (1 << i))
                        specularFactor[i] = std::pow(baseLane[i], exponent[i]);
            }
            const __m256 factor = _mm256_load_ps(specularFactor);
            __m256 specX = _mm256_mul_ps(_mm256_mul_ps(specular.x, factor), attenuatedColor.x);
            __m256 specY = _mm256_mul_ps(_mm256_mul_ps(specular.y, factor), attenuatedColor.y);
            __m256 specZ = _mm256_mul_ps(_mm256_mul_ps(specular.z, factor), attenuatedColor.z);

            totalDiffuse.x = _mm256_add_ps(totalDiffuse.x, diffX);
            totalDiffuse.y = _mm256_add_ps(totalDiffuse.y, diffY);
            totalDiffuse.z = _mm256_add_ps(totalDiffuse.z, diffZ);
            totalSpecular.x = _mm256_add_ps(totalSpecular.x, specX);
            totalSpecular.y = _mm256_add_ps(totalSpecular.y, specY);
            totalSpecular.z = _mm256_add_ps(totalSpecular.z, specZ);
        }
    }

    if (!frame.spotLights.empty())
    {
        for (const auto &light : frame.spotLights)
        {
            const Lanes8 lightPos = Broadcast8(light.position);
            const Lanes8 toLight{_mm256_sub_ps(lightPos.x, position.x), _mm256_sub_ps(lightPos.y, position.y),
                                 _mm256_sub_ps(lightPos.z, position.z)};
            const __m256 distSq = Dot8(toLight, toLight);
            const __m256 dist = _mm256_sqrt_ps(distSq);
            const Lanes8 lightDir = Normalize8(toLight);

            const __m256 ratio = _mm256_mul_ps(dist, _mm256_set1_ps(light.invRange));
            const __m256 ratioSq = _mm256_mul_ps(ratio, ratio);
            const __m256 window = _mm256_max_ps(zero, _mm256_sub_ps(one, _mm256_mul_ps(ratioSq, ratioSq)));
            const __m256 denom = _mm256_add_ps(_mm256_add_ps(_mm256_set1_ps(light.constant),
                                                             _mm256_mul_ps(_mm256_set1_ps(light.linear), dist)),
                                              _mm256_mul_ps(_mm256_set1_ps(light.quadratic), distSq));
            const __m256 distAtten = _mm256_div_ps(_mm256_mul_ps(window, window), _mm256_max_ps(denom, _mm256_set1_ps(1e-4f)));

            const __m256 cosTheta = Dot8(lightDir, Broadcast8(light.directionToLight));
            const __m256 rawT = _mm256_mul_ps(_mm256_sub_ps(cosTheta, _mm256_set1_ps(light.outerConeCos)),
                                              _mm256_set1_ps(light.invConeCosDiff));
            const __m256 spotT = _mm256_max_ps(zero, _mm256_min_ps(one, rawT));
            const __m256 spotFactor = _mm256_mul_ps(_mm256_mul_ps(spotT, spotT),
                                                    _mm256_sub_ps(_mm256_set1_ps(3.0f), _mm256_mul_ps(_mm256_set1_ps(2.0f), spotT)));
            const __m256 atten = _mm256_mul_ps(distAtten, spotFactor);
            const Lanes8 lightColor = Broadcast8(light.color);
            const Lanes8 attenuatedColor{_mm256_mul_ps(lightColor.x, atten), _mm256_mul_ps(lightColor.y, atten),
                                         _mm256_mul_ps(lightColor.z, atten)};

            const __m256 diffuseIntensity = _mm256_max_ps(Dot8(normal, lightDir), zero);
            __m256 diffX = _mm256_mul_ps(_mm256_mul_ps(base.x, diffuseIntensity), attenuatedColor.x);
            __m256 diffY = _mm256_mul_ps(_mm256_mul_ps(base.y, diffuseIntensity), attenuatedColor.y);
            __m256 diffZ = _mm256_mul_ps(_mm256_mul_ps(base.z, diffuseIntensity), attenuatedColor.z);

            const Lanes8 incident{_mm256_mul_ps(_mm256_set1_ps(-1.0f), lightDir.x),
                                  _mm256_mul_ps(_mm256_set1_ps(-1.0f), lightDir.y),
                                  _mm256_mul_ps(_mm256_set1_ps(-1.0f), lightDir.z)};
            const __m256 twice = _mm256_mul_ps(_mm256_set1_ps(2.0f), Dot8(normal, incident));
            const Lanes8 reflectDirection{_mm256_sub_ps(incident.x, _mm256_mul_ps(normal.x, twice)),
                                          _mm256_sub_ps(incident.y, _mm256_mul_ps(normal.y, twice)),
                                          _mm256_sub_ps(incident.z, _mm256_mul_ps(normal.z, twice))};
            const __m256 specularBase = _mm256_max_ps(Dot8(viewDir, reflectDirection), zero);
            const int needPow = _mm256_movemask_ps(
                _mm256_and_ps(_mm256_cmp_ps(diffuseIntensity, zero, _CMP_GT_OQ), _mm256_cmp_ps(specularBase, zero, _CMP_NEQ_OQ)));
            alignas(32) float specularFactor[8] = {};
            if (needPow)
            {
                alignas(32) float baseLane[8];
                _mm256_store_ps(baseLane, specularBase);
                for (int i = 0; i < 8; ++i)
                    if (needPow & (1 << i))
                        specularFactor[i] = std::pow(baseLane[i], exponent[i]);
            }
            const __m256 factor = _mm256_load_ps(specularFactor);
            __m256 specX = _mm256_mul_ps(_mm256_mul_ps(specular.x, factor), attenuatedColor.x);
            __m256 specY = _mm256_mul_ps(_mm256_mul_ps(specular.y, factor), attenuatedColor.y);
            __m256 specZ = _mm256_mul_ps(_mm256_mul_ps(specular.z, factor), attenuatedColor.z);

            if (frame.shadowMap != nullptr && frame.shadowMap->lightType == ShadowMap::LightType::Spot)
            {
                alignas(32) float px[8], py[8], pz[8], nx[8], ny[8], nz[8], lx[8], ly[8], lz[8], vis[8];
                _mm256_store_ps(px, position.x); _mm256_store_ps(py, position.y); _mm256_store_ps(pz, position.z);
                _mm256_store_ps(nx, normal.x); _mm256_store_ps(ny, normal.y); _mm256_store_ps(nz, normal.z);
                _mm256_store_ps(lx, lightDir.x); _mm256_store_ps(ly, lightDir.y); _mm256_store_ps(lz, lightDir.z);
                for (int i = 0; i < 8; ++i)
                    vis[i] = frame.shadowMap->SampleVisibility({px[i], py[i], pz[i]}, {nx[i], ny[i], nz[i]}, {lx[i], ly[i], lz[i]});
                const __m256 shadowVis = _mm256_load_ps(vis);
                diffX = _mm256_mul_ps(diffX, shadowVis);
                diffY = _mm256_mul_ps(diffY, shadowVis);
                diffZ = _mm256_mul_ps(diffZ, shadowVis);
                specX = _mm256_mul_ps(specX, shadowVis);
                specY = _mm256_mul_ps(specY, shadowVis);
                specZ = _mm256_mul_ps(specZ, shadowVis);
            }

            totalDiffuse.x = _mm256_add_ps(totalDiffuse.x, diffX);
            totalDiffuse.y = _mm256_add_ps(totalDiffuse.y, diffY);
            totalDiffuse.z = _mm256_add_ps(totalDiffuse.z, diffZ);
            totalSpecular.x = _mm256_add_ps(totalSpecular.x, specX);
            totalSpecular.y = _mm256_add_ps(totalSpecular.y, specY);
            totalSpecular.z = _mm256_add_ps(totalSpecular.z, specZ);
        }
    }

    Lanes8 phongColor{
        _mm256_add_ps(_mm256_add_ps(ambient.x, totalDiffuse.x), totalSpecular.x),
        _mm256_add_ps(_mm256_add_ps(ambient.y, totalDiffuse.y), totalSpecular.y),
        _mm256_add_ps(_mm256_add_ps(ambient.z, totalDiffuse.z), totalSpecular.z)
    };

    if (frame.tonemap)
    {
        phongColor = ACESFilm8(phongColor);
    }
    else
    {
        phongColor.x = _mm256_max_ps(zero, _mm256_min_ps(one, phongColor.x));
        phongColor.y = _mm256_max_ps(zero, _mm256_min_ps(one, phongColor.y));
        phongColor.z = _mm256_max_ps(zero, _mm256_min_ps(one, phongColor.z));
    }

    Unpack8(phongColor, colors);

    if (frame.srgbGamma)
    {
        constexpr float invGamma = 1.0f / 2.2f;
        for (int i = 0; i < 8; ++i)
        {
            colors[i].x = std::pow(colors[i].x, invGamma);
            colors[i].y = std::pow(colors[i].y, invGamma);
            colors[i].z = std::pow(colors[i].z, invGamma);
        }
    }
}

void ShadeLanes8(const Lanes8 &normal, const Lanes8 &position, const float (&u)[8], const float (&v)[8],
                 const float (&lod)[8], const Material *const (&material)[8], const RenderFrameContext &frame,
                 SRMath::vec3 (&colors)[8]) noexcept
{
    bool anyPbr = false;
    bool allPbr = true;
    for (int i = 0; i < 8; ++i)
    {
        if (material[i]->isPbr)
            anyPbr = true;
        else
            allPbr = false;
    }

    if (frame.shadingModel == EShadingModel::Auto && frame.pbr && anyPbr != allPbr)
    {
        alignas(32) float px[8], py[8], pz[8];
        alignas(32) float nx[8], ny[8], nz[8];
        _mm256_store_ps(px, position.x);
        _mm256_store_ps(py, position.y);
        _mm256_store_ps(pz, position.z);
        _mm256_store_ps(nx, normal.x);
        _mm256_store_ps(ny, normal.y);
        _mm256_store_ps(nz, normal.z);
        for (int i = 0; i < 8; ++i)
        {
            Rasterizer::SurfaceSample s;
            s.positionWorld = SRMath::vec3(px[i], py[i], pz[i]);
            s.normalWorld = SRMath::vec3(nx[i], ny[i], nz[i]);
            s.texcoord = SRMath::vec2(u[i], v[i]);
            s.lod = lod[i];
            colors[i] = Rasterizer::ShadeSurface(s, *material[i], frame);
        }
        return;
    }

    bool runPbr = false;
    if (frame.shadingModel == EShadingModel::Phong)
    {
        runPbr = false;
    }
    else if (frame.shadingModel == EShadingModel::PBR)
    {
        runPbr = frame.pbr;
    }
    else
    {
        runPbr = frame.pbr && allPbr;
    }

    if (runPbr)
    {
        ShadeLanes8PBR(normal, position, u, v, lod, material, frame, colors);
    }
    else
    {
        ShadeLanes8Phong(normal, position, u, v, lod, material, frame, colors);
    }
}

void ShadeSurface8(const Rasterizer::SurfaceSample (&surface)[8],
                   const Material *const (&material)[8],
                   const RenderFrameContext &frame,
                   SRMath::vec3 (&colors)[8]) noexcept
{
    float u[8], v[8], lod[8];
    SRMath::vec3 normal[8];
    for (int i = 0; i < 8; ++i)
    {
        u[i] = surface[i].texcoord.x;
        v[i] = surface[i].texcoord.y;
        lod[i] = surface[i].lod;
        normal[i] = material[i]->normalTexture
                        ? perturbNormal(surface[i].normalWorld, surface[i].tangentWorld,
                                        surface[i].bitangentWorld, u[i], v[i], lod[i], *material[i], frame)
                        : surface[i].normalWorld;
    }
    ShadeLanes8(Transpose8(normal[0], normal[1], normal[2], normal[3],
                           normal[4], normal[5], normal[6], normal[7]),
                Transpose8(surface[0].positionWorld, surface[1].positionWorld, surface[2].positionWorld, surface[3].positionWorld,
                           surface[4].positionWorld, surface[5].positionWorld, surface[6].positionWorld, surface[7].positionWorld),
                u, v, lod, material, frame, colors);
}

void ShadeOwner8(const PreparedTriangle *const (&triangle)[8],
                 const Rasterizer::OwnerSample *const (&sample)[8], const RenderFrameContext &frame,
                 SRMath::vec3 (&colors)[8]) noexcept
{
    const auto blend = [](__m128 a0, __m128 a1, __m128 a2, __m128 a, __m128 b, __m128 c, __m128 r)
    {
        __m128 value = _mm_mul_ps(a0, a);
        value = _mm_add_ps(value, _mm_mul_ps(a1, b));
        value = _mm_add_ps(value, _mm_mul_ps(a2, c));
        return _mm_mul_ps(value, r);
    };

    SRMath::vec3 normal[8], position[8];
    float u[8] = {}, v[8] = {}, lod[8] = {};
    const Material *material[8] = {};
    for (int i = 0; i < 8; ++i)
    {
        const PreparedTriangle &t = *triangle[i];
        const Rasterizer::OwnerSample &s = *sample[i];
        const __m128 a = _mm_set1_ps(s.a), b = _mm_set1_ps(s.b), c = _mm_set1_ps(s.c);
        const __m128 r = _mm_set1_ps(1.f / s.q);
        normal[i].m128 = blend(t.rv0.normalWorldOverW.m128, t.rv1.normalWorldOverW.m128,
                               t.rv2.normalWorldOverW.m128, a, b, c, r);
        position[i].m128 =
            blend(t.rv0.worldPosOverW.m128, t.rv1.worldPosOverW.m128, t.rv2.worldPosOverW.m128, a, b, c, r);
        const __m128 uv =
            blend(t.rv0.texcoordOverW.m128, t.rv1.texcoordOverW.m128, t.rv2.texcoordOverW.m128, a, b, c, r);
        u[i] = _mm_cvtss_f32(uv);
        v[i] = _mm_cvtss_f32(_mm_shuffle_ps(uv, uv, _MM_SHUFFLE(1, 1, 1, 1)));
        material[i] = t.sourceCommand->material;
    }

    if (frame.mipmapping)
    {
        for (int i = 0; i < 8; ++i)
            lod[i] = Rasterizer::ComputeTriangleLod(*triangle[i]);
    }

    Lanes8 normalLanes = Normalize8(Transpose8(normal[0], normal[1], normal[2], normal[3],
                                              normal[4], normal[5], normal[6], normal[7]));
    bool anyNormalTex = false;
    for (int i = 0; i < 8; ++i)
    {
        if (material[i]->normalTexture)
        {
            anyNormalTex = true;
            break;
        }
    }
    if (anyNormalTex)
    {
        for (int i = 0; i < 8; ++i)
        {
            normal[i] = SRMath::normalize(normal[i]);
            if (material[i]->normalTexture)
            {
                SRMath::vec3 tangentWorld, bitangentWorld;
                Rasterizer::ComputeTriangleTangentFrame(*triangle[i], tangentWorld, bitangentWorld);
                normal[i] = perturbNormal(normal[i], tangentWorld, bitangentWorld, u[i], v[i], lod[i],
                                          *material[i], frame);
            }
        }
        normalLanes = Transpose8(normal[0], normal[1], normal[2], normal[3],
                                 normal[4], normal[5], normal[6], normal[7]);
    }

    ShadeLanes8(normalLanes, Transpose8(position[0], position[1], position[2], position[3],
                                        position[4], position[5], position[6], position[7]),
                u, v, lod, material, frame, colors);
}

} // namespace sr::renderer::avx2

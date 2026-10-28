#include "Renderer/Rasterizer.h"
#include "Graphics/Material.h"
#include "Graphics/Mesh.h"
#include "Graphics/Texture.h"
#include "Graphics/Light.h"
#include "Platform/Compiler.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/Tile.h"
#include "Renderer/ShadowMap.h"
#include "Graphics/IBL.h" 
#include <algorithm>
#include <cmath>
#include <smmintrin.h>

PreparedDirectionalLight PreparedDirectionalLight::From(const DirectionalLight &light) noexcept
{
    // 광선 진행 방향을 표면→광원 방향으로 바꾼다. normalize의 짧은 벡터/NaN 처리와
    // 연산 순서를 바꾸지 않아 정규화 전 입력을 받던 기존 셰이더와 같은 값을 전달한다.
    // 준비는 호출 스레드에서 실행된다. 픽셀 값과 달리 worker별 FP 예외 상태나
    // 예외 mask를 해제한 환경의 trap 시점/스레드까지 보존하는 API는 아니다.
    return {SRMath::normalize(-1.0f * light.rayDirection), light.color};
}

PreparedPointLight PreparedPointLight::From(const PointLight &light) noexcept
{
    const float r = std::max(light.range, 1e-4f);
    return {light.position, light.color, 1.0f / r, light.constant, light.linear, light.quadratic};
}

PreparedSpotLight PreparedSpotLight::From(const SpotLight &light) noexcept
{
    const float r = std::max(light.range, 1e-4f);
    const float innerCos = light.innerConeCos;
    const float outerCos = std::min(light.outerConeCos, innerCos - 1e-4f);
    return {light.position,
            SRMath::normalize(-1.0f * light.rayDirection),
            light.color,
            1.0f / r,
            innerCos,
            outerCos,
            1.0f / (innerCos - outerCos),
            light.constant,
            light.linear,
            light.quadratic};
}

namespace
{
} // namespace

SRMath::Color4 Rasterizer::SampleTexture(const Texture &tex, float u, float v, float lod,
                                         const RenderFrameContext &frame)
{
    if (!frame.mipmapping && !frame.bilinearFiltering && tex.GetFilter() == TextureFilter::Nearest)
        return tex.SampleRGBA(u, v);
    if (frame.mipmapping && tex.HasMipmaps())
    {
        if (frame.bilinearFiltering || tex.GetFilter() != TextureFilter::Nearest)
        {
            const float clampedLod = std::clamp(lod, 0.f, static_cast<float>(tex.GetMipCount() - 1));
            const int mip0 = static_cast<int>(std::floor(clampedLod));
            const int mip1 = std::min(mip0 + 1, tex.GetMipCount() - 1);
            const float frac = clampedLod - static_cast<float>(mip0);
            if (mip0 == mip1 || frac <= 0.f)
                return tex.SampleBilinearRGBA(u, v, mip0);
            const SRMath::Color4 c0 = tex.SampleBilinearRGBA(u, v, mip0);
            const SRMath::Color4 c1 = tex.SampleBilinearRGBA(u, v, mip1);
            return c0 + (c1 - c0) * frac;
        }
        const int mip = std::clamp(static_cast<int>(std::round(lod)), 0, tex.GetMipCount() - 1);
        return tex.SampleNearestRGBA(u, v, mip);
    }
    if (frame.bilinearFiltering)
        return tex.SampleBilinearRGBA(u, v, 0);
    return tex.SampleRGBA(u, v, lod);
}

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
} // namespace

float Rasterizer::ComputeTriangleLod(const PreparedTriangle &t) noexcept
{
    if (!t.sourceCommand || !t.sourceCommand->material)
        return 0.f;
    const Material &mat = *t.sourceCommand->material;
    const Texture *tex = mat.diffuseTexture ? mat.diffuseTexture.get() : mat.normalTexture.get();
    if (!tex || !tex->HasMipmaps())
        return 0.f;
    const float dx1 = t.rv1.screenPos.x - t.rv0.screenPos.x, dy1 = t.rv1.screenPos.y - t.rv0.screenPos.y;
    const float dx2 = t.rv2.screenPos.x - t.rv0.screenPos.x, dy2 = t.rv2.screenPos.y - t.rv0.screenPos.y;
    const float area2 = dx1 * dy2 - dx2 * dy1;
    if (!(std::abs(area2) > 1e-6f))
        return 0.f;
    const SRMath::vec2 uv0 = t.rv0.texcoordOverW * (1.f / t.rv0.oneOverW);
    const SRMath::vec2 uv1 = t.rv1.texcoordOverW * (1.f / t.rv1.oneOverW);
    const SRMath::vec2 uv2 = t.rv2.texcoordOverW * (1.f / t.rv2.oneOverW);
    const float du1 = uv1.x - uv0.x, dv1 = uv1.y - uv0.y;
    const float du2 = uv2.x - uv0.x, dv2 = uv2.y - uv0.y;
    const float invArea = 1.f / area2;
    const float w = static_cast<float>(tex->GetWidth());
    const float h = static_cast<float>(tex->GetHeight());
    const float dudx = (du1 * dy2 - du2 * dy1) * invArea * w;
    const float dvdx = (dv1 * dy2 - dv2 * dy1) * invArea * h;
    const float dudy = (dx1 * du2 - dx2 * du1) * invArea * w;
    const float dvdy = (dx1 * dv2 - dx2 * dv1) * invArea * h;
    const float rhoSq = std::max(dudx * dudx + dvdx * dvdx, dudy * dudy + dvdy * dvdy);
    return rhoSq > 1.f ? 0.5f * std::log2(rhoSq) : 0.f;
}

void Rasterizer::ComputeTriangleTangentFrame(const PreparedTriangle &t, SRMath::vec3 &tangentWorld,
                                             SRMath::vec3 &bitangentWorld) noexcept
{
    const SRMath::vec3 w0 = t.rv0.worldPosOverW * (1.f / t.rv0.oneOverW);
    const SRMath::vec3 w1 = t.rv1.worldPosOverW * (1.f / t.rv1.oneOverW);
    const SRMath::vec3 w2 = t.rv2.worldPosOverW * (1.f / t.rv2.oneOverW);
    const SRMath::vec2 uv0 = t.rv0.texcoordOverW * (1.f / t.rv0.oneOverW);
    const SRMath::vec2 uv1 = t.rv1.texcoordOverW * (1.f / t.rv1.oneOverW);
    const SRMath::vec2 uv2 = t.rv2.texcoordOverW * (1.f / t.rv2.oneOverW);
    const SRMath::vec3 dp1 = w1 - w0;
    const SRMath::vec3 dp2 = w2 - w0;
    const float du1 = uv1.x - uv0.x;
    const float dv1 = uv1.y - uv0.y;
    const float du2 = uv2.x - uv0.x;
    const float dv2 = uv2.y - uv0.y;
    const float det = du1 * dv2 - du2 * dv1;
    if (std::abs(det) > 1e-8f)
    {
        const float r = 1.f / det;
        tangentWorld = (dp1 * dv2 - dp2 * dv1) * r;
        bitangentWorld = (dp2 * du1 - dp1 * du2) * r;
    }
    else if (t.sourceCommand && t.sourceCommand->sourceMesh &&
             !t.sourceCommand->sourceMesh->GetTangents().empty())
    {
        const SRMath::vec4 tan0 = t.sourceCommand->sourceMesh->GetTangents()[0];
        const SRMath::vec3 n = SRMath::normalize(t.rv0.normalWorldOverW * (1.f / t.rv0.oneOverW));
        tangentWorld = SRMath::normalize(
            SRMath::vec3(t.sourceCommand->worldTransform * SRMath::vec4(tan0.x, tan0.y, tan0.z, 0.f)));
        bitangentWorld = SRMath::normalize(SRMath::cross(n, tangentWorld) * (tan0.w < 0.f ? -1.f : 1.f));
    }
    else
    {
        const SRMath::vec3 n = SRMath::normalize(t.rv0.normalWorldOverW * (1.f / t.rv0.oneOverW));
        const SRMath::vec3 axis =
            std::abs(n.x) < 0.9f ? SRMath::vec3(1.f, 0.f, 0.f) : SRMath::vec3(0.f, 1.f, 0.f);
        tangentWorld = SRMath::normalize(SRMath::cross(n, axis));
        bitangentWorld = SRMath::normalize(SRMath::cross(n, tangentWorld));
    }
}

// 스크린 공간 barycentric weight로 attr/w를 합한 뒤 한 번만 w를 복원한다.
Rasterizer::SurfaceSample Rasterizer::InterpolateSurface(const RasterizerVertex &v0,
                                                         const RasterizerVertex &v1,
                                                         const RasterizerVertex &v2, float wBary, float uBary,
                                                         float vBary, float reciprocalInterpolatedInverseW)
{
    SRMath::vec3 normalInterpolated = v0.normalWorldOverW * wBary;
    normalInterpolated += v1.normalWorldOverW * uBary;
    normalInterpolated += v2.normalWorldOverW * vBary;
    normalInterpolated *= reciprocalInterpolatedInverseW;
    normalInterpolated = SRMath::normalize(normalInterpolated);

    SRMath::vec2 uvOverWInterpolated = v0.texcoordOverW * wBary;
    uvOverWInterpolated += v1.texcoordOverW * uBary;
    uvOverWInterpolated += v2.texcoordOverW * vBary;

    SRMath::vec2 uvInterpolated = uvOverWInterpolated * reciprocalInterpolatedInverseW;

    SRMath::vec3 interpolatedWorldPos = v0.worldPosOverW * wBary;
    interpolatedWorldPos += v1.worldPosOverW * uBary;
    interpolatedWorldPos += v2.worldPosOverW * vBary;
    interpolatedWorldPos *= reciprocalInterpolatedInverseW;

    return {normalInterpolated, uvInterpolated, interpolatedWorldPos};
}

// 법선, 카메라, 준비된 광원 방향은 모두 월드 공간이다. 광원은 worker 시작 전에 준비된다.

constexpr float pbrPi = 3.14159265358979323846f;

inline float sRGBToLinear(float c) noexcept
{
    c = std::clamp(c, 0.0f, 1.0f);
    return (c <= 0.04045f) ? (c / 12.92f) : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

inline float GGX_D(float NdotH, float alpha2) noexcept
{
    const float d = (NdotH * NdotH) * (alpha2 - 1.0f) + 1.0f;
    return alpha2 / (pbrPi * d * d);
}

inline float SmithGGX_V(float NdotL, float NdotV, float alpha2) noexcept
{
    const float denomL = NdotV * std::sqrt((NdotL * NdotL) * (1.0f - alpha2) + alpha2);
    const float denomV = NdotL * std::sqrt((NdotV * NdotV) * (1.0f - alpha2) + alpha2);
    return 0.5f / std::max(denomL + denomV, 1e-7f);
}

inline SRMath::vec3 Schlick_F(float VdotH, const SRMath::vec3 &F0) noexcept
{
    const float f = 1.0f - VdotH;
    const float f2 = f * f;
    const float f5 = f2 * f2 * f;
    return F0 + (SRMath::vec3(1.0f, 1.0f, 1.0f) - F0) * f5;
}

SRMath::vec3 ShadeSurfacePBR(const Rasterizer::SurfaceSample &surface, const Material &material,
                             const RenderFrameContext &frame)
{
    SRMath::vec3 baseColor{material.baseColor.x, material.baseColor.y, material.baseColor.z};
    float roughness = material.roughness;
    float metallic = material.metallic;
    if (!material.isPbr)
    {
        baseColor = material.diffuse;
        metallic = 0.0f;
        roughness = std::clamp(std::sqrt(2.0f / (material.shininess + 2.0f)), 0.04f, 1.0f);
    }
    if (material.diffuseTexture)
    {
        const auto diff = Rasterizer::SampleTexture(*material.diffuseTexture, surface.texcoord.x,
                                                    surface.texcoord.y, surface.lod, frame);
        baseColor.x *= sRGBToLinear(diff.r);
        baseColor.y *= sRGBToLinear(diff.g);
        baseColor.z *= sRGBToLinear(diff.b);
    }
    if (material.metallicRoughnessTexture)
    {
        const auto mr = Rasterizer::SampleTexture(*material.metallicRoughnessTexture, surface.texcoord.x,
                                                  surface.texcoord.y, surface.lod, frame);
        roughness *= mr.g;
        metallic *= mr.b;
    }
    roughness = std::clamp(roughness, 0.04f, 1.0f);
    metallic = std::clamp(metallic, 0.0f, 1.0f);

    float occlusion = 1.0f;
    if (material.occlusionTexture)
    {
        const auto occ = Rasterizer::SampleTexture(*material.occlusionTexture, surface.texcoord.x,
                                                   surface.texcoord.y, surface.lod, frame);
        occlusion = std::clamp(1.0f + material.occlusionStrength * (occ.r - 1.0f), 0.0f, 1.0f);
    }

    SRMath::vec3 emissive = material.emissive;
    if (material.emissiveTexture)
    {
        const auto em = Rasterizer::SampleTexture(*material.emissiveTexture, surface.texcoord.x,
                                                  surface.texcoord.y, surface.lod, frame);
        emissive.x *= sRGBToLinear(em.r);
        emissive.y *= sRGBToLinear(em.g);
        emissive.z *= sRGBToLinear(em.b);
    }

    const SRMath::vec3 surfaceNormal =
        material.normalTexture
            ? perturbNormal(surface.normalWorld, surface.tangentWorld, surface.bitangentWorld,
                            surface.texcoord.x, surface.texcoord.y, surface.lod, material, frame)
            : surface.normalWorld;

    const float alpha = roughness * roughness;
    const float alpha2 = alpha * alpha;

    const SRMath::vec3 F0 = SRMath::vec3(0.04f, 0.04f, 0.04f) * (1.0f - metallic) + baseColor * metallic;

    const SRMath::vec3 viewDir = SRMath::normalize(frame.cameraPosition - surface.positionWorld);
    const float NdotV = std::clamp(SRMath::dot(surfaceNormal, viewDir), 1e-4f, 1.0f);

    SRMath::vec3 totalDirect{};

    for (const auto &light : frame.lights)
    {
        const SRMath::vec3 &lightDir = light.directionToLight;
        const float NdotL = std::max(0.0f, SRMath::dot(surfaceNormal, lightDir));
        if (NdotL <= 0.0f)
            continue;

        const SRMath::vec3 H = SRMath::normalize(lightDir + viewDir);
        const float NdotH = std::clamp(SRMath::dot(surfaceNormal, H), 0.0f, 1.0f);
        const float VdotH = std::clamp(SRMath::dot(viewDir, H), 0.0f, 1.0f);

        const float D = GGX_D(NdotH, alpha2);
        const float V = SmithGGX_V(NdotL, NdotV, alpha2);
        const SRMath::vec3 F = Schlick_F(VdotH, F0);

        const SRMath::vec3 specular = F * (pbrPi * D * V);
        const SRMath::vec3 kD = (SRMath::vec3(1.0f, 1.0f, 1.0f) - F) * (1.0f - metallic);
        const SRMath::vec3 diffuse = kD * baseColor;

        SRMath::vec3 lightContrib = (diffuse + specular) * light.color * NdotL;

        if (frame.shadowMap != nullptr && frame.shadowMap->lightType == ShadowMap::LightType::Directional)
        {
            const float shadowVis =
                frame.shadowMap->SampleVisibility(surface.positionWorld, surfaceNormal, lightDir);
            lightContrib *= shadowVis;
        }

        totalDirect += lightContrib;
    }

    if (!frame.pointLights.empty())
    {
        for (const auto &light : frame.pointLights)
        {
            const SRMath::vec3 toLight = light.position - surface.positionWorld;
            const float distSq = SRMath::dot(toLight, toLight);
            const float dist = std::sqrt(distSq);
            const SRMath::vec3 lightDir = SRMath::normalize(toLight);

            const float ratio = dist * light.invRange;
            const float ratioSq = ratio * ratio;
            const float window = std::max(0.0f, 1.0f - ratioSq * ratioSq);
            const float denom = light.constant + light.linear * dist + light.quadratic * distSq;
            const float atten = (window * window) / std::max(denom, 1e-4f);
            if (atten <= 0.0f)
                continue;

            const float NdotL = std::max(0.0f, SRMath::dot(surfaceNormal, lightDir));
            if (NdotL <= 0.0f)
                continue;

            const SRMath::vec3 H = SRMath::normalize(lightDir + viewDir);
            const float NdotH = std::clamp(SRMath::dot(surfaceNormal, H), 0.0f, 1.0f);
            const float VdotH = std::clamp(SRMath::dot(viewDir, H), 0.0f, 1.0f);

            const float D = GGX_D(NdotH, alpha2);
            const float V = SmithGGX_V(NdotL, NdotV, alpha2);
            const SRMath::vec3 F = Schlick_F(VdotH, F0);

            const SRMath::vec3 specular = F * (pbrPi * D * V);
            const SRMath::vec3 kD = (SRMath::vec3(1.0f, 1.0f, 1.0f) - F) * (1.0f - metallic);
            const SRMath::vec3 diffuse = kD * baseColor;

            totalDirect += (diffuse + specular) * (light.color * atten) * NdotL;
        }
    }

    if (!frame.spotLights.empty())
    {
        for (const auto &light : frame.spotLights)
        {
            const SRMath::vec3 toLight = light.position - surface.positionWorld;
            const float distSq = SRMath::dot(toLight, toLight);
            const float dist = std::sqrt(distSq);
            const SRMath::vec3 lightDir = SRMath::normalize(toLight);

            const float ratio = dist * light.invRange;
            const float ratioSq = ratio * ratio;
            const float window = std::max(0.0f, 1.0f - ratioSq * ratioSq);
            const float denom = light.constant + light.linear * dist + light.quadratic * distSq;
            const float distAtten = (window * window) / std::max(denom, 1e-4f);

            const float cosTheta = SRMath::dot(lightDir, light.directionToLight);
            const float rawT = (cosTheta - light.outerConeCos) * light.invConeCosDiff;
            const float spotT = std::max(0.0f, std::min(1.0f, rawT));
            const float spotFactor = (spotT * spotT) * (3.0f - 2.0f * spotT);
            const float atten = distAtten * spotFactor;
            if (atten <= 0.0f)
                continue;

            const float NdotL = std::max(0.0f, SRMath::dot(surfaceNormal, lightDir));
            if (NdotL <= 0.0f)
                continue;

            const SRMath::vec3 H = SRMath::normalize(lightDir + viewDir);
            const float NdotH = std::clamp(SRMath::dot(surfaceNormal, H), 0.0f, 1.0f);
            const float VdotH = std::clamp(SRMath::dot(viewDir, H), 0.0f, 1.0f);

            const float D = GGX_D(NdotH, alpha2);
            const float V = SmithGGX_V(NdotL, NdotV, alpha2);
            const SRMath::vec3 F = Schlick_F(VdotH, F0);

            const SRMath::vec3 specular = F * (pbrPi * D * V);
            const SRMath::vec3 kD = (SRMath::vec3(1.0f, 1.0f, 1.0f) - F) * (1.0f - metallic);
            const SRMath::vec3 diffuse = kD * baseColor;

            SRMath::vec3 lightContrib = (diffuse + specular) * (light.color * atten) * NdotL;

            if (frame.shadowMap != nullptr && frame.shadowMap->lightType == ShadowMap::LightType::Spot)
            {
                const float shadowVis =
                    frame.shadowMap->SampleVisibility(surface.positionWorld, surfaceNormal, lightDir);
                lightContrib *= shadowVis;
            }

            totalDirect += lightContrib;
        }
    }

    SRMath::vec3 ambient = baseColor * material.ambient * occlusion;
    if (frame.ibl && frame.iblMap != nullptr)
    {
        const auto *ibl = static_cast<const sr::IBLMap *>(frame.iblMap);

        // 1. Diffuse IBL
        const SRMath::vec3 irradiance = ibl->sh.Evaluate(surfaceNormal);
        const float oneMinusNdotV = 1.0f - NdotV;
        const float f2 = oneMinusNdotV * oneMinusNdotV;
        const float f5 = f2 * f2 * oneMinusNdotV;
        const SRMath::vec3 maxF{std::max(1.0f - roughness, F0.x),
                                std::max(1.0f - roughness, F0.y),
                                std::max(1.0f - roughness, F0.z)};
        const SRMath::vec3 F_rough = F0 + (maxF - F0) * f5;
        const SRMath::vec3 kD_ibl = (SRMath::vec3(1.0f, 1.0f, 1.0f) - F_rough) * (1.0f - metallic);
        const SRMath::vec3 diffuseIBL = kD_ibl * baseColor * irradiance;

        // 2. Specular IBL
        const SRMath::vec3 R = SRMath::normalize(surfaceNormal * (2.0f * SRMath::dot(surfaceNormal, viewDir)) - viewDir);
        const SRMath::vec3 prefilteredColor = ibl->SampleSpecular(R, roughness);
        const SRMath::vec2 envBRDF = sr::EnvBRDFApprox(NdotV, roughness);
        const SRMath::vec3 specularIBL =
            prefilteredColor * (F0 * envBRDF.x + SRMath::vec3(envBRDF.y, envBRDF.y, envBRDF.y));

        ambient = (diffuseIBL + specularIBL) * occlusion;
    }

    SRMath::vec3 color = ambient + totalDirect + emissive;

    if (frame.tonemap)
    {
        color = sr::ACESFilm(color);
    }
    else
    {
        color.clamp(0.0f, 1.0f);
    }

    if (frame.srgbGamma)
    {
        constexpr float invGamma = 1.0f / 2.2f;
        color.x = std::pow(color.x, invGamma);
        color.y = std::pow(color.y, invGamma);
        color.z = std::pow(color.z, invGamma);
    }

    return color;
}

SRMath::vec3 Rasterizer::ShadeSurface(const SurfaceSample &surface, const Material &material,
                                      const RenderFrameContext &frame)
{
    const bool usePbr = (frame.shadingModel == EShadingModel::Phong)
                            ? false
                            : ((frame.shadingModel == EShadingModel::PBR) ? frame.pbr
                                                                          : (frame.pbr && material.isPbr));
    if (usePbr)
        return ShadeSurfacePBR(surface, material, frame);
    SRMath::vec3 baseColor;

    if (material.diffuseTexture)
    {
        baseColor =
            sampleTexture(*material.diffuseTexture, surface.texcoord.x, surface.texcoord.y, surface.lod, frame);
        baseColor *= material.diffuse;
    }
    else
    {
        baseColor = material.diffuse;
    }

    const SRMath::vec3 surfaceNormal =
        material.normalTexture
            ? perturbNormal(surface.normalWorld, surface.tangentWorld, surface.bitangentWorld,
                            surface.texcoord.x, surface.texcoord.y, surface.lod, material, frame)
            : surface.normalWorld;

    // 주변광 조명 계산
    SRMath::vec3 ambientColor = material.ambient;

    SRMath::vec3 totalDiffuseColor{};
    SRMath::vec3 totalSpecularColor{};

    SRMath::vec3 viewDir = SRMath::normalize(frame.cameraPosition - surface.positionWorld);

    for (const auto &light : frame.lights)
    {
        // 같은 광원을 모든 픽셀에서 다시 정규화하지 않는다. 반사/Phong 연산은 원래 순서다.
        const SRMath::vec3 &lightDir = light.directionToLight;

        // 난반사 조명 계산
        const float diffuseIntensity = std::max(0.0f, dot(surfaceNormal, lightDir));
        SRMath::vec3 diffuseTerm = baseColor;
        diffuseTerm *= diffuseIntensity;
        diffuseTerm *= light.color;

        // 정반사 조명 계산
        const SRMath::vec3 reflectDirection = SRMath::reflect(-1.0f * lightDir, surfaceNormal);
        // 반사광이 시선 반대쪽이면 밑이 +0이다. 지수는 max(1, shininess)로 1 이상(또는 +inf)이므로
        // pow(+0, y)는 정확히 +0이다. 그 경우 ucrtbase powf 호출(VTune 2위 핫스팟)을 건너뛴다.
        const float specularBase = std::max(0.0f, SRMath::dot(viewDir, reflectDirection));
        const float specularFactor = diffuseIntensity > 0.0f && specularBase != 0.0f
                                         ? std::pow(specularBase, std::max(1.0f, material.shininess))
                                         : 0.0f;

        SRMath::vec3 specularTerm = material.specular;
        specularTerm *= specularFactor;
        specularTerm *= light.color;

        if (frame.shadowMap != nullptr && frame.shadowMap->lightType == ShadowMap::LightType::Directional)
        {
            const float shadowVis =
                frame.shadowMap->SampleVisibility(surface.positionWorld, surfaceNormal, lightDir);
            diffuseTerm *= shadowVis;
            specularTerm *= shadowVis;
        }

        totalDiffuseColor += diffuseTerm;
        totalSpecularColor += specularTerm;
    }

    if (!frame.pointLights.empty())
    {
        for (const auto &light : frame.pointLights)
        {
            const SRMath::vec3 toLight = light.position - surface.positionWorld;
            const float distSq = SRMath::dot(toLight, toLight);
            const float dist = std::sqrt(distSq);
            const SRMath::vec3 lightDir = SRMath::normalize(toLight);

            const float ratio = dist * light.invRange;
            const float ratioSq = ratio * ratio;
            const float window = std::max(0.0f, 1.0f - ratioSq * ratioSq);
            const float denom = light.constant + light.linear * dist + light.quadratic * distSq;
            const float atten = (window * window) / std::max(denom, 1e-4f);
            const SRMath::vec3 attenuatedColor = light.color * atten;

            const float diffuseIntensity = std::max(0.0f, dot(surfaceNormal, lightDir));
            SRMath::vec3 diffuseTerm = baseColor;
            diffuseTerm *= diffuseIntensity;
            diffuseTerm *= attenuatedColor;
            totalDiffuseColor += diffuseTerm;

            const SRMath::vec3 reflectDirection = SRMath::reflect(-1.0f * lightDir, surfaceNormal);
            const float specularBase = std::max(0.0f, SRMath::dot(viewDir, reflectDirection));
            const float specularFactor = diffuseIntensity > 0.0f && specularBase != 0.0f
                                             ? std::pow(specularBase, std::max(1.0f, material.shininess))
                                             : 0.0f;

            SRMath::vec3 specularTerm = material.specular;
            specularTerm *= specularFactor;
            specularTerm *= attenuatedColor;
            totalSpecularColor += specularTerm;
        }
    }

    if (!frame.spotLights.empty())
    {
        for (const auto &light : frame.spotLights)
        {
            const SRMath::vec3 toLight = light.position - surface.positionWorld;
            const float distSq = SRMath::dot(toLight, toLight);
            const float dist = std::sqrt(distSq);
            const SRMath::vec3 lightDir = SRMath::normalize(toLight);

            const float ratio = dist * light.invRange;
            const float ratioSq = ratio * ratio;
            const float window = std::max(0.0f, 1.0f - ratioSq * ratioSq);
            const float denom = light.constant + light.linear * dist + light.quadratic * distSq;
            const float distAtten = (window * window) / std::max(denom, 1e-4f);

            const float cosTheta = SRMath::dot(lightDir, light.directionToLight);
            const float rawT = (cosTheta - light.outerConeCos) * light.invConeCosDiff;
            const float spotT = std::max(0.0f, std::min(1.0f, rawT));
            const float spotFactor = (spotT * spotT) * (3.0f - 2.0f * spotT);
            const float atten = distAtten * spotFactor;
            const SRMath::vec3 attenuatedColor = light.color * atten;

            const float diffuseIntensity = std::max(0.0f, dot(surfaceNormal, lightDir));
            SRMath::vec3 diffuseTerm = baseColor;
            diffuseTerm *= diffuseIntensity;
            diffuseTerm *= attenuatedColor;
            totalDiffuseColor += diffuseTerm;

            const SRMath::vec3 reflectDirection = SRMath::reflect(-1.0f * lightDir, surfaceNormal);
            const float specularBase = std::max(0.0f, SRMath::dot(viewDir, reflectDirection));
            const float specularFactor = diffuseIntensity > 0.0f && specularBase != 0.0f
                                             ? std::pow(specularBase, std::max(1.0f, material.shininess))
                                             : 0.0f;

            SRMath::vec3 specularTerm = material.specular;
            specularTerm *= specularFactor;
            specularTerm *= attenuatedColor;

            if (frame.shadowMap != nullptr && frame.shadowMap->lightType == ShadowMap::LightType::Spot)
            {
                const float shadowVis =
                    frame.shadowMap->SampleVisibility(surface.positionWorld, surfaceNormal, lightDir);
                diffuseTerm *= shadowVis;
                specularTerm *= shadowVis;
            }

            totalSpecularColor += specularTerm;
        }
    }

    SRMath::vec3 color = ambientColor;
    color += totalDiffuseColor;
    color += totalSpecularColor;

    // 최종 색상의 각 채널(R, G, B)을 0.0과 1.0 사이로 클램핑합니다.
    if (frame.tonemap)
        color = sr::ACESFilm(color);
    else
        color.clamp(0.f, 1.0f);

    if (frame.srgbGamma)
    {
        constexpr float invGamma = 1.0f / 2.2f;
        color.x = std::pow(color.x, invGamma);
        color.y = std::pow(color.y, invGamma);
        color.z = std::pow(color.z, invGamma);
    }

    return color;
}

namespace
{
Lanes3 Transpose(const SRMath::vec3 &a, const SRMath::vec3 &b, const SRMath::vec3 &c, const SRMath::vec3 &d)
{
    __m128 r0 = a.m128, r1 = b.m128, r2 = c.m128, r3 = d.m128;
    _MM_TRANSPOSE4_PS(r0, r1, r2, r3);
    return {r0, r1, r2};
}

Lanes3 Broadcast(const SRMath::vec3 &v)
{
    return {_mm_set1_ps(v.x), _mm_set1_ps(v.y), _mm_set1_ps(v.z)};
}

// dot(vec3, vec3)의 _mm_dp_ps(.., 0x71): 곱 세 개와 제외된 lane의 +0을 (p0 + p1) + (p2 + 0)으로 더한다.
__m128 Dot(const Lanes3 &a, const Lanes3 &b)
{
    const __m128 xy = _mm_add_ps(_mm_mul_ps(a.x, b.x), _mm_mul_ps(a.y, b.y));
    return _mm_add_ps(xy, _mm_add_ps(_mm_mul_ps(a.z, b.z), _mm_setzero_ps()));
}

// SRMath::normalize: 길이가 1e-5 미만이면 그대로, 아니면 v * (1 / 길이).
Lanes3 Normalize(const Lanes3 &v)
{
    const __m128 length = _mm_sqrt_ps(Dot(v, v));
    const __m128 keep = _mm_cmplt_ps(length, _mm_set1_ps(1e-5f));
    const __m128 reciprocal = _mm_div_ps(_mm_set1_ps(1.0f), length);
    return {_mm_blendv_ps(_mm_mul_ps(v.x, reciprocal), v.x, keep),
            _mm_blendv_ps(_mm_mul_ps(v.y, reciprocal), v.y, keep),
            _mm_blendv_ps(_mm_mul_ps(v.z, reciprocal), v.z, keep)};
}

// ShadeSurface의 lane 버전 본체. normal은 이미 정규화된 월드 법선, u/v는 텍스처 좌표다.

inline __m128 GGX_D4(__m128 NdotH, __m128 alpha2) noexcept
{
    const __m128 one = _mm_set1_ps(1.0f);
    const __m128 pi = _mm_set1_ps(3.14159265358979323846f);
    const __m128 NdotH2 = _mm_mul_ps(NdotH, NdotH);
    const __m128 d = _mm_add_ps(_mm_mul_ps(NdotH2, _mm_sub_ps(alpha2, one)), one);
    const __m128 denom = _mm_mul_ps(pi, _mm_mul_ps(d, d));
    return _mm_div_ps(alpha2, denom);
}

inline __m128 SmithGGX_V4(__m128 NdotL, __m128 NdotV, __m128 alpha2) noexcept
{
    const __m128 one = _mm_set1_ps(1.0f);
    const __m128 half = _mm_set1_ps(0.5f);
    const __m128 eps = _mm_set1_ps(1e-7f);
    const __m128 oneMinusAlpha2 = _mm_sub_ps(one, alpha2);

    const __m128 termL = _mm_add_ps(_mm_mul_ps(_mm_mul_ps(NdotL, NdotL), oneMinusAlpha2), alpha2);
    const __m128 termV = _mm_add_ps(_mm_mul_ps(_mm_mul_ps(NdotV, NdotV), oneMinusAlpha2), alpha2);
    const __m128 denomL = _mm_mul_ps(NdotV, _mm_sqrt_ps(termL));
    const __m128 denomV = _mm_mul_ps(NdotL, _mm_sqrt_ps(termV));
    const __m128 denom = _mm_max_ps(_mm_add_ps(denomL, denomV), eps);
    return _mm_div_ps(half, denom);
}

inline Lanes3 Schlick_F4(__m128 VdotH, const Lanes3 &F0) noexcept
{
    const __m128 one = _mm_set1_ps(1.0f);
    const __m128 f = _mm_sub_ps(one, VdotH);
    const __m128 f2 = _mm_mul_ps(f, f);
    const __m128 f5 = _mm_mul_ps(_mm_mul_ps(f2, f2), f);
    return {_mm_add_ps(F0.x, _mm_mul_ps(_mm_sub_ps(one, F0.x), f5)),
            _mm_add_ps(F0.y, _mm_mul_ps(_mm_sub_ps(one, F0.y), f5)),
            _mm_add_ps(F0.z, _mm_mul_ps(_mm_sub_ps(one, F0.z), f5))};
}

inline void EnvBRDFApprox4(__m128 NdotV, __m128 roughness, __m128 &scaleA, __m128 &biasB) noexcept
{
    const __m128 r_x = _mm_add_ps(_mm_mul_ps(roughness, _mm_set1_ps(-1.0f)), _mm_set1_ps(1.0f));
    const __m128 r_y = _mm_add_ps(_mm_mul_ps(roughness, _mm_set1_ps(-0.0275f)), _mm_set1_ps(0.0425f));
    const __m128 r_z = _mm_add_ps(_mm_mul_ps(roughness, _mm_set1_ps(-0.572f)), _mm_set1_ps(1.04f));
    const __m128 r_w = _mm_add_ps(_mm_mul_ps(roughness, _mm_set1_ps(0.022f)), _mm_set1_ps(-0.04f));

    alignas(16) float nv[4];
    _mm_store_ps(nv, NdotV);
    alignas(16) float e[4];
    for (int i = 0; i < 4; ++i)
        e[i] = std::exp2(-9.28f * nv[i]);
    const __m128 expTerm = _mm_load_ps(e);

    const __m128 r_x2 = _mm_mul_ps(r_x, r_x);
    const __m128 minTerm = _mm_min_ps(r_x2, expTerm);
    const __m128 a004 = _mm_add_ps(_mm_mul_ps(minTerm, r_x), r_y);

    scaleA = _mm_max_ps(_mm_setzero_ps(), _mm_add_ps(_mm_mul_ps(_mm_set1_ps(-1.04f), a004), r_z));
    biasB = _mm_max_ps(_mm_setzero_ps(), _mm_add_ps(_mm_mul_ps(_mm_set1_ps(0.04f), a004), r_w));
}

inline Lanes3 ACESFilm4(const Lanes3 &v) noexcept
{
    const __m128 zero = _mm_setzero_ps();
    const __m128 one = _mm_set1_ps(1.0f);
    const __m128 a = _mm_set1_ps(2.51f);
    const __m128 b = _mm_set1_ps(0.03f);
    const __m128 c = _mm_set1_ps(2.43f);
    const __m128 d = _mm_set1_ps(0.59f);
    const __m128 e = _mm_set1_ps(0.14f);

    auto mapLane = [&](__m128 x) {
        x = _mm_max_ps(zero, x);
        const __m128 num = _mm_mul_ps(x, _mm_add_ps(_mm_mul_ps(a, x), b));
        const __m128 den = _mm_add_ps(_mm_mul_ps(x, _mm_add_ps(_mm_mul_ps(c, x), d)), e);
        return _mm_max_ps(zero, _mm_min_ps(one, _mm_div_ps(num, den)));
    };

    return Lanes3{mapLane(v.x), mapLane(v.y), mapLane(v.z)};
}

void ShadeLanesPBR(const Lanes3 &normal, const Lanes3 &position, const float (&u)[4], const float (&v)[4],
                   const float (&lod)[4], const Material *const (&material)[4], const RenderFrameContext &frame,
                   SRMath::vec3 (&colors)[4])
{
    SRMath::vec3 baseColor[4];
    float roughness[4], metallic[4], occlusion[4];
    SRMath::vec3 emissive[4], ambientMat[4];

    for (int i = 0; i < 4; ++i)
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

    const Lanes3 base = Transpose(baseColor[0], baseColor[1], baseColor[2], baseColor[3]);
    const __m128 r = _mm_setr_ps(roughness[0], roughness[1], roughness[2], roughness[3]);
    const __m128 m = _mm_setr_ps(metallic[0], metallic[1], metallic[2], metallic[3]);
    const __m128 occ = _mm_setr_ps(occlusion[0], occlusion[1], occlusion[2], occlusion[3]);
    const Lanes3 em = Transpose(emissive[0], emissive[1], emissive[2], emissive[3]);
    const Lanes3 ambMat = Transpose(ambientMat[0], ambientMat[1], ambientMat[2], ambientMat[3]);

    const __m128 alpha = _mm_mul_ps(r, r);
    const __m128 alpha2 = _mm_mul_ps(alpha, alpha);

    const __m128 zero = _mm_setzero_ps();
    const __m128 one = _mm_set1_ps(1.0f);
    const __m128 pointZeroFour = _mm_set1_ps(0.04f);
    const __m128 pi = _mm_set1_ps(3.14159265358979323846f);

    const __m128 oneMinusM = _mm_sub_ps(one, m);
    const Lanes3 F0{
        _mm_add_ps(_mm_mul_ps(pointZeroFour, oneMinusM), _mm_mul_ps(base.x, m)),
        _mm_add_ps(_mm_mul_ps(pointZeroFour, oneMinusM), _mm_mul_ps(base.y, m)),
        _mm_add_ps(_mm_mul_ps(pointZeroFour, oneMinusM), _mm_mul_ps(base.z, m))
    };

    const Lanes3 camera = Broadcast(frame.cameraPosition);
    const Lanes3 viewDir = Normalize({_mm_sub_ps(camera.x, position.x),
                                      _mm_sub_ps(camera.y, position.y),
                                      _mm_sub_ps(camera.z, position.z)});

    const __m128 NdotV = _mm_max_ps(_mm_set1_ps(1e-4f), _mm_min_ps(one, Dot(normal, viewDir)));

    Lanes3 totalDirect{zero, zero, zero};

    for (const auto &light : frame.lights)
    {
        const Lanes3 lightDir = Broadcast(light.directionToLight);
        const Lanes3 lightColor = Broadcast(light.color);

        const __m128 NdotL = _mm_max_ps(zero, Dot(normal, lightDir));
        const __m128 activeMask = _mm_cmpgt_ps(NdotL, zero);
        if (_mm_movemask_ps(activeMask) == 0)
            continue;

        const Lanes3 H = Normalize({_mm_add_ps(lightDir.x, viewDir.x),
                                    _mm_add_ps(lightDir.y, viewDir.y),
                                    _mm_add_ps(lightDir.z, viewDir.z)});
        const __m128 NdotH = _mm_max_ps(zero, _mm_min_ps(one, Dot(normal, H)));
        const __m128 VdotH = _mm_max_ps(zero, _mm_min_ps(one, Dot(viewDir, H)));

        const __m128 D = GGX_D4(NdotH, alpha2);
        const __m128 V = SmithGGX_V4(NdotL, NdotV, alpha2);
        const Lanes3 F = Schlick_F4(VdotH, F0);

        const __m128 piDV = _mm_mul_ps(pi, _mm_mul_ps(D, V));
        const Lanes3 specular{_mm_mul_ps(F.x, piDV), _mm_mul_ps(F.y, piDV), _mm_mul_ps(F.z, piDV)};

        const __m128 kD_scale = oneMinusM;
        const Lanes3 diffuse{_mm_mul_ps(_mm_mul_ps(_mm_sub_ps(one, F.x), kD_scale), base.x),
                             _mm_mul_ps(_mm_mul_ps(_mm_sub_ps(one, F.y), kD_scale), base.y),
                             _mm_mul_ps(_mm_mul_ps(_mm_sub_ps(one, F.z), kD_scale), base.z)};

        __m128 factorX = _mm_mul_ps(_mm_add_ps(diffuse.x, specular.x), _mm_mul_ps(lightColor.x, NdotL));
        __m128 factorY = _mm_mul_ps(_mm_add_ps(diffuse.y, specular.y), _mm_mul_ps(lightColor.y, NdotL));
        __m128 factorZ = _mm_mul_ps(_mm_add_ps(diffuse.z, specular.z), _mm_mul_ps(lightColor.z, NdotL));

        if (frame.shadowMap != nullptr && frame.shadowMap->lightType == ShadowMap::LightType::Directional)
        {
            alignas(16) float vis[4];
            alignas(16) float px[4], py[4], pz[4], nx[4], ny[4], nz[4];
            _mm_store_ps(px, position.x); _mm_store_ps(py, position.y); _mm_store_ps(pz, position.z);
            _mm_store_ps(nx, normal.x); _mm_store_ps(ny, normal.y); _mm_store_ps(nz, normal.z);
            for (int i = 0; i < 4; ++i)
            {
                vis[i] = frame.shadowMap->SampleVisibility({px[i], py[i], pz[i]}, {nx[i], ny[i], nz[i]}, light.directionToLight);
            }
            const __m128 shadowVis = _mm_load_ps(vis);
            factorX = _mm_mul_ps(factorX, shadowVis);
            factorY = _mm_mul_ps(factorY, shadowVis);
            factorZ = _mm_mul_ps(factorZ, shadowVis);
        }

        totalDirect.x = _mm_add_ps(totalDirect.x, _mm_and_ps(activeMask, factorX));
        totalDirect.y = _mm_add_ps(totalDirect.y, _mm_and_ps(activeMask, factorY));
        totalDirect.z = _mm_add_ps(totalDirect.z, _mm_and_ps(activeMask, factorZ));
    }

    if (!frame.pointLights.empty())
    {
        for (const auto &light : frame.pointLights)
        {
            const Lanes3 lightPos = Broadcast(light.position);
            const Lanes3 lightColor = Broadcast(light.color);
            const Lanes3 toLight{_mm_sub_ps(lightPos.x, position.x), _mm_sub_ps(lightPos.y, position.y),
                                 _mm_sub_ps(lightPos.z, position.z)};
            const __m128 distSq = Dot(toLight, toLight);
            const __m128 dist = _mm_sqrt_ps(distSq);
            const Lanes3 lightDir = Normalize(toLight);

            const __m128 ratio = _mm_mul_ps(dist, _mm_set1_ps(light.invRange));
            const __m128 ratioSq = _mm_mul_ps(ratio, ratio);
            const __m128 window = _mm_max_ps(zero, _mm_sub_ps(one, _mm_mul_ps(ratioSq, ratioSq)));
            const __m128 denom = _mm_add_ps(_mm_set1_ps(light.constant),
                                            _mm_add_ps(_mm_mul_ps(_mm_set1_ps(light.linear), dist),
                                                       _mm_mul_ps(_mm_set1_ps(light.quadratic), distSq)));
            const __m128 atten = _mm_div_ps(_mm_mul_ps(window, window), _mm_max_ps(denom, _mm_set1_ps(1e-4f)));

            const __m128 NdotL = _mm_max_ps(zero, Dot(normal, lightDir));
            const __m128 activeMask = _mm_and_ps(_mm_cmpgt_ps(atten, zero), _mm_cmpgt_ps(NdotL, zero));
            if (_mm_movemask_ps(activeMask) == 0)
                continue;

            const Lanes3 H = Normalize({_mm_add_ps(lightDir.x, viewDir.x),
                                        _mm_add_ps(lightDir.y, viewDir.y),
                                        _mm_add_ps(lightDir.z, viewDir.z)});
            const __m128 NdotH = _mm_max_ps(zero, _mm_min_ps(one, Dot(normal, H)));
            const __m128 VdotH = _mm_max_ps(zero, _mm_min_ps(one, Dot(viewDir, H)));

            const __m128 D = GGX_D4(NdotH, alpha2);
            const __m128 V = SmithGGX_V4(NdotL, NdotV, alpha2);
            const Lanes3 F = Schlick_F4(VdotH, F0);

            const __m128 piDV = _mm_mul_ps(pi, _mm_mul_ps(D, V));
            const Lanes3 specular{_mm_mul_ps(F.x, piDV), _mm_mul_ps(F.y, piDV), _mm_mul_ps(F.z, piDV)};

            const __m128 kD_scale = oneMinusM;
            const Lanes3 diffuse{_mm_mul_ps(_mm_mul_ps(_mm_sub_ps(one, F.x), kD_scale), base.x),
                                 _mm_mul_ps(_mm_mul_ps(_mm_sub_ps(one, F.y), kD_scale), base.y),
                                 _mm_mul_ps(_mm_mul_ps(_mm_sub_ps(one, F.z), kD_scale), base.z)};

            const __m128 lightScale = _mm_mul_ps(atten, NdotL);
            const __m128 factorX = _mm_mul_ps(_mm_add_ps(diffuse.x, specular.x), _mm_mul_ps(lightColor.x, lightScale));
            const __m128 factorY = _mm_mul_ps(_mm_add_ps(diffuse.y, specular.y), _mm_mul_ps(lightColor.y, lightScale));
            const __m128 factorZ = _mm_mul_ps(_mm_add_ps(diffuse.z, specular.z), _mm_mul_ps(lightColor.z, lightScale));

            totalDirect.x = _mm_add_ps(totalDirect.x, _mm_and_ps(activeMask, factorX));
            totalDirect.y = _mm_add_ps(totalDirect.y, _mm_and_ps(activeMask, factorY));
            totalDirect.z = _mm_add_ps(totalDirect.z, _mm_and_ps(activeMask, factorZ));
        }
    }

    if (!frame.spotLights.empty())
    {
        for (const auto &light : frame.spotLights)
        {
            const Lanes3 lightPos = Broadcast(light.position);
            const Lanes3 lightColor = Broadcast(light.color);
            const Lanes3 spotDir = Broadcast(light.directionToLight);
            const Lanes3 toLight{_mm_sub_ps(lightPos.x, position.x), _mm_sub_ps(lightPos.y, position.y),
                                 _mm_sub_ps(lightPos.z, position.z)};
            const __m128 distSq = Dot(toLight, toLight);
            const __m128 dist = _mm_sqrt_ps(distSq);
            const Lanes3 lightDir = Normalize(toLight);

            const __m128 ratio = _mm_mul_ps(dist, _mm_set1_ps(light.invRange));
            const __m128 ratioSq = _mm_mul_ps(ratio, ratio);
            const __m128 window = _mm_max_ps(zero, _mm_sub_ps(one, _mm_mul_ps(ratioSq, ratioSq)));
            const __m128 denom = _mm_add_ps(_mm_set1_ps(light.constant),
                                            _mm_add_ps(_mm_mul_ps(_mm_set1_ps(light.linear), dist),
                                                       _mm_mul_ps(_mm_set1_ps(light.quadratic), distSq)));
            const __m128 distAtten = _mm_div_ps(_mm_mul_ps(window, window), _mm_max_ps(denom, _mm_set1_ps(1e-4f)));

            const __m128 cosTheta = Dot(lightDir, spotDir);
            const __m128 rawT = _mm_mul_ps(_mm_sub_ps(cosTheta, _mm_set1_ps(light.outerConeCos)),
                                           _mm_set1_ps(light.invConeCosDiff));
            const __m128 spotT = _mm_max_ps(zero, _mm_min_ps(one, rawT));
            const __m128 spotFactor = _mm_mul_ps(_mm_mul_ps(spotT, spotT),
                                                 _mm_sub_ps(_mm_set1_ps(3.0f), _mm_mul_ps(_mm_set1_ps(2.0f), spotT)));
            const __m128 atten = _mm_mul_ps(distAtten, spotFactor);

            const __m128 NdotL = _mm_max_ps(zero, Dot(normal, lightDir));
            const __m128 activeMask = _mm_and_ps(_mm_cmpgt_ps(atten, zero), _mm_cmpgt_ps(NdotL, zero));
            if (_mm_movemask_ps(activeMask) == 0)
                continue;

            const Lanes3 H = Normalize({_mm_add_ps(lightDir.x, viewDir.x),
                                        _mm_add_ps(lightDir.y, viewDir.y),
                                        _mm_add_ps(lightDir.z, viewDir.z)});
            const __m128 NdotH = _mm_max_ps(zero, _mm_min_ps(one, Dot(normal, H)));
            const __m128 VdotH = _mm_max_ps(zero, _mm_min_ps(one, Dot(viewDir, H)));

            const __m128 D = GGX_D4(NdotH, alpha2);
            const __m128 V = SmithGGX_V4(NdotL, NdotV, alpha2);
            const Lanes3 F = Schlick_F4(VdotH, F0);

            const __m128 piDV = _mm_mul_ps(pi, _mm_mul_ps(D, V));
            const Lanes3 specular{_mm_mul_ps(F.x, piDV), _mm_mul_ps(F.y, piDV), _mm_mul_ps(F.z, piDV)};

            const __m128 kD_scale = oneMinusM;
            const Lanes3 diffuse{_mm_mul_ps(_mm_mul_ps(_mm_sub_ps(one, F.x), kD_scale), base.x),
                                 _mm_mul_ps(_mm_mul_ps(_mm_sub_ps(one, F.y), kD_scale), base.y),
                                 _mm_mul_ps(_mm_mul_ps(_mm_sub_ps(one, F.z), kD_scale), base.z)};

            const __m128 lightScale = _mm_mul_ps(atten, NdotL);
            __m128 factorX = _mm_mul_ps(_mm_add_ps(diffuse.x, specular.x), _mm_mul_ps(lightColor.x, lightScale));
            __m128 factorY = _mm_mul_ps(_mm_add_ps(diffuse.y, specular.y), _mm_mul_ps(lightColor.y, lightScale));
            __m128 factorZ = _mm_mul_ps(_mm_add_ps(diffuse.z, specular.z), _mm_mul_ps(lightColor.z, lightScale));

            if (frame.shadowMap != nullptr && frame.shadowMap->lightType == ShadowMap::LightType::Spot)
            {
                alignas(16) float vis[4];
                alignas(16) float px[4], py[4], pz[4], nx[4], ny[4], nz[4], lx[4], ly[4], lz[4];
                _mm_store_ps(px, position.x); _mm_store_ps(py, position.y); _mm_store_ps(pz, position.z);
                _mm_store_ps(nx, normal.x); _mm_store_ps(ny, normal.y); _mm_store_ps(nz, normal.z);
                _mm_store_ps(lx, lightDir.x); _mm_store_ps(ly, lightDir.y); _mm_store_ps(lz, lightDir.z);
                for (int i = 0; i < 4; ++i)
                {
                    vis[i] = frame.shadowMap->SampleVisibility({px[i], py[i], pz[i]}, {nx[i], ny[i], nz[i]}, {lx[i], ly[i], lz[i]});
                }
                const __m128 shadowVis = _mm_load_ps(vis);
                factorX = _mm_mul_ps(factorX, shadowVis);
                factorY = _mm_mul_ps(factorY, shadowVis);
                factorZ = _mm_mul_ps(factorZ, shadowVis);
            }

            totalDirect.x = _mm_add_ps(totalDirect.x, _mm_and_ps(activeMask, factorX));
            totalDirect.y = _mm_add_ps(totalDirect.y, _mm_and_ps(activeMask, factorY));
            totalDirect.z = _mm_add_ps(totalDirect.z, _mm_and_ps(activeMask, factorZ));
        }
    }

    Lanes3 ambient{_mm_mul_ps(_mm_mul_ps(base.x, ambMat.x), occ),
                   _mm_mul_ps(_mm_mul_ps(base.y, ambMat.y), occ),
                   _mm_mul_ps(_mm_mul_ps(base.z, ambMat.z), occ)};

    if (frame.ibl && frame.iblMap != nullptr)
    {
        const auto *ibl = static_cast<const sr::IBLMap *>(frame.iblMap);

        // 1. Diffuse IBL
        alignas(16) float nx[4], ny[4], nz[4];
        _mm_store_ps(nx, normal.x);
        _mm_store_ps(ny, normal.y);
        _mm_store_ps(nz, normal.z);
        alignas(16) float irR[4], irG[4], irB[4];
        for (int i = 0; i < 4; ++i)
        {
            const auto ir = ibl->sh.Evaluate({nx[i], ny[i], nz[i]});
            irR[i] = ir.x; irG[i] = ir.y; irB[i] = ir.z;
        }
        const Lanes3 irradiance{_mm_load_ps(irR), _mm_load_ps(irG), _mm_load_ps(irB)};

        const __m128 oneMinusNdotV = _mm_sub_ps(one, NdotV);
        const __m128 f2 = _mm_mul_ps(oneMinusNdotV, oneMinusNdotV);
        const __m128 f5 = _mm_mul_ps(_mm_mul_ps(f2, f2), oneMinusNdotV);
        const __m128 oneMinusR = _mm_sub_ps(one, r);
        const Lanes3 maxF{_mm_max_ps(oneMinusR, F0.x), _mm_max_ps(oneMinusR, F0.y), _mm_max_ps(oneMinusR, F0.z)};
        const Lanes3 F_rough{
            _mm_add_ps(F0.x, _mm_mul_ps(_mm_sub_ps(maxF.x, F0.x), f5)),
            _mm_add_ps(F0.y, _mm_mul_ps(_mm_sub_ps(maxF.y, F0.y), f5)),
            _mm_add_ps(F0.z, _mm_mul_ps(_mm_sub_ps(maxF.z, F0.z), f5))
        };
        const __m128 kD_scale = _mm_sub_ps(one, m);
        const Lanes3 diffuseIBL{
            _mm_mul_ps(_mm_mul_ps(_mm_mul_ps(_mm_sub_ps(one, F_rough.x), kD_scale), base.x), irradiance.x),
            _mm_mul_ps(_mm_mul_ps(_mm_mul_ps(_mm_sub_ps(one, F_rough.y), kD_scale), base.y), irradiance.y),
            _mm_mul_ps(_mm_mul_ps(_mm_mul_ps(_mm_sub_ps(one, F_rough.z), kD_scale), base.z), irradiance.z)
        };

        // 2. Specular IBL
        const __m128 twoNdotV = _mm_mul_ps(_mm_set1_ps(2.0f), NdotV);
        const Lanes3 R = Normalize({
            _mm_sub_ps(_mm_mul_ps(twoNdotV, normal.x), viewDir.x),
            _mm_sub_ps(_mm_mul_ps(twoNdotV, normal.y), viewDir.y),
            _mm_sub_ps(_mm_mul_ps(twoNdotV, normal.z), viewDir.z)
        });

        alignas(16) float rx[4], ry[4], rz[4], rough[4];
        _mm_store_ps(rx, R.x); _mm_store_ps(ry, R.y); _mm_store_ps(rz, R.z);
        _mm_store_ps(rough, r);
        alignas(16) float specR[4], specG[4], specB[4];
        for (int i = 0; i < 4; ++i)
        {
            const auto sp = ibl->SampleSpecular({rx[i], ry[i], rz[i]}, rough[i]);
            specR[i] = sp.x; specG[i] = sp.y; specB[i] = sp.z;
        }
        const Lanes3 prefilteredColor{_mm_load_ps(specR), _mm_load_ps(specG), _mm_load_ps(specB)};

        __m128 scaleA, biasB;
        EnvBRDFApprox4(NdotV, r, scaleA, biasB);

        const Lanes3 specularIBL{
            _mm_mul_ps(prefilteredColor.x, _mm_add_ps(_mm_mul_ps(F0.x, scaleA), biasB)),
            _mm_mul_ps(prefilteredColor.y, _mm_add_ps(_mm_mul_ps(F0.y, scaleA), biasB)),
            _mm_mul_ps(prefilteredColor.z, _mm_add_ps(_mm_mul_ps(F0.z, scaleA), biasB))
        };

        ambient.x = _mm_mul_ps(_mm_add_ps(diffuseIBL.x, specularIBL.x), occ);
        ambient.y = _mm_mul_ps(_mm_add_ps(diffuseIBL.y, specularIBL.y), occ);
        ambient.z = _mm_mul_ps(_mm_add_ps(diffuseIBL.z, specularIBL.z), occ);
    }

    Lanes3 finalColor{
        _mm_add_ps(_mm_add_ps(ambient.x, totalDirect.x), em.x),
        _mm_add_ps(_mm_add_ps(ambient.y, totalDirect.y), em.y),
        _mm_add_ps(_mm_add_ps(ambient.z, totalDirect.z), em.z)
    };

    if (frame.tonemap)
    {
        finalColor = ACESFilm4(finalColor);
    }
    else
    {
        finalColor.x = _mm_max_ps(zero, _mm_min_ps(one, finalColor.x));
        finalColor.y = _mm_max_ps(zero, _mm_min_ps(one, finalColor.y));
        finalColor.z = _mm_max_ps(zero, _mm_min_ps(one, finalColor.z));
    }

    __m128 r0 = finalColor.x;
    __m128 r1 = finalColor.y;
    __m128 r2 = finalColor.z;
    __m128 r3 = zero;
    _MM_TRANSPOSE4_PS(r0, r1, r2, r3);
    colors[0].m128 = r0;
    colors[1].m128 = r1;
    colors[2].m128 = r2;
    colors[3].m128 = r3;

    if (frame.srgbGamma)
    {
        constexpr float invGamma = 1.0f / 2.2f;
        for (int i = 0; i < 4; ++i)
        {
            colors[i].x = std::pow(colors[i].x, invGamma);
            colors[i].y = std::pow(colors[i].y, invGamma);
            colors[i].z = std::pow(colors[i].z, invGamma);
        }
    }
}

void ShadeLanes(const Lanes3 &normal, const Lanes3 &position, const float (&u)[4], const float (&v)[4],
                const float (&lod)[4], const Material *const (&material)[4], const RenderFrameContext &frame,
                SRMath::vec3 (&colors)[4])
{
    const bool anyPbr = material[0]->isPbr || material[1]->isPbr || material[2]->isPbr || material[3]->isPbr;
    const bool allPbr = material[0]->isPbr && material[1]->isPbr && material[2]->isPbr && material[3]->isPbr;

    if (frame.shadingModel == EShadingModel::Auto && frame.pbr && anyPbr != allPbr)
    {
        alignas(16) float px[4], py[4], pz[4];
        alignas(16) float nx[4], ny[4], nz[4];
        _mm_store_ps(px, position.x);
        _mm_store_ps(py, position.y);
        _mm_store_ps(pz, position.z);
        _mm_store_ps(nx, normal.x);
        _mm_store_ps(ny, normal.y);
        _mm_store_ps(nz, normal.z);
        for (int i = 0; i < 4; ++i)
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
        ShadeLanesPBR(normal, position, u, v, lod, material, frame, colors);
        return;
    }
    // 재질 값은 lane마다 다를 수 있다. 같은 재질이면(대부분) 전치 대신 broadcast한다.
    const bool sameMaterial =
        material[0] == material[1] && material[0] == material[2] && material[0] == material[3];
    SRMath::vec3 baseColor[4];
    for (int i = 0; i < 4; ++i)
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
    const Lanes3 base = Transpose(baseColor[0], baseColor[1], baseColor[2], baseColor[3]);
    const Lanes3 ambient = sameMaterial ? Broadcast(material[0]->ambient)
                                        : Transpose(material[0]->ambient, material[1]->ambient,
                                                    material[2]->ambient, material[3]->ambient);
    const Lanes3 specular = sameMaterial ? Broadcast(material[0]->specular)
                                         : Transpose(material[0]->specular, material[1]->specular,
                                                     material[2]->specular, material[3]->specular);
    float exponent[4];
    for (int i = 0; i < 4; ++i)
        exponent[i] = std::max(1.0f, material[i]->shininess);

    const Lanes3 camera = Broadcast(frame.cameraPosition);
    const Lanes3 viewDir = Normalize({_mm_sub_ps(camera.x, position.x), _mm_sub_ps(camera.y, position.y),
                                      _mm_sub_ps(camera.z, position.z)});

    const __m128 zero = _mm_setzero_ps();
    const __m128 one = _mm_set1_ps(1.0f);
    Lanes3 totalDiffuse{zero, zero, zero};
    Lanes3 totalSpecular{zero, zero, zero};
    for (const auto &light : frame.lights)
    {
        const Lanes3 lightDir = Broadcast(light.directionToLight);
        const Lanes3 lightColor = Broadcast(light.color);

        // std::max(0.0f, d)는 d > 0이 아니면(NaN 포함) +0이다. _mm_max_ps(d, 0)과 같다.
        const __m128 diffuseIntensity = _mm_max_ps(Dot(normal, lightDir), zero);
        __m128 diffX = _mm_mul_ps(_mm_mul_ps(base.x, diffuseIntensity), lightColor.x);
        __m128 diffY = _mm_mul_ps(_mm_mul_ps(base.y, diffuseIntensity), lightColor.y);
        __m128 diffZ = _mm_mul_ps(_mm_mul_ps(base.z, diffuseIntensity), lightColor.z);

        // reflect(-1 * lightDir, normal) = incident - normal * (2 * dot(normal, incident))
        const Lanes3 incident = Broadcast(-1.0f * light.directionToLight);
        const __m128 twice = _mm_mul_ps(_mm_set1_ps(2.0f), Dot(normal, incident));
        const Lanes3 reflectDirection{_mm_sub_ps(incident.x, _mm_mul_ps(normal.x, twice)),
                                      _mm_sub_ps(incident.y, _mm_mul_ps(normal.y, twice)),
                                      _mm_sub_ps(incident.z, _mm_mul_ps(normal.z, twice))};
        const __m128 specularBase = _mm_max_ps(Dot(viewDir, reflectDirection), zero);
        // ShadeSurface와 같은 조건에서만 powf를 부른다(pow(+0, y >= 1) = +0).
        const int needPow = _mm_movemask_ps(
            _mm_and_ps(_mm_cmpgt_ps(diffuseIntensity, zero), _mm_cmpneq_ps(specularBase, zero)));
        alignas(16) float specularFactor[4] = {};
        if (needPow)
        {
            alignas(16) float baseLane[4];
            _mm_store_ps(baseLane, specularBase);
            for (int i = 0; i < 4; ++i)
                if (needPow & (1 << i))
                    specularFactor[i] = std::pow(baseLane[i], exponent[i]);
        }
        const __m128 factor = _mm_load_ps(specularFactor);
        __m128 specX = _mm_mul_ps(_mm_mul_ps(specular.x, factor), lightColor.x);
        __m128 specY = _mm_mul_ps(_mm_mul_ps(specular.y, factor), lightColor.y);
        __m128 specZ = _mm_mul_ps(_mm_mul_ps(specular.z, factor), lightColor.z);

        if (frame.shadowMap != nullptr && frame.shadowMap->lightType == ShadowMap::LightType::Directional)
        {
            const __m128 shadowVis = frame.shadowMap->SampleVisibility4(normal, position, lightDir);
            diffX = _mm_mul_ps(diffX, shadowVis);
            diffY = _mm_mul_ps(diffY, shadowVis);
            diffZ = _mm_mul_ps(diffZ, shadowVis);
            specX = _mm_mul_ps(specX, shadowVis);
            specY = _mm_mul_ps(specY, shadowVis);
            specZ = _mm_mul_ps(specZ, shadowVis);
        }

        totalDiffuse.x = _mm_add_ps(totalDiffuse.x, diffX);
        totalDiffuse.y = _mm_add_ps(totalDiffuse.y, diffY);
        totalDiffuse.z = _mm_add_ps(totalDiffuse.z, diffZ);
        totalSpecular.x = _mm_add_ps(totalSpecular.x, specX);
        totalSpecular.y = _mm_add_ps(totalSpecular.y, specY);
        totalSpecular.z = _mm_add_ps(totalSpecular.z, specZ);
    }

    if (!frame.pointLights.empty())
    {
        for (const auto &light : frame.pointLights)
        {
            const Lanes3 lightPos = Broadcast(light.position);
            const Lanes3 toLight{_mm_sub_ps(lightPos.x, position.x), _mm_sub_ps(lightPos.y, position.y),
                                 _mm_sub_ps(lightPos.z, position.z)};
            const __m128 distSq = Dot(toLight, toLight);
            const __m128 dist = _mm_sqrt_ps(distSq);
            const Lanes3 lightDir = Normalize(toLight);

            const __m128 ratio = _mm_mul_ps(dist, _mm_set1_ps(light.invRange));
            const __m128 ratioSq = _mm_mul_ps(ratio, ratio);
            const __m128 window = _mm_max_ps(zero, _mm_sub_ps(one, _mm_mul_ps(ratioSq, ratioSq)));
            const __m128 denom = _mm_add_ps(_mm_add_ps(_mm_set1_ps(light.constant),
                                                       _mm_mul_ps(_mm_set1_ps(light.linear), dist)),
                                            _mm_mul_ps(_mm_set1_ps(light.quadratic), distSq));
            const __m128 atten = _mm_div_ps(_mm_mul_ps(window, window), _mm_max_ps(denom, _mm_set1_ps(1e-4f)));
            const Lanes3 lightColor = Broadcast(light.color);
            const Lanes3 attenuatedColor{_mm_mul_ps(lightColor.x, atten), _mm_mul_ps(lightColor.y, atten),
                                         _mm_mul_ps(lightColor.z, atten)};

            const __m128 diffuseIntensity = _mm_max_ps(Dot(normal, lightDir), zero);
            __m128 diffX = _mm_mul_ps(_mm_mul_ps(base.x, diffuseIntensity), attenuatedColor.x);
            __m128 diffY = _mm_mul_ps(_mm_mul_ps(base.y, diffuseIntensity), attenuatedColor.y);
            __m128 diffZ = _mm_mul_ps(_mm_mul_ps(base.z, diffuseIntensity), attenuatedColor.z);

            const Lanes3 incident{_mm_mul_ps(_mm_set1_ps(-1.0f), lightDir.x),
                                  _mm_mul_ps(_mm_set1_ps(-1.0f), lightDir.y),
                                  _mm_mul_ps(_mm_set1_ps(-1.0f), lightDir.z)};
            const __m128 twice = _mm_mul_ps(_mm_set1_ps(2.0f), Dot(normal, incident));
            const Lanes3 reflectDirection{_mm_sub_ps(incident.x, _mm_mul_ps(normal.x, twice)),
                                          _mm_sub_ps(incident.y, _mm_mul_ps(normal.y, twice)),
                                          _mm_sub_ps(incident.z, _mm_mul_ps(normal.z, twice))};
            const __m128 specularBase = _mm_max_ps(Dot(viewDir, reflectDirection), zero);
            const int needPow = _mm_movemask_ps(
                _mm_and_ps(_mm_cmpgt_ps(diffuseIntensity, zero), _mm_cmpneq_ps(specularBase, zero)));
            alignas(16) float specularFactor[4] = {};
            if (needPow)
            {
                alignas(16) float baseLane[4];
                _mm_store_ps(baseLane, specularBase);
                for (int i = 0; i < 4; ++i)
                    if (needPow & (1 << i))
                        specularFactor[i] = std::pow(baseLane[i], exponent[i]);
            }
            const __m128 factor = _mm_load_ps(specularFactor);
            __m128 specX = _mm_mul_ps(_mm_mul_ps(specular.x, factor), attenuatedColor.x);
            __m128 specY = _mm_mul_ps(_mm_mul_ps(specular.y, factor), attenuatedColor.y);
            __m128 specZ = _mm_mul_ps(_mm_mul_ps(specular.z, factor), attenuatedColor.z);

            if (frame.shadowMap != nullptr && frame.shadowMap->lightType == ShadowMap::LightType::Spot)
            {
                const __m128 shadowVis = frame.shadowMap->SampleVisibility4(normal, position, lightDir);
                diffX = _mm_mul_ps(diffX, shadowVis);
                diffY = _mm_mul_ps(diffY, shadowVis);
                diffZ = _mm_mul_ps(diffZ, shadowVis);
                specX = _mm_mul_ps(specX, shadowVis);
                specY = _mm_mul_ps(specY, shadowVis);
                specZ = _mm_mul_ps(specZ, shadowVis);
            }

            totalDiffuse.x = _mm_add_ps(totalDiffuse.x, diffX);
            totalDiffuse.y = _mm_add_ps(totalDiffuse.y, diffY);
            totalDiffuse.z = _mm_add_ps(totalDiffuse.z, diffZ);
            totalSpecular.x = _mm_add_ps(totalSpecular.x, specX);
            totalSpecular.y = _mm_add_ps(totalSpecular.y, specY);
            totalSpecular.z = _mm_add_ps(totalSpecular.z, specZ);
        }
    }

    if (!frame.spotLights.empty())
    {
        for (const auto &light : frame.spotLights)
        {
            const Lanes3 lightPos = Broadcast(light.position);
            const Lanes3 toLight{_mm_sub_ps(lightPos.x, position.x), _mm_sub_ps(lightPos.y, position.y),
                                 _mm_sub_ps(lightPos.z, position.z)};
            const __m128 distSq = Dot(toLight, toLight);
            const __m128 dist = _mm_sqrt_ps(distSq);
            const Lanes3 lightDir = Normalize(toLight);

            const __m128 ratio = _mm_mul_ps(dist, _mm_set1_ps(light.invRange));
            const __m128 ratioSq = _mm_mul_ps(ratio, ratio);
            const __m128 window = _mm_max_ps(zero, _mm_sub_ps(one, _mm_mul_ps(ratioSq, ratioSq)));
            const __m128 denom = _mm_add_ps(_mm_add_ps(_mm_set1_ps(light.constant),
                                                       _mm_mul_ps(_mm_set1_ps(light.linear), dist)),
                                            _mm_mul_ps(_mm_set1_ps(light.quadratic), distSq));
            const __m128 distAtten =
                _mm_div_ps(_mm_mul_ps(window, window), _mm_max_ps(denom, _mm_set1_ps(1e-4f)));

            const __m128 cosTheta = Dot(lightDir, Broadcast(light.directionToLight));
            const __m128 rawT = _mm_mul_ps(_mm_sub_ps(cosTheta, _mm_set1_ps(light.outerConeCos)),
                                           _mm_set1_ps(light.invConeCosDiff));
            const __m128 spotT = _mm_max_ps(zero, _mm_min_ps(one, rawT));
            const __m128 spotFactor =
                _mm_mul_ps(_mm_mul_ps(spotT, spotT),
                           _mm_sub_ps(_mm_set1_ps(3.0f), _mm_mul_ps(_mm_set1_ps(2.0f), spotT)));
            const __m128 atten = _mm_mul_ps(distAtten, spotFactor);
            const Lanes3 lightColor = Broadcast(light.color);
            const Lanes3 attenuatedColor{_mm_mul_ps(lightColor.x, atten), _mm_mul_ps(lightColor.y, atten),
                                         _mm_mul_ps(lightColor.z, atten)};

            const __m128 diffuseIntensity = _mm_max_ps(Dot(normal, lightDir), zero);
            totalDiffuse.x =
                _mm_add_ps(totalDiffuse.x, _mm_mul_ps(_mm_mul_ps(base.x, diffuseIntensity), attenuatedColor.x));
            totalDiffuse.y =
                _mm_add_ps(totalDiffuse.y, _mm_mul_ps(_mm_mul_ps(base.y, diffuseIntensity), attenuatedColor.y));
            totalDiffuse.z =
                _mm_add_ps(totalDiffuse.z, _mm_mul_ps(_mm_mul_ps(base.z, diffuseIntensity), attenuatedColor.z));

            const Lanes3 incident{_mm_mul_ps(_mm_set1_ps(-1.0f), lightDir.x),
                                  _mm_mul_ps(_mm_set1_ps(-1.0f), lightDir.y),
                                  _mm_mul_ps(_mm_set1_ps(-1.0f), lightDir.z)};
            const __m128 twice = _mm_mul_ps(_mm_set1_ps(2.0f), Dot(normal, incident));
            const Lanes3 reflectDirection{_mm_sub_ps(incident.x, _mm_mul_ps(normal.x, twice)),
                                          _mm_sub_ps(incident.y, _mm_mul_ps(normal.y, twice)),
                                          _mm_sub_ps(incident.z, _mm_mul_ps(normal.z, twice))};
            const __m128 specularBase = _mm_max_ps(Dot(viewDir, reflectDirection), zero);
            const int needPow = _mm_movemask_ps(
                _mm_and_ps(_mm_cmpgt_ps(diffuseIntensity, zero), _mm_cmpneq_ps(specularBase, zero)));
            alignas(16) float specularFactor[4] = {};
            if (needPow)
            {
                alignas(16) float baseLane[4];
                _mm_store_ps(baseLane, specularBase);
                for (int i = 0; i < 4; ++i)
                    if (needPow & (1 << i))
                        specularFactor[i] = std::pow(baseLane[i], exponent[i]);
            }
            const __m128 factor = _mm_load_ps(specularFactor);
            totalSpecular.x = _mm_add_ps(totalSpecular.x,
                                         _mm_mul_ps(_mm_mul_ps(specular.x, factor), attenuatedColor.x));
            totalSpecular.y = _mm_add_ps(totalSpecular.y,
                                         _mm_mul_ps(_mm_mul_ps(specular.y, factor), attenuatedColor.y));
            totalSpecular.z = _mm_add_ps(totalSpecular.z,
                                         _mm_mul_ps(_mm_mul_ps(specular.z, factor), attenuatedColor.z));
        }
    }

    Lanes3 phongColor{
        _mm_add_ps(_mm_add_ps(ambient.x, totalDiffuse.x), totalSpecular.x),
        _mm_add_ps(_mm_add_ps(ambient.y, totalDiffuse.y), totalSpecular.y),
        _mm_add_ps(_mm_add_ps(ambient.z, totalDiffuse.z), totalSpecular.z)
    };
    if (frame.tonemap)
    {
        phongColor = ACESFilm4(phongColor);
    }
    else
    {
        phongColor.x = _mm_max_ps(zero, _mm_min_ps(one, phongColor.x));
        phongColor.y = _mm_max_ps(zero, _mm_min_ps(one, phongColor.y));
        phongColor.z = _mm_max_ps(zero, _mm_min_ps(one, phongColor.z));
    }
    __m128 r0 = phongColor.x;
    __m128 r1 = phongColor.y;
    __m128 r2 = phongColor.z;
    __m128 r3 = zero;
    _MM_TRANSPOSE4_PS(r0, r1, r2, r3);
    colors[0].m128 = r0;
    colors[1].m128 = r1;
    colors[2].m128 = r2;
    colors[3].m128 = r3;

    if (frame.srgbGamma)
    {
        constexpr float invGamma = 1.0f / 2.2f;
        for (int i = 0; i < 4; ++i)
        {
            colors[i].x = std::pow(colors[i].x, invGamma);
            colors[i].y = std::pow(colors[i].y, invGamma);
            colors[i].z = std::pow(colors[i].z, invGamma);
        }
    }
}
} // namespace

void Rasterizer::ShadeSurface4(const SurfaceSample (&surface)[4], const Material *const (&material)[4],
                               const RenderFrameContext &frame, SRMath::vec3 (&colors)[4])
{
    const float u[4] = {surface[0].texcoord.x, surface[1].texcoord.x, surface[2].texcoord.x,
                        surface[3].texcoord.x};
    const float v[4] = {surface[0].texcoord.y, surface[1].texcoord.y, surface[2].texcoord.y,
                        surface[3].texcoord.y};
    const float lod[4] = {surface[0].lod, surface[1].lod, surface[2].lod, surface[3].lod};
    SRMath::vec3 normal[4];
    for (int i = 0; i < 4; ++i)
    {
        normal[i] = material[i]->normalTexture
                        ? perturbNormal(surface[i].normalWorld, surface[i].tangentWorld,
                                        surface[i].bitangentWorld, u[i], v[i], lod[i], *material[i], frame)
                        : surface[i].normalWorld;
    }
    ShadeLanes(Transpose(normal[0], normal[1], normal[2], normal[3]),
               Transpose(surface[0].positionWorld, surface[1].positionWorld, surface[2].positionWorld,
                         surface[3].positionWorld),
               u, v, lod, material, frame, colors);
}

// InterpolateSurface를 lane마다 같은 순서((v0*w + v1*u) + v2*v, 그 뒤 *1/q)로 계산하되 vec3 복사(성분별
// 재조립) 없이 저장된 __m128을 바로 읽는다. 법선 정규화는 전치 뒤 4픽셀을 한 번에 한다.
void Rasterizer::ShadeOwnerQuad(const PreparedTriangle *const (&triangle)[4],
                                const OwnerSample *const (&sample)[4], const RenderFrameContext &frame,
                                SRMath::vec3 (&colors)[4])
{
    const auto blend = [](__m128 a0, __m128 a1, __m128 a2, __m128 a, __m128 b, __m128 c, __m128 r)
    {
        __m128 value = _mm_mul_ps(a0, a);
        value = _mm_add_ps(value, _mm_mul_ps(a1, b));
        value = _mm_add_ps(value, _mm_mul_ps(a2, c));
        return _mm_mul_ps(value, r);
    };
    SRMath::vec3 normal[4], position[4];
    float u[4] = {}, v[4] = {}, lod[4] = {};
    const Material *material[4] = {};
    for (int i = 0; i < 4; ++i)
    {
        const PreparedTriangle &t = *triangle[i];
        const OwnerSample &s = *sample[i];
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
        for (int i = 0; i < 4; ++i)
            lod[i] = ComputeTriangleLod(*triangle[i]);
    }
    Lanes3 normalLanes = Normalize(Transpose(normal[0], normal[1], normal[2], normal[3]));
    if (material[0]->normalTexture || material[1]->normalTexture || material[2]->normalTexture ||
        material[3]->normalTexture)
    {
        for (int i = 0; i < 4; ++i)
        {
            normal[i] = SRMath::normalize(normal[i]);
            if (material[i]->normalTexture)
            {
                SRMath::vec3 tangentWorld, bitangentWorld;
                ComputeTriangleTangentFrame(*triangle[i], tangentWorld, bitangentWorld);
                normal[i] = perturbNormal(normal[i], tangentWorld, bitangentWorld, u[i], v[i], lod[i],
                                          *material[i], frame);
            }
        }
        normalLanes = Transpose(normal[0], normal[1], normal[2], normal[3]);
    }
    ShadeLanes(normalLanes, Transpose(position[0], position[1], position[2], position[3]), u, v, lod, material,
               frame, colors);
}

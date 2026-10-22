#include "Renderer/Rasterizer.h"
#include "Graphics/Material.h"
#include "Graphics/Texture.h"
#include "Graphics/Light.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/Tile.h"
#include <smmintrin.h>

PreparedDirectionalLight PreparedDirectionalLight::From(const DirectionalLight &light) noexcept
{
    // 광선 진행 방향을 표면→광원 방향으로 바꾼다. normalize의 짧은 벡터/NaN 처리와
    // 연산 순서를 바꾸지 않아 정규화 전 입력을 받던 기존 셰이더와 같은 값을 전달한다.
    // 준비는 호출 스레드에서 실행된다. 픽셀 값과 달리 worker별 FP 예외 상태나
    // 예외 mask를 해제한 환경의 trap 시점/스레드까지 보존하는 API는 아니다.
    return {SRMath::normalize(-1.0f * light.rayDirection), light.color};
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
SRMath::vec3 Rasterizer::ShadeSurface(const SurfaceSample &surface, const Material &material,
                                      const RenderFrameContext &frame)
{
    SRMath::vec3 baseColor;

    if (material.diffuseTexture)
    {
        baseColor = material.diffuseTexture->Sample(surface.texcoord.x, surface.texcoord.y);
        baseColor *= material.diffuse;
    }
    else
    {
        baseColor = material.diffuse;
    }

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
        const float diffuseIntensity = std::max(0.0f, dot(surface.normalWorld, lightDir));
        SRMath::vec3 diffuseTerm = baseColor;
        diffuseTerm *= diffuseIntensity;
        diffuseTerm *= light.color;

        totalDiffuseColor += diffuseTerm;

        // 정반사 조명 계산
        const SRMath::vec3 reflectDirection = SRMath::reflect(-1.0f * lightDir, surface.normalWorld);
        // 반사광이 시선 반대쪽이면 밑이 +0이다. 지수는 max(1, shininess)로 1 이상(또는 +inf)이므로
        // pow(+0, y)는 정확히 +0이다. 그 경우 ucrtbase powf 호출(VTune 2위 핫스팟)을 건너뛴다.
        const float specularBase = std::max(0.0f, SRMath::dot(viewDir, reflectDirection));
        const float specularFactor = diffuseIntensity > 0.0f && specularBase != 0.0f
                                         ? std::pow(specularBase, std::max(1.0f, material.shininess))
                                         : 0.0f;

        SRMath::vec3 specularTerm = material.specular;
        specularTerm *= specularFactor;
        specularTerm *= light.color;

        totalSpecularColor += specularTerm;
    }

    SRMath::vec3 color = ambientColor;
    color += totalDiffuseColor;
    color += totalSpecularColor;

    // 최종 색상의 각 채널(R, G, B)을 0.0과 1.0 사이로 클램핑합니다.
    color.clamp(0.f, 1.0f);

    return color;
}

namespace
{
// SoA 한 성분 묶음. x, y, z의 lane i가 픽셀 i다.
struct Lanes3
{
    __m128 x, y, z;
};

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
void ShadeLanes(const Lanes3 &normal, const Lanes3 &position, const float (&u)[4], const float (&v)[4],
                const Material *const (&material)[4], const RenderFrameContext &frame,
                SRMath::vec3 (&colors)[4])
{
    // 재질 값은 lane마다 다를 수 있다. 같은 재질이면(대부분) 전치 대신 broadcast한다.
    const bool sameMaterial =
        material[0] == material[1] && material[0] == material[2] && material[0] == material[3];
    SRMath::vec3 baseColor[4];
    for (int i = 0; i < 4; ++i)
    {
        const Material &m = *material[i];
        if (m.diffuseTexture)
        {
            baseColor[i] = m.diffuseTexture->Sample(u[i], v[i]);
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
    Lanes3 totalDiffuse{zero, zero, zero};
    Lanes3 totalSpecular{zero, zero, zero};
    for (const auto &light : frame.lights)
    {
        const Lanes3 lightDir = Broadcast(light.directionToLight);
        const Lanes3 lightColor = Broadcast(light.color);

        // std::max(0.0f, d)는 d > 0이 아니면(NaN 포함) +0이다. _mm_max_ps(d, 0)과 같다.
        const __m128 diffuseIntensity = _mm_max_ps(Dot(normal, lightDir), zero);
        totalDiffuse.x =
            _mm_add_ps(totalDiffuse.x, _mm_mul_ps(_mm_mul_ps(base.x, diffuseIntensity), lightColor.x));
        totalDiffuse.y =
            _mm_add_ps(totalDiffuse.y, _mm_mul_ps(_mm_mul_ps(base.y, diffuseIntensity), lightColor.y));
        totalDiffuse.z =
            _mm_add_ps(totalDiffuse.z, _mm_mul_ps(_mm_mul_ps(base.z, diffuseIntensity), lightColor.z));

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
        totalSpecular.x =
            _mm_add_ps(totalSpecular.x, _mm_mul_ps(_mm_mul_ps(specular.x, factor), lightColor.x));
        totalSpecular.y =
            _mm_add_ps(totalSpecular.y, _mm_mul_ps(_mm_mul_ps(specular.y, factor), lightColor.y));
        totalSpecular.z =
            _mm_add_ps(totalSpecular.z, _mm_mul_ps(_mm_mul_ps(specular.z, factor), lightColor.z));
    }

    // color = ambient + diffuse + specular, clamp(0, 1)은 max(0, min(1, v)) 순서다.
    const __m128 one = _mm_set1_ps(1.0f);
    __m128 r0 =
        _mm_max_ps(zero, _mm_min_ps(one, _mm_add_ps(_mm_add_ps(ambient.x, totalDiffuse.x), totalSpecular.x)));
    __m128 r1 =
        _mm_max_ps(zero, _mm_min_ps(one, _mm_add_ps(_mm_add_ps(ambient.y, totalDiffuse.y), totalSpecular.y)));
    __m128 r2 =
        _mm_max_ps(zero, _mm_min_ps(one, _mm_add_ps(_mm_add_ps(ambient.z, totalDiffuse.z), totalSpecular.z)));
    __m128 r3 = zero;
    _MM_TRANSPOSE4_PS(r0, r1, r2, r3);
    colors[0].m128 = r0;
    colors[1].m128 = r1;
    colors[2].m128 = r2;
    colors[3].m128 = r3;
}
} // namespace

void Rasterizer::ShadeSurface4(const SurfaceSample (&surface)[4], const Material *const (&material)[4],
                               const RenderFrameContext &frame, SRMath::vec3 (&colors)[4])
{
    const float u[4] = {surface[0].texcoord.x, surface[1].texcoord.x, surface[2].texcoord.x,
                        surface[3].texcoord.x};
    const float v[4] = {surface[0].texcoord.y, surface[1].texcoord.y, surface[2].texcoord.y,
                        surface[3].texcoord.y};
    ShadeLanes(Transpose(surface[0].normalWorld, surface[1].normalWorld, surface[2].normalWorld,
                         surface[3].normalWorld),
               Transpose(surface[0].positionWorld, surface[1].positionWorld, surface[2].positionWorld,
                         surface[3].positionWorld),
               u, v, material, frame, colors);
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
    float u[4] = {}, v[4] = {};
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
    ShadeLanes(Normalize(Transpose(normal[0], normal[1], normal[2], normal[3])),
               Transpose(position[0], position[1], position[2], position[3]), u, v, material, frame, colors);
}

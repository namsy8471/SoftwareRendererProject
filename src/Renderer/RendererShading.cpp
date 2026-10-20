#include "Renderer/Rasterizer.h"
#include "Graphics/Material.h"
#include "Graphics/Texture.h"
#include "Graphics/Light.h"

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

#pragma once
#include "Math/SRMath.h"
#include <vector>
#include <array>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <immintrin.h>

class RenderQueue;
struct MeshRenderCommand;
struct PreparedDirectionalLight;
struct PreparedSpotLight;
struct AABB;

struct Lanes3
{
    __m128 x, y, z;
};

struct ShadowCascade
{
    SRMath::mat4 viewProjection = SRMath::mat4::identity();
    float splitDepth = 0.0f; // View-space linear distance threshold
    std::vector<float> depthBuffer;
};

struct ShadowMap
{
    static constexpr int shadowMapSize = 1024;
    static constexpr int cascadeCount = 3;

    int width = shadowMapSize;
    int height = shadowMapSize;

    enum class LightType : std::uint8_t
    {
        Directional,
        Spot
    };

    LightType lightType = LightType::Directional;
    SRMath::mat4 lightViewProjection = SRMath::mat4::identity();
    std::vector<float> depthBuffer;

    // CSM (Cascaded Shadow Maps)
    bool useCascades = false;
    SRMath::mat4 cameraView = SRMath::mat4::identity();
    std::array<ShadowCascade, cascadeCount> cascades;

    ShadowMap();
    ~ShadowMap() = default;

    void Clear() noexcept;

    // Renders single shadow map (spot lights or standalone passes)
    void Render(const RenderQueue &queue, const SRMath::mat4 &lightVP);

    // Renders 3-cascade CSM for directional lights
    void RenderCascades(const RenderQueue &queue,
                        const SRMath::vec3 &lightDir,
                        const SRMath::mat4 &camView,
                        const SRMath::mat4 &camProj,
                        float shadowNear,
                        float shadowFar,
                        const AABB &sceneBounds);

    // Samples shadow visibility using 16-tap rotated Poisson disk PCF and cascade selection
    [[nodiscard]] float SampleVisibility(const SRMath::vec3 &positionWorld,
                                         const SRMath::vec3 &normalWorld,
                                         const SRMath::vec3 &lightDir,
                                         float viewDepth = -1.0f) const noexcept;

    // 4-lane SSE version of SampleVisibility for ShadeLanes
    [[nodiscard]] __m128 SampleVisibility4(const Lanes3 &normal,
                                           const Lanes3 &position,
                                           const Lanes3 &lightDir) const noexcept;
};

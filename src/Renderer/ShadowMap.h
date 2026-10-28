#pragma once
#include "Math/SRMath.h"
#include <vector>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <immintrin.h>

class RenderQueue;
struct MeshRenderCommand;
struct PreparedDirectionalLight;
struct PreparedSpotLight;

struct Lanes3
{
    __m128 x, y, z;
};

struct ShadowMap
{
    static constexpr int shadowMapSize = 1024;
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

    ShadowMap();
    ~ShadowMap() = default;

    void Clear() noexcept;

    // Renders all fill mesh commands from the queue into the depth buffer using lightViewProjection.
    void Render(const RenderQueue &queue, const SRMath::mat4 &lightVP);

    // Samples 2x2 bilinear PCF shadow visibility at a world-space position.
    // Returns 1.0f (fully lit) to 0.0f (fully shadowed).
    [[nodiscard]] float SampleVisibility(const SRMath::vec3 &positionWorld,
                                         const SRMath::vec3 &normalWorld,
                                         const SRMath::vec3 &lightDir) const noexcept;

    // 4-lane SSE version of SampleVisibility for ShadeLanes.
    [[nodiscard]] __m128 SampleVisibility4(const Lanes3 &normal,
                                           const Lanes3 &position,
                                           const Lanes3 &lightDir) const noexcept;
};

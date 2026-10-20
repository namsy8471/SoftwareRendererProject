#include "Renderer/DebugGizmos.h"
#include "Renderer/RenderQueue.h"
#include "Graphics/Light.h"

namespace sr::debug
{
namespace
{
constexpr float kLightGizmoLength = 1.5f;
constexpr float kArrowHeadLength = 0.3f;
constexpr float kArrowHeadWidth = 0.18f;
void addDebugLine(DebugPrimitiveCommand &command, const SRMath::vec3 &from, const SRMath::vec3 &to,
                  const SRMath::vec4 &color)
{
    command.vertices.push_back({from, color});
    command.vertices.push_back({to, color});
}

} // namespace
void SubmitDirectionalLightGizmos(RenderQueue &renderQueue, std::span<const DirectionalLight> lights)
{
    for (std::size_t index = 0; index < lights.size(); ++index)
    {
        const auto &light = lights[index];
        const SRMath::vec3 rayDirection = SRMath::normalize(light.rayDirection);
        const SRMath::vec3 anchor{-3.0f + static_cast<float>(index) * 3.0f, 3.0f, 0.0f};
        const SRMath::vec3 tip = anchor + rayDirection * kLightGizmoLength;
        const SRMath::vec3 up =
            std::abs(rayDirection.y) > 0.9f ? SRMath::vec3{1.0f, 0.0f, 0.0f} : SRMath::vec3{0.0f, 1.0f, 0.0f};
        const SRMath::vec3 side = SRMath::normalize(SRMath::cross(rayDirection, up));

        DebugPrimitiveCommand command;
        command.worldTransform = SRMath::mat4::identity();
        command.type = DebugPrimitiveType::Line;

        // 노란색: DirectionalLight가 정의한 실제 광선 진행 방향.
        const SRMath::vec4 rayColor{1.0f, 0.85f, 0.0f, 1.0f};
        addDebugLine(command, anchor, tip, rayColor);
        addDebugLine(command, tip, tip - rayDirection * kArrowHeadLength + side * kArrowHeadWidth, rayColor);
        addDebugLine(command, tip, tip - rayDirection * kArrowHeadLength - side * kArrowHeadWidth, rayColor);

        // 청록색: Phong 난반사 계산에 사용되는 surface-to-light 방향.
        const SRMath::vec4 shadingColor{0.0f, 0.85f, 1.0f, 1.0f};
        addDebugLine(command, anchor, anchor - rayDirection * (kLightGizmoLength * 0.55f), shadingColor);

        renderQueue.Submit(std::move(command));
    }
}
} // namespace sr::debug

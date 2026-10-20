#include "Renderer/DebugGizmos.h"
#include "Renderer/RenderQueue.h"
#include "Graphics/Light.h"
#include "Graphics/Mesh.h"
#include "Math/AABB.h"

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

DebugPrimitiveCommand MakeAABBGizmo(const AABB &worldBounds)
{
    // AABB 8개 꼭짓점 계산
    const auto vertices = worldBounds.Corners();
    const SRMath::vec4 color = SRMath::vec4(1.0f, 0.0f, 0.0f, 1.0f); // 빨간색

    DebugPrimitiveCommand command;
    // 12개의 엣지를 선분으로 추가 (Bottom/Top/Sides)
    command.vertices.insert(command.vertices.end(),
                            {
                                {vertices[0], color}, {vertices[1], color}, {vertices[1], color},
                                {vertices[3], color}, {vertices[3], color}, {vertices[2], color},
                                {vertices[2], color}, {vertices[0], color}, // Bottom
                                {vertices[4], color}, {vertices[5], color}, {vertices[5], color},
                                {vertices[7], color}, {vertices[7], color}, {vertices[6], color},
                                {vertices[6], color}, {vertices[4], color}, // Top
                                {vertices[0], color}, {vertices[4], color}, {vertices[1], color},
                                {vertices[5], color}, {vertices[2], color}, {vertices[6], color},
                                {vertices[3], color}, {vertices[7], color} // Sides
                            });
    command.worldTransform = SRMath::mat4::identity(); // 이미 월드로 변환된 좌표이므로 단위행렬
    command.type = DebugPrimitiveType::Line;
    return command;
}

void AppendNormalLines(std::vector<DebugVertex> &lines, std::span<const Vertex> vertices,
                       const SRMath::mat4 &world, const SRMath::mat4 &normalMatrix)
{
    constexpr float normalLength = 0.1f;

    for (const auto &vertex : vertices)
    {
        const SRMath::vec3 startWorld{world * vertex.position};

        // 비균등 스케일에서도 표면에 수직이 되도록 법선은 역전치 행렬로 변환한다.
        // (w=0으로 설정하여 방향 벡터임을 명시)
        const SRMath::vec3 normalWorld{normalMatrix * SRMath::vec4(vertex.normal, 0.f)};
        const SRMath::vec3 endWorld = startWorld + SRMath::normalize(normalWorld) * normalLength;

        lines.push_back({startWorld, SRMath::vec4(1.0f, 1.0f, 0.0f, 1.0f)});
        lines.push_back({endWorld, SRMath::vec4(1.0f, 1.0f, 0.0f, 1.0f)});
    }
}
} // namespace sr::debug

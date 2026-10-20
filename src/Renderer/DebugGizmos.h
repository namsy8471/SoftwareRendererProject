#pragma once
#include <span>
#include <vector>
#include "Math/SRMath.h"
#include "Renderer/RenderCommand.h"

struct AABB;
struct DirectionalLight;
struct Vertex;
class RenderQueue;
namespace sr::debug
{
// 명령이 월드 좌표 정점을 소유한다. lights는 이 호출 중에만 참조한다.
void SubmitDirectionalLightGizmos(RenderQueue &queue, std::span<const DirectionalLight> lights);

// 월드 AABB의 12개 모서리를 빨간 선으로 그리는 오버레이 명령.
[[nodiscard]] DebugPrimitiveCommand MakeAABBGizmo(const AABB &worldBounds);

// 정점마다 (시작, 끝) 두 점을 lines 끝에 붙인다. 끝점은 법선 방향으로 0.1 월드 단위다.
// normalMatrix는 world의 역전치라 비균등 스케일에서도 선이 표면에 수직이다.
void AppendNormalLines(std::vector<DebugVertex> &lines, std::span<const Vertex> vertices,
                       const SRMath::mat4 &world, const SRMath::mat4 &normalMatrix);
} // namespace sr::debug

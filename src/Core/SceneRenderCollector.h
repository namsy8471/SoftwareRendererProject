#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <vector>
#include "Math/SRMath.h"
#include "Renderer/RenderCommand.h"

class Frustum;
class GameObject;
class RenderQueue;
struct DebugFlags;
struct Mesh;
namespace sr::anim
{
class SkinnedMesh;
}

// 장면 → RenderQueue 변환을 한곳에 모은다. Scene/Graphics는 렌더 명령과 DebugFlags를 모르고,
// 이 클래스만 가시성 결과(GameObject AABB, Octree 노드)를 MeshRenderCommand와 디버그 선으로 바꾼다.
// 명령 순서: 루트 순서 → 객체 전위(자기 → 자식) → 메시 순서 → 옥트리 전위. 디버그 명령은 객체마다
// 메시 명령 뒤에 붙는다. worker 수와 관계없이 순서가 같아 pixel_hash가 결정적이다.
// 스킨 메시는 Octree 대신 이번 프레임 변형 정점의 AABB로 통째 컬링하고, primitive마다 전체 인덱스를 그리는
// 명령 하나(world = 단위 행렬, 정점은 SkinnedMesh의 월드 공간 버퍼)를 낸다.
// LOD: 객체마다 카메라에 가장 가까운 깊이에서 "로컬 단위 1이 몇 픽셀인가"를 구하고, 메시마다
// 오차(Mesh::lods[i].error)가 lodErrorPixels 이하로 보이는 가장 거친 단계를 고른다. 화면 크기 기준이라
// 해상도·시야각·객체 크기·스케일이 모두 반영되고, 카메라가 상자 안이나 뒤에 있으면 원본을 쓴다.
class SceneRenderCollector
{
  public:
    // 화면 오차 기준(픽셀). 0.5픽셀이면 단순화로 생긴 표면 이동이 픽셀 중심 판정을 거의 바꾸지 않는다.
    static constexpr float lodErrorPixels = 0.5f;

    // Collect 전에 부른다. pixelsPerUnitAtUnitDepth = 깊이 1에서 월드 길이 1의 픽셀 수(= P[1][1] * 높이 / 2).
    // 0 이하이면 LOD를 쓰지 않는다(기본값).
    void SetLodView(const SRMath::mat4 &viewProjection, float pixelsPerUnitAtUnitDepth) noexcept
    {
        m_viewProjection = viewProjection;
        m_pixelsPerUnitAtUnitDepth = pixelsPerUnitAtUnitDepth;
    }

    void Collect(std::span<const std::shared_ptr<GameObject>> roots, const Frustum &frustum,
                 const DebugFlags &flags, RenderQueue &queue);
    // object와 그 자식 전체를 제출한다.
    void Collect(const GameObject &object, const Frustum &frustum, const DebugFlags &flags, RenderQueue &queue);

  private:
    struct VisibleMeshTask
    {
        const GameObject *object = nullptr;
        const Mesh *mesh = nullptr;
        float allowedLodError = 0.f; // 메시 로컬 단위. 0이면 원본(LOD0)
    };
    SRMath::mat4 m_viewProjection = SRMath::mat4::identity();
    float m_pixelsPerUnitAtUnitDepth = 0.f;
    [[nodiscard]] float allowedLodError(const GameObject &object) const noexcept;
    std::vector<VisibleMeshTask> m_visibleMeshTasks;
    // 각 병렬 작업은 자기 메시의 batch만 쓴다. 프레임과 객체 사이에 용량을 재사용하려고
    // 크기를 줄이지 않고 앞쪽 meshCount개만 쓴다.
    struct MeshCommandBatch
    {
        std::vector<MeshRenderCommand> commands;
        std::vector<DebugPrimitiveCommand> debugCommands;
        std::vector<DebugVertex> normalLines; // RenderQueue에서 받은 재사용 저장소
    };
    std::vector<MeshCommandBatch> m_batches;

    void traverseObject(const GameObject &object, const Frustum &frustum, const DebugFlags &flags,
                        RenderQueue &queue);
    void processVisibleMeshTasks(const Frustum &frustum, const DebugFlags &flags,
                          RenderQueue &queue);
    static void collectSkinnedMeshes(const sr::anim::SkinnedMesh &skinned, const DebugFlags &flags,
                                     RenderQueue &queue);
    static void collectMeshCommands(const GameObject &object, const Mesh &mesh, float allowedLodError,
                                    const Frustum &frustum, const DebugFlags &flags, MeshCommandBatch &batch);
};

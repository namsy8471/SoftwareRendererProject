#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <vector>
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
class SceneRenderCollector
{
  public:
    void Collect(std::span<const std::shared_ptr<GameObject>> roots, const Frustum &frustum,
                 const DebugFlags &flags, RenderQueue &queue);
    // object와 그 자식 전체를 제출한다.
    void Collect(const GameObject &object, const Frustum &frustum, const DebugFlags &flags, RenderQueue &queue);

  private:
    struct VisibleMeshTask
    {
        const GameObject *object = nullptr;
        const Mesh *mesh = nullptr;
    };
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
    static void collectMeshCommands(const GameObject &object, const Mesh &mesh, const Frustum &frustum,
                                    const DebugFlags &flags, MeshCommandBatch &batch);
};

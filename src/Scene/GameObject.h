#pragma once

#include <vector>
#include <memory>
#include <span>
#include "Math/AABB.h"
#include "Math/SRMath.h"
#include "Renderer/RenderQueue.h"

class Model;
class RenderQueue;
class Frustum;
struct DebugFlags;
struct Mesh;

class GameObject : public std::enable_shared_from_this<GameObject>
{
  private:
    SRMath::vec3 m_position;
    SRMath::vec3 m_rotation;
    SRMath::vec3 m_scale;

    SRMath::mat4 m_worldMatrix;  // 월드 변환 행렬
    SRMath::mat4 m_normalMatrix; // 법선 행렬 (역전치 행렬)

    // Model
    std::unique_ptr<Model> m_model;
    AABB m_worldAABB; // 월드 공간에서의 AABB

    // Hierarchy
    std::weak_ptr<GameObject> m_parent;
    std::vector<std::shared_ptr<GameObject>> m_children;

    // worker 등록 순서가 아닌 모델의 메시 순서로 병합한다. 각 작업은 자기
    // batch만 쓰고, 프레임 사이에는 벡터 용량을 유지해 할당을 재사용한다.
    struct MeshCommandBatch
    {
        std::vector<MeshRenderCommand> commands;
        std::vector<DebugPrimitiveCommand> debugCommands;
    };
    std::vector<MeshCommandBatch> m_meshCommandBatches;

    void submitOwnMeshes(RenderQueue &queue, const Frustum &frustum, const DebugFlags &flags);
    void collectMeshCommands(const Mesh &mesh, const Frustum &frustum, const DebugFlags &flags,
                             std::vector<MeshRenderCommand> &commands,
                             std::vector<DebugPrimitiveCommand> &debugCommands);
    void mergeCommands(RenderQueue &queue);

  public:
    GameObject(const SRMath::vec3 &position, const SRMath::vec3 &rotation, const SRMath::vec3 &scale,
               std::unique_ptr<Model> model);
    ~GameObject();

    GameObject(GameObject &&) = delete;
    GameObject &operator=(GameObject &&) = delete;
    GameObject(const GameObject &) = delete;
    GameObject &operator=(const GameObject &) = delete;

    void Update(float deltaTime, bool isRotate);
    void UpdateTransform(float deltaTime, bool isRotate); // Transform 업데이트 시 호출될 함수

    // 위치/회전/크기는 부모 기준 로컬 값이며 world AABB는 부모 변환까지 포함한다.
    [[nodiscard]] SRMath::vec3 GetPosition() const noexcept { return m_position; }
    [[nodiscard]] SRMath::vec3 GetRotation() const noexcept { return m_rotation; }
    [[nodiscard]] SRMath::vec3 GetScale() const noexcept { return m_scale; }
    [[nodiscard]] const Model *GetModel() const noexcept { return m_model.get(); }
    [[nodiscard]] const AABB &GetWorldAABB() const noexcept { return m_worldAABB; }
    [[nodiscard]] std::weak_ptr<GameObject> GetParent() const noexcept { return m_parent; }
    [[nodiscard]] std::span<const std::shared_ptr<GameObject>> GetChildren() const noexcept
    {
        return m_children;
    }

    // shared_ptr로 관리되는 부모만 연결할 수 있다. null/중복/순환/기존 부모는 거부한다.
    [[nodiscard]] bool AddChild(std::shared_ptr<GameObject> child);

    // Rendering
    void SubmitToRenderQueue(RenderQueue &renderQueue, const Frustum &frustum, const DebugFlags &debugFlags);
};

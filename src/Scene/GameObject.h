#pragma once

#include <vector>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include "Math/AABB.h"
#include "Math/SRMath.h"

class Model;
namespace sr::anim
{
class SkinnedMesh;
}

// 씬 파일과 런타임 물체를 잇는 편집용 정보. 렌더링·갱신은 읽지 않는다.
// 프로젝트 밖에서 만든 물체(데모 장면, 프로젝트 없이 드롭한 모델)는 id가 비어 있다.
struct EntityInfo
{
    std::string id;        // 씬 안에서 고유. 부모 참조와 이후 애니메이션 대상 지정에 쓴다
    std::string name;      // 사람이 읽는 이름. 고유하지 않아도 된다
    std::string meshAsset; // 프로젝트 에셋 표의 id. 모델 없는 그룹 노드는 빈 문자열
};

class GameObject : public std::enable_shared_from_this<GameObject>
{
  private:
    SRMath::vec3 m_position;
    SRMath::vec3 m_rotation;
    SRMath::vec3 m_scale;
    // 애니메이션이 쓰는 회전. m_useOrientation이면 m_rotation(축 * 각도) 대신 이 쿼터니언으로 행렬을 만든다.
    SRMath::quat m_orientation;
    bool m_useOrientation = false;

    SRMath::mat4 m_worldMatrix;  // 월드 변환 행렬
    SRMath::mat4 m_normalMatrix; // 법선 행렬 (역전치 행렬)

    // Model
    std::unique_ptr<Model> m_model;
    AABB m_worldAABB; // 월드 공간에서의 AABB
    // 스킨 메시면 렌더는 Model 정점 대신 이 변형 결과를 쓴다(SceneRenderCollector).
    std::unique_ptr<sr::anim::SkinnedMesh> m_skinnedMesh;

    // Hierarchy
    std::weak_ptr<GameObject> m_parent;
    std::vector<std::shared_ptr<GameObject>> m_children;

    EntityInfo m_entityInfo;

  public:
    // model이 null이면 그리지 않는 변환 노드다(프로젝트 파일의 그룹 엔티티 등).
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
    // 로컬 값만 바꾼다. 행렬과 AABB는 다음 Update(Scene::Update)에서 다시 계산된다.
    void SetPosition(const SRMath::vec3 &position) noexcept { m_position = position; }
    void SetRotation(const SRMath::vec3 &rotation) noexcept
    {
        m_rotation = rotation;
        m_useOrientation = false;
    }
    // 쿼터니언 회전(애니메이션). 다음 SetRotation까지 m_rotation과 데모 자동 회전보다 우선한다.
    void SetOrientation(const SRMath::quat &orientation) noexcept
    {
        m_orientation = orientation;
        m_useOrientation = true;
    }
    [[nodiscard]] SRMath::quat GetOrientation() const noexcept
    {
        return m_useOrientation ? m_orientation : SRMath::from_rotation_vector(m_rotation);
    }
    [[nodiscard]] bool HasOrientation() const noexcept { return m_useOrientation; }
    [[nodiscard]] bool UsesOrientation() const noexcept { return m_useOrientation; }
    void SetScale(const SRMath::vec3 &scale) noexcept { m_scale = scale; }
    [[nodiscard]] const Model *GetModel() const noexcept { return m_model.get(); }
    // null이 아니면 스킨 메시다. 소유권은 이 물체에 있고 관절은 같은 계층의 다른 GameObject다.
    [[nodiscard]] sr::anim::SkinnedMesh *GetSkinnedMesh() noexcept { return m_skinnedMesh.get(); }
    [[nodiscard]] const sr::anim::SkinnedMesh *GetSkinnedMesh() const noexcept { return m_skinnedMesh.get(); }
    void SetSkinnedMesh(std::unique_ptr<sr::anim::SkinnedMesh> skinnedMesh) noexcept;
    [[nodiscard]] const EntityInfo &GetEntityInfo() const noexcept { return m_entityInfo; }
    void SetEntityInfo(EntityInfo info) noexcept { m_entityInfo = std::move(info); }
    [[nodiscard]] const AABB &GetWorldAABB() const noexcept { return m_worldAABB; }
    // 열벡터 규약의 world = parentWorld * T * R * S. normal은 그 역전치(특이하면 단위 행렬)다.
    [[nodiscard]] const SRMath::mat4 &GetWorldMatrix() const noexcept { return m_worldMatrix; }
    [[nodiscard]] const SRMath::mat4 &GetNormalMatrix() const noexcept { return m_normalMatrix; }
    [[nodiscard]] std::weak_ptr<GameObject> GetParent() const noexcept { return m_parent; }
    [[nodiscard]] std::span<const std::shared_ptr<GameObject>> GetChildren() const noexcept
    {
        return m_children;
    }

    // shared_ptr로 관리되는 부모만 연결할 수 있다. null/중복/순환/기존 부모는 거부한다.
    [[nodiscard]] bool AddChild(std::shared_ptr<GameObject> child);
};

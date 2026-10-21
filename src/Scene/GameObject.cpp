#include "GameObject.h"
#include "Graphics/Model.h"

#include <utility>

GameObject::GameObject(const SRMath::vec3 &position, const SRMath::vec3 &rotation, const SRMath::vec3 &scale,
                       std::unique_ptr<Model> model)
    : m_position(position), m_rotation(rotation), m_scale(scale), m_model(std::move(model))
{
    UpdateTransform(0.f, false);
}

GameObject::~GameObject() = default;

void GameObject::Update(float deltaTime, bool isRotate)
{
    UpdateTransform(deltaTime, isRotate);

    // 데모 자동 회전은 이 물체에만 준다. 자식은 부모 행렬을 따라 함께 돌므로 자기 회전까지 더하면
    // 계층 모델(glTF)의 관절이 제각각 돌아간다.
    for (const auto &child : m_children)
    {
        if (child)
        {
            child->Update(deltaTime, false);
        }
    }
}

void GameObject::UpdateTransform(float deltaTime, bool isRotate)
{
    // 열벡터 규약: local = T * R * S, world = parentWorld * local.
    if (isRotate)
        m_rotation += SRMath::vec3(0.0f, 0.4f * deltaTime, 0.0f); // 초당 0.4 rad 회전

    const SRMath::mat4 scaleMatrix = SRMath::scale(m_scale);
    const SRMath::mat4 rotationMatrix = SRMath::rotate(m_rotation);
    const SRMath::mat4 translationMatrix = SRMath::translate(m_position);
    m_worldMatrix = translationMatrix * rotationMatrix * scaleMatrix;
    if (const auto parent = m_parent.lock())
        m_worldMatrix = parent->m_worldMatrix * m_worldMatrix;

    // scale에 0이 있으면 역행렬이 존재하지 않는다. expected를 확인해 실패
    // 경로를 눈에 보이게 하고, 렌더링을 계속할 때만 단위 행렬을 사용한다.
    if (const auto normalMatrix = SRMath::inverse_transpose(m_worldMatrix))
        m_normalMatrix = *normalMatrix;
    else
        m_normalMatrix = SRMath::mat4::identity();

    // 모델 없는 노드(그룹·광원 자리)는 빈 AABB라 컬링·카메라 맞춤에서 빠진다.
    m_worldAABB = m_model ? m_model->GetLocalAABB().Transform(m_worldMatrix) : AABB{};
}

bool GameObject::AddChild(std::shared_ptr<GameObject> child)
{
    const auto self = weak_from_this().lock();
    if (!self || !child || !child->m_parent.expired())
        return false;
    for (auto ancestor = self; ancestor; ancestor = ancestor->m_parent.lock())
        if (ancestor == child)
            return false;
    m_children.push_back(child);
    child->m_parent = self;
    child->Update(0.f, false);
    return true;
}

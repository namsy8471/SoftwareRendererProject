#include "Scene/Scene.h"
#include "Animation/Animator.h"
#include "Scene/GameObject.h"

#include <utility>

Scene::Scene() = default;
Scene::~Scene() = default;
Scene::Scene(Scene &&) noexcept = default;
Scene &Scene::operator=(Scene &&) noexcept = default;

bool Scene::AddObject(std::shared_ptr<GameObject> object, const std::shared_ptr<GameObject> &parent)
{
    if (!object || !object->GetParent().expired())
        return false;
    if (parent)
        return parent->AddChild(std::move(object));
    for (const auto &root : m_rootObjects)
        if (root == object)
            return false;
    m_rootObjects.push_back(std::move(object));
    return true;
}

void Scene::AddAnimator(std::shared_ptr<sr::anim::Animator> animator)
{
    if (animator)
        m_animators.push_back(std::move(animator));
}

void Scene::Update(float deltaTime, float aspectRatio, std::span<const bool, keyCount> keys)
{
    m_camera.Update(deltaTime, keys, aspectRatio);
    // 포즈는 로컬 TRS만 바꾸므로 transform 갱신 전에, 스킨은 관절 world 행렬이 필요하므로 그 뒤에 한다.
    for (const auto &animator : m_animators)
        animator->ApplyPose(deltaTime);
    // GameObject::Update가 자식까지 내려가므로 부모 행렬이 항상 먼저 갱신된다.
    for (const auto &object : m_rootObjects)
        if (object)
            object->Update(deltaTime, m_isRotateMode);
    for (const auto &animator : m_animators)
        animator->UpdateSkins();
}

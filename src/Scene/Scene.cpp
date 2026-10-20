#include "Scene/Scene.h"
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

void Scene::Update(float deltaTime, float aspectRatio, std::span<const bool, keyCount> keys)
{
    m_camera.Update(deltaTime, keys, aspectRatio);
    // GameObject::Update가 자식까지 내려가므로 부모 행렬이 항상 먼저 갱신된다.
    for (const auto &object : m_rootObjects)
        if (object)
            object->Update(deltaTime, m_isRotateMode);
}

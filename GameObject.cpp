#include "GameObject.h"
#include "Model.h"

GameObject::GameObject()
	: m_position(SRMath::vec3()), m_rotation(SRMath::vec3()), m_scale(SRMath::vec3()),
	m_model(nullptr), m_parent(), m_sons()
{
}

GameObject::GameObject(std::unique_ptr<GameObject> parent)
{
	GameObject();
}

GameObject::GameObject(GameObject&& move)
{
	
}

GameObject::~GameObject()
{
}

const SRMath::vec3 const GameObject::GetPosition() const
{
	return m_position;
}

const SRMath::vec3 const GameObject::GetRotation() const
{
	return m_rotation;
}

const SRMath::vec3 const GameObject::GetScale() const
{
	return m_scale;
}

const std::unique_ptr<Model>& GameObject::GetModel() const
{
	return m_model;
}

const std::weak_ptr<GameObject>& GameObject::Getparent() const
{
	return m_parent;
}

const std::vector<std::shared_ptr<GameObject>>& GameObject::Getsons() const
{
	return m_sons;
}


void GameObject::SetSon(std::shared_ptr<GameObject> son)
{
	//son->m_parent = this->shared_from_this();
	m_sons.emplace_back(std::move(son));
}



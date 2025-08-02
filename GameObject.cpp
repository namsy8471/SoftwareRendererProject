#include "GameObject.h"
#include "Model.h"

GameObject::GameObject() = default;
GameObject::~GameObject() = default;

GameObject::GameObject(GameObject&& move) noexcept = default;
GameObject& GameObject::operator=(GameObject&&) noexcept = default;


bool GameObject::Initialize(const SRMath::vec3& position, const SRMath::vec3& rotation, const SRMath::vec3& scale, std::unique_ptr<Model> model)
{
	m_position = position;
	m_rotation = rotation;
	m_scale = scale;
	m_model = std::move(model);
	if (!m_model)
	{
		// Handle error: model is null
		return false;
	}

	return true;
}

void GameObject::Update(float deltaTime)
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



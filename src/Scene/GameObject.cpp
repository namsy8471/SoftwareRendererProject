
#include "GameObject.h"
#include "Graphics/Model.h"
#include "Renderer/RenderQueue.h"
#include "Math/Frustum.h"
#include "Graphics/Octree.h"

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
	UpdateTransform();

	for(const auto& son : m_sons)
	{
		if (son)
		{
			son->Update(deltaTime);
		}
	}
}

void GameObject::UpdateTransform()
{
	// World Transform
	SRMath::mat4 scaleMatrix = SRMath::scale(m_scale);
	SRMath::mat4 rotationMatrix = SRMath::rotate(m_rotation);
	SRMath::mat4 translationMatrix = SRMath::translate(m_position);
	m_worldMatrix = translationMatrix * rotationMatrix * scaleMatrix;

	const AABB& localAABB = m_model->GetLocalAABB();
	m_worldAABB = AABB::TransformAABB(localAABB, m_worldMatrix);
}

const SRMath::vec3 GameObject::GetPosition() const
{
	return m_position;
}

const SRMath::vec3 GameObject::GetRotation() const
{
	return m_rotation;
}

const SRMath::vec3 GameObject::GetScale() const
{
	return m_scale;
}

const std::unique_ptr<Model>& GameObject::GetModel() const
{
	return m_model;
}

const AABB& GameObject::GetWorldAABB() const
{
	return m_worldAABB;
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

void GameObject::SubmitToRenderQueue(RenderQueue& renderQueue, const Frustum& frustum)
{
	if (!frustum.IsAABBInFrustum(m_worldAABB)) return;
	if (!m_model) return;

	for (const auto& mesh : m_model->GetMeshes())
	{
		if (mesh.octree)
		{
			mesh.octree->SubmitVisibleNodes(renderQueue, frustum, m_worldMatrix);
		}
		else
		{
			MeshRenderCommand cmd;
			cmd.sourceMesh = &mesh;
			cmd.indicesToDraw = &mesh.indices;
			cmd.worldTransform = m_worldMatrix;
			renderQueue.Submit(cmd);
		}
	}

	for (const auto& son : m_sons)
	{
		if (son)
		{
			son->SubmitToRenderQueue(renderQueue, frustum);
		}
	}
}



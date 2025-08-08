#include "GameObject.h"
#include "Graphics/Model.h"
#include "Renderer/RenderQueue.h"
#include "Math/Frustum.h"
#include "Graphics/Octree.h"
#include "Utils/DebugUtils.h"

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

const std::weak_ptr<GameObject>& GameObject::GetParent() const
{
	return m_parent;
}

const std::vector<std::shared_ptr<GameObject>>& GameObject::GetSons() const
{
	return m_sons;
}


void GameObject::SetSon(std::shared_ptr<GameObject> son)
{
	//son->m_parent = this->shared_from_this();
	m_sons.emplace_back(std::move(son));
}

void GameObject::SubmitToRenderQueue(RenderQueue& renderQueue, const Frustum& frustum, const DebugFlags& debugFlags)
{
	if (!frustum.IsAABBInFrustum(m_worldAABB)) return;
	if (!m_model) return;

	for (const auto& mesh : m_model->GetMeshes())
	{
		if (mesh.octree)
		{
			mesh.octree->SubmitNodesToRenderQueue(renderQueue, frustum, m_worldMatrix, debugFlags);
		}
		else
		{
			MeshRenderCommand cmd;
			cmd.sourceMesh = &mesh;
			cmd.indicesToDraw = &mesh.indices;
			cmd.worldTransform = m_worldMatrix;

			if(debugFlags.bShowWireframe)
			{
				cmd.rasterizeMode = ERasterizeMode::Wireframe;
			}
			else
			{
				cmd.rasterizeMode = ERasterizeMode::Fill;
			}

			renderQueue.Submit(cmd);
		}

		if (debugFlags.bShowNormal)
		{
			std::vector<DebugVertex> normalLines;

			SRMath::mat4 normalMatrix = SRMath::inverse_transpose(m_worldMatrix).value_or(SRMath::mat4(1.f));

			for (const auto& vertex : mesh.vertices)
			{
				// 1. 위치(position)는 월드 행렬(m_worldMatrix)로 변환하여 월드 공간의 시작점 계산
				SRMath::vec3 startPoint_world = m_worldMatrix * vertex.position;

				// 2. 방향(normal)은 법선 행렬(normalMatrix)로 변환하여 월드 공간의 법선 방향 계산
				SRMath::vec3 normalDir_world = normalMatrix * SRMath::vec4(vertex.normal, 0.f);

				// 3. 월드 공간의 시작점에 월드 공간의 법선 방향을 더하여 끝점 계산
				SRMath::vec3 endPoint_world = startPoint_world + SRMath::normalize(normalDir_world) * 0.1f;
				
				normalLines.push_back({ startPoint_world, SRMath::vec4(1.0f, 1.0f, 0.0f, 1.0f) });
				normalLines.push_back({ endPoint_world, SRMath::vec4(1.0f, 1.0f, 0.0f, 1.0f) });
			}

			if (!normalLines.empty()) {

				DebugPrimitiveCommand cmd;
				cmd.vertices = std::move(normalLines);
				cmd.worldTransform = SRMath::mat4(1.f);
				cmd.type = DebugPrimitiveType::Line;
				renderQueue.Submit(cmd);
			}
		}
	}

	for (const auto& son : m_sons)
	{
		if (son)
		{
			son->SubmitToRenderQueue(renderQueue, frustum, debugFlags);
		}
	}
}



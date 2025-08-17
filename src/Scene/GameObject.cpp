#include "GameObject.h"
#include "Graphics/Model.h"
#include "Renderer/RenderQueue.h"
#include "Math/Frustum.h"
#include "Graphics/Octree.h"
#include "Utils/DebugUtils.h"
#include <omp.h>

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

void GameObject::Update(float deltaTime, bool isRotate)
{
	UpdateTransform(deltaTime, isRotate);

	for(const auto& son : m_sons)
	{
		if (son)
		{
			son->Update(deltaTime, isRotate);
		}
	}
}

void GameObject::UpdateTransform(float deltaTime, bool isRotate)
{
	// World Transform
	if(isRotate) m_rotation += SRMath::vec3(0.0f, 0.4f * deltaTime, 0.0f); // Example rotation update, can be customized

	SRMath::mat4 scaleMatrix = SRMath::scale(m_scale);
	SRMath::mat4 rotationMatrix = SRMath::rotate(m_rotation);
	SRMath::mat4 translationMatrix = SRMath::translate(m_position);
	m_worldMatrix = translationMatrix * rotationMatrix * scaleMatrix;
	m_normalMatrix = SRMath::inverse_transpose(m_worldMatrix).value_or(SRMath::mat4(1.f)); // ���� ��� ���

	const AABB& localAABB = m_model->GetLocalAABB();
	m_worldAABB = localAABB.Transform(m_worldMatrix);

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

	int modelSize = m_model->GetMeshes().size();
	std::vector<std::vector<MeshRenderCommand>> threadLocalCmd;
	std::vector<std::vector<DebugPrimitiveCommand>> threadLocalDebugCmd;

#pragma omp parallel
	{
		#pragma omp single
		{
			threadLocalCmd.resize(omp_get_num_threads());
			threadLocalDebugCmd.resize(omp_get_num_threads());
		}

		std::vector<MeshRenderCommand> localCmd;
		std::vector<DebugPrimitiveCommand> localDebugCmd;
		int threadId = omp_get_thread_num();
			
		#pragma omp for schedule(dynamic)
		for (int i = 0; i < modelSize; i++)
		{
			const auto& mesh = m_model->GetMeshes()[i];
			if (mesh.octree)
			{
				mesh.octree->SubmitNodesToRenderQueue(renderQueue, frustum, m_worldMatrix,
					threadId, localCmd, localDebugCmd, debugFlags);
			}
			else
			{
				MeshRenderCommand cmd;
				cmd.sourceMesh = &mesh;
				cmd.indicesToDraw = &mesh.indices;
				cmd.worldTransform = m_worldMatrix;
				cmd.material = &mesh.material;

				if (debugFlags.bShowWireframe)
				{
					cmd.rasterizeMode = ERasterizeMode::Wireframe;
				}
				else
				{
					cmd.rasterizeMode = ERasterizeMode::Fill;
				}

				// renderQueue.Submit(cmd);
				localCmd.push_back(cmd);
			}

			if (debugFlags.bShowNormal)
			{
				std::vector<DebugVertex> normalLines;
				normalLines.reserve(mesh.vertices.size() * 2); // �� �������� �������� ������ �����Ƿ� 2�� ũ��

				const float normalLength = 0.1f; // Normal vector length for visualization

				for (const auto& vertex : mesh.vertices)
				{
					SRMath::vec3 startPoint_local = m_worldMatrix * vertex.position;

					// 3. ����(normal)�� ����ġ ��ķ� ��ȯ�Ͽ� ���� ������ ���� ������ ����մϴ�.
					// (w=0���� �����Ͽ� ���� �������� ���)
					SRMath::vec3 normalDir_world = m_normalMatrix * SRMath::vec4(vertex.normal, 0.f);
					SRMath::vec3 endPoint_local = startPoint_local + SRMath::normalize(normalDir_world) * normalLength;

					normalLines.push_back({ startPoint_local, SRMath::vec4(1.0f, 1.0f, 0.0f, 1.0f) });
					normalLines.push_back({ endPoint_local, SRMath::vec4(1.0f, 1.0f, 0.0f, 1.0f) });
				}

				if (!normalLines.empty()) {

					DebugPrimitiveCommand cmd;
					cmd.vertices = std::move(normalLines);
					cmd.worldTransform = SRMath::mat4(1.f);
					cmd.type = DebugPrimitiveType::Line;
					//renderQueue.Submit(cmd);
					localDebugCmd.push_back(cmd);
				}
			}
		}

		threadLocalCmd[threadId] = std::move(localCmd);
		threadLocalDebugCmd[threadId] = std::move(localDebugCmd);
	}

	for(const auto& localCmd : threadLocalCmd)
	{
		for (const auto& cmd : localCmd)
		{
			renderQueue.Submit(cmd);
		}
	}

	for (const auto& localDebugCmd : threadLocalDebugCmd)
	{
		for (const auto& cmd : localDebugCmd)
		{
			renderQueue.Submit(cmd);
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



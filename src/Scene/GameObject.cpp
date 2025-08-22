#include "GameObject.h"
#include "Graphics/Model.h"
#include "Math/Frustum.h"
#include "Graphics/Octree.h"
#include "Utils/DebugUtils.h"

#include <stdexcept>
#include <tbb/tbb.h>

GameObject::GameObject(const SRMath::vec3& position, const SRMath::vec3& rotation, const SRMath::vec3& scale, std::unique_ptr<Model> model)
{
	m_position = position;
	m_rotation = rotation;
	m_scale = scale;
	m_model = std::move(model);
	if (!m_model)
	{
		// Handle error: model is null
		throw std::runtime_error("Model cannot be null");
	}

	int maxThreads = tbb::this_task_arena::max_concurrency();
	tbb::task_arena arena(maxThreads);
	arena.execute([&] {
		tbb::parallel_for(0, maxThreads, [&](int) {
			std::vector<MeshRenderCommand> localCmd = m_threadLocalCmd.local();
			std::vector<DebugPrimitiveCommand> localDebugCmd = m_threadLocalDebugCmd.local();

			localCmd.reserve(200);
			localDebugCmd.reserve(200);
			});
		});
}

GameObject::~GameObject() = default;

GameObject::GameObject(GameObject&& move) noexcept = default;
GameObject& GameObject::operator=(GameObject&&) noexcept = default;

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
	m_normalMatrix = SRMath::inverse_transpose(m_worldMatrix).value_or(SRMath::mat4(1.f)); // 법선 행렬 계산

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

	m_threadLocalCmd.clear();
	m_threadLocalDebugCmd.clear();

	tbb::parallel_for(tbb::blocked_range<int>(0, modelSize),
		[&](const tbb::blocked_range<int>& r) {

			std::vector<MeshRenderCommand>& localCmd = m_threadLocalCmd.local();
			std::vector<DebugPrimitiveCommand>& localDebugCmd = m_threadLocalDebugCmd.local();

			for (int i = r.begin(); i != r.end(); i++)
			{
				const auto& mesh = m_model->GetMeshes()[i];
				if (mesh.octree)
				{
					mesh.octree->SubmitNodesToRenderQueue(renderQueue, frustum, m_worldMatrix,
						localCmd, localDebugCmd, debugFlags);
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

					localCmd.push_back(cmd);
				}

				if (debugFlags.bShowNormal)
				{
					std::vector<DebugVertex> normalLines;
					normalLines.reserve(mesh.vertices.size() * 2); // 각 정점마다 시작점과 끝점이 있으므로 2배 크기

					const float normalLength = 0.1f; // Normal vector length for visualization

					for (const auto& vertex : mesh.vertices)
					{
						SRMath::vec3 startPoint_local = m_worldMatrix * vertex.position;

						// 3. 방향(normal)은 역전치 행렬로 변환하여 월드 공간의 법선 방향을 계산합니다.
						// (w=0으로 설정하여 방향 벡터임을 명시)
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
						localDebugCmd.push_back(cmd);
					}
				}
			}
		});

	for(const auto& localCmd : m_threadLocalCmd)
	{
		for (const auto& cmd : localCmd)
		{
			renderQueue.Submit(cmd);
		}
	}

	for (const auto& localDebugCmd : m_threadLocalDebugCmd)
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



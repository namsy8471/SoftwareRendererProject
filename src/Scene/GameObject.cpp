#include "GameObject.h"
#include "Graphics/Model.h"
#include "Math/Frustum.h"
#include "Graphics/Octree.h"
#include "Utils/DebugUtils.h"

#include <stdexcept>
#include <utility>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

GameObject::GameObject(const SRMath::vec3& position, const SRMath::vec3& rotation, const SRMath::vec3& scale, std::unique_ptr<Model> model)
	: m_position(position),
	  m_rotation(rotation),
	  m_scale(scale),
	  m_model(std::move(model))
{
	if (!m_model)
	{
		throw std::invalid_argument("GameObject requires a model");
	}
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

	const SRMath::mat4 scaleMatrix = SRMath::scale(m_scale);
	const SRMath::mat4 rotationMatrix = SRMath::rotate(m_rotation);
	const SRMath::mat4 translationMatrix = SRMath::translate(m_position);
	m_worldMatrix = translationMatrix * rotationMatrix * scaleMatrix;

	// scale에 0이 있으면 역행렬이 존재하지 않는다. expected를 확인해 실패
	// 경로를 눈에 보이게 하고, 렌더링을 계속할 때만 단위 행렬을 사용한다.
	if (const auto normalMatrix = SRMath::inverse_transpose(m_worldMatrix))
		m_normalMatrix = *normalMatrix;
	else
		m_normalMatrix = SRMath::mat4::identity();

	const AABB& localAABB = m_model->GetLocalAABB();
	m_worldAABB = localAABB.Transform(m_worldMatrix);

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

	const std::span<const Mesh> meshes = m_model->GetMeshes();

	m_threadLocalCmd.clear();
	m_threadLocalDebugCmd.clear();

	tbb::parallel_for(tbb::blocked_range<std::size_t>{ 0, meshes.size() },
		[&](const tbb::blocked_range<std::size_t>& range) {

			std::vector<MeshRenderCommand>& localCmd = m_threadLocalCmd.local();
			std::vector<DebugPrimitiveCommand>& localDebugCmd = m_threadLocalDebugCmd.local();
			// 이전 생성자 코드는 이 벡터들을 복사한 뒤 복사본만 reserve했다.
			// 실제 TLS 인스턴스의 capacity를 최초 사용 시 확보한다.
			if (localCmd.capacity() == 0) localCmd.reserve(200);
			if (localDebugCmd.capacity() == 0) localDebugCmd.reserve(200);

			for (std::size_t i = range.begin(); i != range.end(); ++i)
			{
				const Mesh& mesh = meshes[i];
				if (mesh.octree)
				{
					mesh.octree->SubmitNodesToRenderQueue(renderQueue, frustum, m_worldMatrix,
						localCmd, localDebugCmd, debugFlags);
				}
				else
				{
					localCmd.push_back(MeshRenderCommand{
						.sourceMesh = &mesh,
						.indicesToDraw = mesh.indices,
						.worldTransform = m_worldMatrix,
						.material = &mesh.material,
						.rasterizeMode = debugFlags.bShowWireframe
							? ERasterizeMode::Wireframe : ERasterizeMode::Fill
					});
				}

				if (debugFlags.bShowNormal)
				{
					std::vector<DebugVertex> normalLines;
					normalLines.reserve(mesh.vertices.size() * 2); // 각 정점마다 시작점과 끝점이 있으므로 2배 크기

					constexpr float normalLength = 0.1f;

					for (const auto& vertex : mesh.vertices)
					{
						const SRMath::vec3 startPoint_local{ m_worldMatrix * vertex.position };

						// 3. 방향(normal)은 역전치 행렬로 변환하여 월드 공간의 법선 방향을 계산합니다.
						// (w=0으로 설정하여 방향 벡터임을 명시)
						const SRMath::vec3 normalDir_world{
							m_normalMatrix * SRMath::vec4(vertex.normal, 0.f) };
						const SRMath::vec3 endPoint_local = startPoint_local + SRMath::normalize(normalDir_world) * normalLength;

						normalLines.push_back({ startPoint_local, SRMath::vec4(1.0f, 1.0f, 0.0f, 1.0f) });
						normalLines.push_back({ endPoint_local, SRMath::vec4(1.0f, 1.0f, 0.0f, 1.0f) });
					}

					if (!normalLines.empty()) {

						localDebugCmd.push_back(DebugPrimitiveCommand{
							.vertices = std::move(normalLines),
							.worldTransform = SRMath::mat4::identity(),
							.type = DebugPrimitiveType::Line
						});
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

	for (auto& localDebugCmd : m_threadLocalDebugCmd)
	{
		for (auto& cmd : localDebugCmd)
		{
			renderQueue.Submit(std::move(cmd));
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

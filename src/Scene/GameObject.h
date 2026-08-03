#pragma once

#include "Core/pch.h"
#include <vector>
#include <memory>
#include "Math/AABB.h"
#include "Math/SRMath.h"
#include "Renderer/RenderQueue.h"
#include "tbb/tbb.h"

class Model;
class RenderQueue;
class Frustum;
struct DebugFlags;

class GameObject
{
private:
	SRMath::vec3 m_position;
	SRMath::vec3 m_rotation;
	SRMath::vec3 m_scale;

	SRMath::mat4 m_worldMatrix; // ���� ��ȯ ���
	SRMath::mat4 m_normalMatrix; // ���� ��� (����ġ ���)

	// Model
	std::unique_ptr<Model> m_model;
	AABB m_worldAABB; // ���� ���������� AABB

	// Hierarchy
	std::weak_ptr<GameObject> m_parent;
	std::vector<std::shared_ptr<GameObject>> m_sons;

	tbb::enumerable_thread_specific<std::vector<MeshRenderCommand>> m_threadLocalCmd;
	tbb::enumerable_thread_specific<std::vector<DebugPrimitiveCommand>> m_threadLocalDebugCmd;

public:

	GameObject(const SRMath::vec3& position, const SRMath::vec3& rotation, const SRMath::vec3& scale, std::unique_ptr<Model> model);
	~GameObject();

	GameObject(GameObject&&) noexcept;
	GameObject& operator=(GameObject&&) noexcept;
	GameObject(const GameObject&) = delete;
	GameObject& operator=(const GameObject&) = delete;


	bool Initialize(const SRMath::vec3& position, const SRMath::vec3& rotation, const SRMath::vec3& scale, std::unique_ptr<Model> model);
	void Update(float deltaTime, bool isRotate);
	void UpdateTransform(float deltaTime, bool isRotate); // Transform ������Ʈ �� ȣ��� �Լ�

	const SRMath::vec3 GetPosition() const;
	const SRMath::vec3 GetRotation() const;
	const SRMath::vec3 GetScale() const;
	const std::unique_ptr<Model>& GetModel() const;
	const AABB& GetWorldAABB() const;
	const std::weak_ptr<GameObject>& GetParent() const;
	const std::vector<std::shared_ptr<GameObject>>& GetSons() const;

	void SetSon(std::shared_ptr<GameObject> son);

	// Rendering
	void SubmitToRenderQueue(RenderQueue& renderQueue, const Frustum& frustum, const DebugFlags& debugFlags);
};

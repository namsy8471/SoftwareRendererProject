#pragma once

#include <vector>
#include <memory>
#include <span>
#include "Math/AABB.h"
#include "Math/SRMath.h"
#include "Renderer/RenderQueue.h"
#include <tbb/enumerable_thread_specific.h>

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

	SRMath::mat4 m_worldMatrix; // 월드 변환 행렬
	SRMath::mat4 m_normalMatrix; // 법선 행렬 (역전치 행렬)

	// Model
	std::unique_ptr<Model> m_model;
	AABB m_worldAABB; // 월드 공간에서의 AABB

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


	void Update(float deltaTime, bool isRotate);
	void UpdateTransform(float deltaTime, bool isRotate); // Transform 업데이트 시 호출될 함수

	// 값 반환에 붙은 top-level const는 호출자에게 아무 계약도 주지 않는다.
	// C++11 시절 형태를 제거하고 관찰 API에는 nodiscard/noexcept를 명시한다.
	[[nodiscard]] SRMath::vec3 GetPosition() const noexcept { return m_position; }
	[[nodiscard]] SRMath::vec3 GetRotation() const noexcept { return m_rotation; }
	[[nodiscard]] SRMath::vec3 GetScale() const noexcept { return m_scale; }
	[[nodiscard]] const Model* GetModel() const noexcept { return m_model.get(); }
	[[nodiscard]] const AABB& GetWorldAABB() const noexcept { return m_worldAABB; }
	[[nodiscard]] std::weak_ptr<GameObject> GetParent() const noexcept { return m_parent; }
	[[nodiscard]] std::span<const std::shared_ptr<GameObject>> GetSons() const noexcept { return m_sons; }

	void SetSon(std::shared_ptr<GameObject> son);

	// Rendering
	void SubmitToRenderQueue(RenderQueue& renderQueue, const Frustum& frustum, const DebugFlags& debugFlags);
};

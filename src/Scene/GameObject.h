#pragma once

#include "Core/pch.h"
#include <vector>
#include <memory>
#include "Math/AABB.h"
#include "Math/SRMath.h"

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

	// Model
	std::unique_ptr<Model> m_model;
	AABB m_worldAABB; // 월드 공간에서의 AABB

	// Hierarchy
	std::weak_ptr<GameObject> m_parent;
	std::vector<std::shared_ptr<GameObject>> m_sons;

public:

	GameObject();
	~GameObject();

	GameObject(GameObject&&) noexcept;
	GameObject& operator=(GameObject&&) noexcept;
	GameObject(const GameObject&) = delete;
	GameObject& operator=(const GameObject&) = delete;


	bool Initialize(const SRMath::vec3& position, const SRMath::vec3& rotation, const SRMath::vec3& scale, std::unique_ptr<Model> model);
	void Update(float deltaTime);
	void UpdateTransform(); // Transform 업데이트 시 호출될 함수

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


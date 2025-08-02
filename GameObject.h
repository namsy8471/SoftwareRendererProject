#pragma once

#include <vector>
#include <memory>
#include "pch.h"
#include "SRMath.h"

class Model;

class GameObject
{
private:
	SRMath::vec3 m_position;
	SRMath::vec3 m_rotation;
	SRMath::vec3 m_scale;

	std::unique_ptr<Model> m_model;
	
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

	const SRMath::vec3 GetPosition() const;
	const SRMath::vec3 GetRotation() const;
	const SRMath::vec3 GetScale() const;
	const std::unique_ptr<Model>& GetModel() const;
	const std::weak_ptr<GameObject>& Getparent() const;
	const std::vector<std::shared_ptr<GameObject>>& Getsons() const;


	void SetSon(std::shared_ptr<GameObject> son);
};


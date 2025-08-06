#pragma once
#include "Math/SRMath.h"
#include "Math/Frustum.h"

class Camera
{
private:
	float moveSpeed = 100.f; // 카메라 이동 속도

	SRMath::vec3 m_cameraPos = { 0.f, 0.f, 5.f };
	SRMath::vec3 m_cameraforward;
	float m_cameraYaw = 0.f;	// 좌우 회전 (Y축 기준)
	float m_cameraPitch = 0.f;	// 상하 회전 (X축 기준)

	Frustum m_frustum;
	SRMath::mat4 m_viewMatrix;
	SRMath::mat4 m_projectionMatrix;

	inline void MoveForward(float deltaTime)
	{
		m_cameraPos += m_cameraforward * moveSpeed * deltaTime;
	}
	inline void MoveBackward(float deltaTime)
	{
		m_cameraPos -= m_cameraforward * moveSpeed * deltaTime;
	}
	inline void MoveRight(float deltaTime)
	{
		SRMath::vec3 right = SRMath::normalize(SRMath::cross(m_cameraforward, SRMath::vec3(0.f, 1.f, 0.f)));
		m_cameraPos += right * moveSpeed * deltaTime;
	}
	inline void MoveLeft(float deltaTime)
	{
		SRMath::vec3 right = SRMath::normalize(SRMath::cross(m_cameraforward, SRMath::vec3(0.f, 1.f, 0.f)));
		m_cameraPos -= right * moveSpeed * deltaTime;
	}

public:
	Camera();
	~Camera();

	void Initialize(SRMath::vec3 pos);
	void Update(const float deltaTime, const bool* keyInput, const float aspectRatio);
	void Move(const float deltaTime, const bool* keyInput);

	const SRMath::vec3 GetCameraPos() const { return m_cameraPos; }
	const SRMath::vec3 GetCameraForward() const { return m_cameraforward; }
	const float GetCameraYaw() const { return m_cameraYaw; }
	const float GetCameraPitch() const { return m_cameraPitch; }
	const Frustum& GetFrustum() const { return m_frustum; }
	const SRMath::mat4& GetViewMatrix() const { return m_viewMatrix; }
	const SRMath::mat4& GetProjectionMatrix() const { return m_projectionMatrix; }

	void SetCameraPos(const SRMath::vec3& pos) { m_cameraPos = pos; }
	void SetCameraYaw(float yaw) { m_cameraYaw = yaw; }
	void SetCameraPitch(float pitch) { m_cameraPitch = pitch; }
};


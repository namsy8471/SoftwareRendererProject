#pragma once
#include "SRMath.h"

class Camera
{
private:
	float moveSpeed = 100.f; // ī�޶� �̵� �ӵ�

	SRMath::vec3 m_cameraPos = { 0.f, 0.f, 5.f };
	SRMath::vec3 m_cameraforward;
	float m_cameraYaw = 0.f;	// �¿� ȸ�� (Y�� ����)
	float m_cameraPitch = 0.f;	// ���� ȸ�� (X�� ����)

	void MoveForward(float deltaTime)
	{
		m_cameraPos += m_cameraforward * moveSpeed * deltaTime;
	}
	void MoveBackward(float deltaTime)
	{
		m_cameraPos -= m_cameraforward * moveSpeed * deltaTime;
	}
	void MoveRight(float deltaTime)
	{
		SRMath::vec3 right = SRMath::normalize(SRMath::cross(m_cameraforward, SRMath::vec3(0.f, 1.f, 0.f)));
		m_cameraPos += right * moveSpeed * deltaTime;
	}
	void MoveLeft(float deltaTime)
	{
		SRMath::vec3 right = SRMath::normalize(SRMath::cross(m_cameraforward, SRMath::vec3(0.f, 1.f, 0.f)));
		m_cameraPos -= right * moveSpeed * deltaTime;
	}

public:
	Camera();
	~Camera();

	void Initialize(SRMath::vec3 pos);
	void Update(float deltaTime, const bool* keyInput);
	void Move(float deltaTime, const bool* keyInput);

	const SRMath::vec3 GetCameraPos() const { return m_cameraPos; }
	const SRMath::vec3 GetCameraForward() const { return m_cameraforward; }
	const float GetCameraYaw() const { return m_cameraYaw; }
	const float GetCameraPitch() const { return m_cameraPitch; }

	void SetCameraPos(const SRMath::vec3& pos) { m_cameraPos = pos; }
	void SetCameraYaw(float yaw) { m_cameraYaw = yaw; }
	void SetCameraPitch(float pitch) { m_cameraPitch = pitch; }
};


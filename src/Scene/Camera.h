#pragma once

#include <span>
#include "Math/SRMath.h"
#include "Math/Frustum.h"

class Camera
{
private:
	static constexpr std::size_t key_count = 256;
	float moveSpeed = 10.f; // 카메라 이동 속도

	SRMath::vec3 m_cameraPos = { 0.f, 0.f, 5.f };
	SRMath::vec3 m_cameraforward;
	float m_cameraYaw = 0.f;	// 좌우 회전 (Y축 기준)
	float m_cameraPitch = 0.f;	// 상하 회전 (X축 기준)

	Frustum m_frustum;
	SRMath::mat4 m_viewMatrix;
	SRMath::mat4 m_projectionMatrix;

	void MoveForward(float deltaTime) noexcept
	{
		m_cameraPos += m_cameraforward * moveSpeed * deltaTime;
	}
	void MoveBackward(float deltaTime) noexcept
	{
		m_cameraPos -= m_cameraforward * moveSpeed * deltaTime;
	}
	void MoveRight(float deltaTime) noexcept
	{
		SRMath::vec3 right = SRMath::normalize(SRMath::cross(m_cameraforward, SRMath::vec3(0.f, 1.f, 0.f)));
		m_cameraPos += right * moveSpeed * deltaTime;
	}
	void MoveLeft(float deltaTime) noexcept
	{
		SRMath::vec3 right = SRMath::normalize(SRMath::cross(m_cameraforward, SRMath::vec3(0.f, 1.f, 0.f)));
		m_cameraPos -= right * moveSpeed * deltaTime;
	}

public:
	Camera();
	explicit Camera(SRMath::vec3 pos) noexcept;
	~Camera() = default;

	// C++20 span은 C++11식 raw bool*의 길이 정보를 타입에 포함한다. 256개가
	// 아닌 입력은 호출 자체가 성립하지 않아 키 배열 범위 오류를 막는다.
	void Update(float deltaTime, std::span<const bool, key_count> keyInput, float aspectRatio) noexcept;
	void Move(float deltaTime, std::span<const bool, key_count> keyInput) noexcept;

	[[nodiscard]] SRMath::vec3 GetCameraPos() const noexcept { return m_cameraPos; }
	[[nodiscard]] SRMath::vec3 GetCameraForward() const noexcept { return m_cameraforward; }
	[[nodiscard]] float GetCameraYaw() const noexcept { return m_cameraYaw; }
	[[nodiscard]] float GetCameraPitch() const noexcept { return m_cameraPitch; }
	[[nodiscard]] const Frustum& GetFrustum() const noexcept { return m_frustum; }
	[[nodiscard]] const SRMath::mat4& GetViewMatrix() const noexcept { return m_viewMatrix; }
	[[nodiscard]] const SRMath::mat4& GetProjectionMatrix() const noexcept { return m_projectionMatrix; }

	void SetCameraPos(const SRMath::vec3& pos) noexcept { m_cameraPos = pos; }
	void SetCameraYaw(float yaw) noexcept { m_cameraYaw = yaw; }
	void SetCameraPitch(float pitch) noexcept { m_cameraPitch = pitch; }
};

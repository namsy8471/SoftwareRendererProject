#pragma once

#include <span>
#include "Math/SRMath.h"
#include "Math/Frustum.h"

class Camera
{
  private:
    static constexpr std::size_t keyCount = 256;
    float m_moveSpeed = 10.f; // 카메라 이동 속도

    SRMath::vec3 m_cameraPos = {0.f, 0.f, 5.f};
    SRMath::vec3 m_cameraForward;
    float m_cameraYaw = 0.f;   // 좌우 회전 (Y축 기준)
    float m_cameraPitch = 0.f; // 상하 회전 (X축 기준)

    Frustum m_frustum;
    SRMath::mat4 m_viewMatrix;
    SRMath::mat4 m_projectionMatrix;

    void moveForward(float deltaTime) noexcept { m_cameraPos += m_cameraForward * m_moveSpeed * deltaTime; }
    void moveBackward(float deltaTime) noexcept { m_cameraPos -= m_cameraForward * m_moveSpeed * deltaTime; }
    void moveRight(float deltaTime) noexcept
    {
        SRMath::vec3 right = SRMath::normalize(SRMath::cross(m_cameraForward, SRMath::vec3(0.f, 1.f, 0.f)));
        m_cameraPos += right * m_moveSpeed * deltaTime;
    }
    void moveLeft(float deltaTime) noexcept
    {
        SRMath::vec3 right = SRMath::normalize(SRMath::cross(m_cameraForward, SRMath::vec3(0.f, 1.f, 0.f)));
        m_cameraPos -= right * m_moveSpeed * deltaTime;
    }

  public:
    Camera();
    explicit Camera(SRMath::vec3 pos) noexcept;
    ~Camera() = default;

    void Update(float deltaTime, std::span<const bool, keyCount> keyInput, float aspectRatio) noexcept;
    void Move(float deltaTime, std::span<const bool, keyCount> keyInput) noexcept;

    [[nodiscard]] SRMath::vec3 GetCameraPos() const noexcept { return m_cameraPos; }
    [[nodiscard]] SRMath::vec3 GetCameraForward() const noexcept { return m_cameraForward; }
    [[nodiscard]] float GetCameraYaw() const noexcept { return m_cameraYaw; }
    [[nodiscard]] float GetCameraPitch() const noexcept { return m_cameraPitch; }
    [[nodiscard]] const Frustum &GetFrustum() const noexcept { return m_frustum; }
    [[nodiscard]] const SRMath::mat4 &GetViewMatrix() const noexcept { return m_viewMatrix; }
    [[nodiscard]] const SRMath::mat4 &GetProjectionMatrix() const noexcept { return m_projectionMatrix; }

    void SetCameraPos(const SRMath::vec3 &pos) noexcept { m_cameraPos = pos; }
    void SetCameraYaw(float yaw) noexcept { m_cameraYaw = yaw; }
    void SetCameraPitch(float pitch) noexcept { m_cameraPitch = pitch; }
};

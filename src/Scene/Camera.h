#pragma once

#include <algorithm>
#include <span>
#include "Math/AABB.h"
#include "Math/SRMath.h"
#include "Math/Frustum.h"
#include "Graphics/ViewParams.h"

class Camera
{
  private:
    static constexpr std::size_t keyCount = 256;
    float m_moveSpeed = 10.f; // 카메라 이동 속도

    SRMath::vec3 m_cameraPos = {0.f, 0.f, 5.f};
    SRMath::vec3 m_cameraForward = {0.f, 0.f, 1.f};
    float m_cameraYaw = 0.f;   // 좌우 회전 (Y축 기준)
    float m_cameraPitch = 0.f; // 상하 회전 (X축 기준)
    SRMath::vec3 m_orbitTarget = {0.f, 0.f, 0.f};
    float m_orbitDistance = 5.f;

    Frustum m_frustum;
    SRMath::mat4 m_viewMatrix;
    SRMath::mat4 m_projectionMatrix;

    void updateForwardVector() noexcept;
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

    void FocusOnBounds(const AABB &bounds, float distanceMultiplier = 1.8f) noexcept;
    void Orbit(float deltaYawRadians, float deltaPitchRadians) noexcept;
    void Zoom(float deltaDistance) noexcept;

    [[nodiscard]] SRMath::vec3 GetCameraPos() const noexcept { return m_cameraPos; }
    [[nodiscard]] SRMath::vec3 GetCameraForward() const noexcept { return m_cameraForward; }
    [[nodiscard]] float GetCameraYaw() const noexcept { return m_cameraYaw; }
    [[nodiscard]] float GetCameraPitch() const noexcept { return m_cameraPitch; }
    [[nodiscard]] SRMath::vec3 GetOrbitTarget() const noexcept { return m_orbitTarget; }
    [[nodiscard]] float GetOrbitDistance() const noexcept { return m_orbitDistance; }
    [[nodiscard]] const Frustum &GetFrustum() const noexcept { return m_frustum; }
    [[nodiscard]] const SRMath::mat4 &GetViewMatrix() const noexcept { return m_viewMatrix; }
    [[nodiscard]] const SRMath::mat4 &GetProjectionMatrix() const noexcept { return m_projectionMatrix; }
    [[nodiscard]] ViewParams GetViewParams() const noexcept { return {m_viewMatrix, m_projectionMatrix, m_cameraPos}; }

    void SetCameraPos(const SRMath::vec3 &pos) noexcept { m_cameraPos = pos; }
    void SetCameraYaw(float yaw) noexcept { m_cameraYaw = yaw; }
    void SetCameraPitch(float pitch) noexcept { m_cameraPitch = pitch; }
    void SetOrbitTarget(const SRMath::vec3 &target) noexcept { m_orbitTarget = target; }
    void SetOrbitDistance(float distance) noexcept { m_orbitDistance = std::max(0.5f, distance); }
};

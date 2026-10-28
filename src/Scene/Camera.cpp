#include "Camera.h"

#include <algorithm>
#include <cmath>
#include <numbers>

Camera::Camera() : m_cameraPos({0.0f, 0.0f, 0.0f}) {}

Camera::Camera(SRMath::vec3 pos) noexcept : m_cameraPos(pos) {}

void Camera::updateForwardVector() noexcept
{
    m_cameraForward = {std::cos(m_cameraPitch) * std::sin(m_cameraYaw), std::sin(m_cameraPitch),
                       std::cos(m_cameraPitch) * std::cos(m_cameraYaw)};
    m_cameraForward = SRMath::normalize(m_cameraForward);
}

void Camera::Update(float deltaTime, std::span<const bool, keyCount> keyInput, float aspectRatio) noexcept
{
    Camera::Move(deltaTime, keyInput);

    m_viewMatrix = SRMath::lookAt(m_cameraPos, m_cameraPos + m_cameraForward, SRMath::vec3(0.f, 1.f, 0.f));

    m_projectionMatrix =
        SRMath::perspective(std::numbers::pi_v<float> / 3.0f, aspectRatio, 0.1f, 100.f); // 60 FOV

    SRMath::mat4 viewProjectionMatrix = m_projectionMatrix * m_viewMatrix;
    m_frustum.Update(viewProjectionMatrix);
}

void Camera::Move(float deltaTime, std::span<const bool, keyCount> keyInput) noexcept
{
    updateForwardVector();

    const bool moved = keyInput['W'] || keyInput['S'] || keyInput['D'] || keyInput['A'];
    if (keyInput['W'])
        moveForward(deltaTime);
    if (keyInput['S'])
        moveBackward(deltaTime);
    if (keyInput['D'])
        moveRight(deltaTime);
    if (keyInput['A'])
        moveLeft(deltaTime);
    if (moved)
        m_orbitTarget = m_cameraPos + m_cameraForward * m_orbitDistance;
}

void Camera::FocusOnBounds(const AABB &bounds, float distanceMultiplier) noexcept
{
    if (!bounds.IsValid())
        return;
    updateForwardVector();
    m_orbitTarget = bounds.Center();
    const float radius = SRMath::length(bounds.max - bounds.min) * 0.5f;
    const float effectiveRadius = radius > 1e-4f ? radius : 1.0f;
    m_orbitDistance = std::max(0.5f, effectiveRadius * distanceMultiplier);
    m_cameraPos = m_orbitTarget - m_cameraForward * m_orbitDistance;
}

void Camera::Orbit(float deltaYawRadians, float deltaPitchRadians) noexcept
{
    constexpr float maxPitch = 89.0f * (std::numbers::pi_v<float> / 180.0f);
    m_cameraYaw += deltaYawRadians;
    m_cameraPitch = std::clamp(m_cameraPitch + deltaPitchRadians, -maxPitch, maxPitch);
    updateForwardVector();
    m_cameraPos = m_orbitTarget - m_cameraForward * m_orbitDistance;
}

void Camera::Zoom(float deltaDistance) noexcept
{
    updateForwardVector();
    m_orbitDistance = std::max(0.5f, m_orbitDistance - deltaDistance);
    m_cameraPos = m_orbitTarget - m_cameraForward * m_orbitDistance;
}

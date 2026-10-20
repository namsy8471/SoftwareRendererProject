#include "Camera.h"

#include <cmath>
#include <numbers>

Camera::Camera() : m_cameraPos({0.0f, 0.0f, 0.0f}) {}

Camera::Camera(SRMath::vec3 pos) noexcept : m_cameraPos(pos) {}

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
    // --- 카메라의 실제 방향 벡터 계산 ---
    // Yaw와 Pitch를 모두 사용하여 3D 방향 벡터를 계산합니다.
    m_cameraForward = {std::cos(m_cameraPitch) * std::sin(m_cameraYaw), std::sin(m_cameraPitch),
                       std::cos(m_cameraPitch) * std::cos(m_cameraYaw)};
    m_cameraForward = SRMath::normalize(m_cameraForward); // 정규화하여 길이를 1로 만듦

    if (keyInput['W'])
        moveForward(deltaTime);
    if (keyInput['S'])
        moveBackward(deltaTime);
    if (keyInput['D'])
        moveRight(deltaTime);
    if (keyInput['A'])
        moveLeft(deltaTime);
}

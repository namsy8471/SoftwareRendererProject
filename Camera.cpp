#include "Camera.h"

Camera::Camera() : m_cameraPos({0.0f, 0.0f ,0.0f })
{
}

Camera::~Camera()
{
}

void Camera::Initialize(SRMath::vec3 pos)
{
	m_cameraPos = pos;
}

void Camera::Update(const float deltaTime, const bool* keyInput, const float aspectRatio)
{
	Camera::Move(deltaTime, keyInput);

    m_viewMatrix = SRMath::lookAt(m_cameraPos, m_cameraPos + m_cameraforward,
        SRMath::vec3(0.f, 1.f, 0.f));

    m_projectionMatrix =
        SRMath::perspective(PI / 3.0f, aspectRatio, 0.1f, 100.f); // 60 FOV

    SRMath::mat4 viewProjectionMatrix = m_projectionMatrix * m_viewMatrix;
	m_frustum.Update(viewProjectionMatrix);
}

void Camera::Move(const float deltaTime, const bool* keyInput)
{
    moveSpeed *= deltaTime; // 프레임 시간에 따라 이동 속도를 조정합니다.

    // --- 카메라의 실제 방향 벡터 계산 ---
    // Yaw와 Pitch를 모두 사용하여 3D 방향 벡터를 계산합니다.
    m_cameraforward = {
        cos(m_cameraPitch) * sin(m_cameraYaw),
        sin(m_cameraPitch),
        cos(m_cameraPitch) * cos(m_cameraYaw)
    };
    m_cameraforward = SRMath::normalize(m_cameraforward); // 정규화하여 길이를 1로 만듦

    if(keyInput['W']) MoveForward(deltaTime);
    if (keyInput['S']) MoveBackward(deltaTime);
    if (keyInput['D']) MoveRight(deltaTime);
    if (keyInput['A']) MoveLeft(deltaTime);

    // 카메라 위치를 업데이트한 후, moveSpeed를 원래 값으로 되돌립니다.
    moveSpeed /= deltaTime;
}

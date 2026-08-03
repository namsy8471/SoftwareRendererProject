#include "Camera.h"

Camera::Camera() : m_cameraPos({0.0f, 0.0f, 0.0f})
{
}

Camera::Camera(SRMath::vec3 pos) : m_cameraPos(pos)
{
}

Camera::~Camera()
{
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
    // --- ī�޶��� ���� ���� ���� ��� ---
    // Yaw�� Pitch�� ��� ����Ͽ� 3D ���� ���͸� ����մϴ�.
    m_cameraforward = {
        cos(m_cameraPitch) * sin(m_cameraYaw),
        sin(m_cameraPitch),
        cos(m_cameraPitch) * cos(m_cameraYaw)
    };
    m_cameraforward = SRMath::normalize(m_cameraforward); // ����ȭ�Ͽ� ���̸� 1�� ����

    if(keyInput['W']) MoveForward(deltaTime);
    if (keyInput['S']) MoveBackward(deltaTime);
    if (keyInput['D']) MoveRight(deltaTime);
    if (keyInput['A']) MoveLeft(deltaTime);
}
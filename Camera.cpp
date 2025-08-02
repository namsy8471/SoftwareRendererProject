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

void Camera::Update(float deltaTime, const bool* keyInput)
{
	moveSpeed *= deltaTime; // ������ �ð��� ���� �̵� �ӵ��� �����մϴ�.

    // --- ī�޶��� ���� ���� ���� ��� ---
    // Yaw�� Pitch�� ��� ����Ͽ� 3D ���� ���͸� ����մϴ�.
    m_cameraforward = {
        cos(m_cameraPitch) * sin(m_cameraYaw),
        sin(m_cameraPitch),
        cos(m_cameraPitch) * cos(m_cameraYaw)
    };
    m_cameraforward = SRMath::normalize(m_cameraforward); // ����ȭ�Ͽ� ���̸� 1�� ����

	Camera::Move(deltaTime, keyInput);

    // ī�޶� ��ġ�� ������Ʈ�� ��, moveSpeed�� ���� ������ �ǵ����ϴ�.
	moveSpeed /= deltaTime;
}

void Camera::Move(float deltaTime, const bool* keyInput)
{
    if(keyInput['W']) MoveForward(deltaTime);
    if (keyInput['S']) MoveBackward(deltaTime);
    if (keyInput['D']) MoveRight(deltaTime);
    if (keyInput['A']) MoveLeft(deltaTime);
}

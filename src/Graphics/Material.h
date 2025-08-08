#pragma once
#include <memory>
#include "Graphics/Texture.h"
#include "Math/SRMath.h"

struct Material
{
	SRMath::vec3 diffuseColor = { 1.0f, 1.0f, 1.0f }; // �⺻ Ȯ�� ����
	SRMath::vec3 specularColor = { 1.0f, 1.0f, 1.0f }; // �⺻ �ݻ� ����
	float shininess = 32.0f; // �⺻ �ݻ���

	std::shared_ptr<Texture> diffuseTexture = nullptr; // �ؽ���
};


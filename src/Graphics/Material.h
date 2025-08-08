#pragma once
#include <memory>
#include "Graphics/Texture.h"
#include "Math/SRMath.h"

struct Material
{
	SRMath::vec3 diffuseColor = { 1.0f, 1.0f, 1.0f }; // 기본 확산 색상
	SRMath::vec3 specularColor = { 1.0f, 1.0f, 1.0f }; // 기본 반사 색상
	float shininess = 32.0f; // 기본 반사율

	std::shared_ptr<Texture> diffuseTexture = nullptr; // 텍스쳐
};


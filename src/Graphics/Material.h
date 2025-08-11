#pragma once
#include <memory>
#include "Math/SRMath.h"

class Texture;

struct Material
{
	std::string name;						// 머티리얼 이름
	SRMath::vec3 ka = { 0.5f, 0.5f, 0.5f};	// 기본 확산 색상
	SRMath::vec3 kd = { 0.5f, 0.5f, 0.5f};	// 기본 난반사 색상
	SRMath::vec3 ks = { 0.5f, 0.5f, 0.5f};	// 기본 정반사 색상
	float Ns = 32.0f;						// 기본 반사율
	float d = 1.0f;							// 투명도 (기본값: 1.0, 완전 불투명)

	std::shared_ptr<Texture> 
		diffuseTexture = nullptr;			// 텍스쳐
	int illum = 2;							// 조명 모델 (기본값: 2, Phong 조명 모델)
};


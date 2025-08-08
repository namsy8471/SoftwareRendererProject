#pragma once
#include "Math/SRMath.h"

struct DirectionalLight {
	SRMath::vec3 direction = {0.f, -1.f, 0.f}; // 빛의 방향
	SRMath::vec3 color = { 1.f, 1.f, 1.f};    // 빛의 색상
};
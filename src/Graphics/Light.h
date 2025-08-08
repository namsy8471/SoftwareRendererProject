#pragma once
#include "Math/SRMath.h"

struct DirectionalLight {
	SRMath::vec3 direction = {0.f, -1.f, 0.f}; // ���� ����
	SRMath::vec3 color = { 1.f, 1.f, 1.f};    // ���� ����
};
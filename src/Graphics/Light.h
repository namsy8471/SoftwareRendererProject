#pragma once
#include "Math/SRMath.h"

struct DirectionalLight {
	// 광원에서 장면으로 실제 광선이 진행하는 방향이다. 이름이 단순히
	// direction이었을 때 셰이더가 요구하는 "표면 -> 광원" 벡터와 혼동되어
	// 밝고 어두운 면이 뒤집혔으므로 의미를 타입 가까이에 명시한다.
	SRMath::vec3 rayDirection = { 0.f, 0.f, 1.0f };
	SRMath::vec3 color = { 1.0f, 1.0f, 1.0f };
};

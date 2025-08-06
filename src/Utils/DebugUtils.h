#pragma once
#include "Math/SRMath.h"

enum class DebugPrimitiveType
{
	Line,
	AABB,
	Sphere,
	Circle,
	Triangle,
	Quad,
	Count // 디버그 프리미티브의 개수를 나타내는 용도로 사용
};

struct DebugVertex
{
	SRMath::vec3 position; // 위치
	SRMath::vec4 color;    // 색상
};
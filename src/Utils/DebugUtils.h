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
	Count // ����� ������Ƽ���� ������ ��Ÿ���� �뵵�� ���
};

struct DebugVertex
{
	SRMath::vec3 position; // ��ġ
	SRMath::vec4 color;    // ����
};
#pragma once
#include "Math/SRMath.h"
#include <array>
struct Mesh;

struct AABB
{
	SRMath::vec3 min;
	SRMath::vec3 max;
	
	void Encapsulate(const AABB& other); // �ٸ� AABB�� �����ϵ��� Ȯ���ϴ� �Լ�
	void Encapsulate(const SRMath::vec3& point); // �� �ϳ��� �����ϵ��� Ȯ���ϴ� �Լ�
	const bool AABBIntersects(const AABB& other) const;
	const bool AABBContains(const AABB& other) const;

	const SRMath::vec3 GetCenter() const; // �߽����� ��ȯ�ϴ� �Լ�
	const std::array<SRMath::vec3, 8> GetVertice() const; // 8���� �������� ��ȯ�ϴ� �Լ�

	static AABB CreateFromMesh(const Mesh& mesh);
	static AABB TransformAABB(const AABB& aabb, const SRMath::mat4& transform);
};


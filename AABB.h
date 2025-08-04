#pragma once
#include "SRMath.h"

struct Mesh;
struct Frustum;

struct AABB
{
	SRMath::vec3 min;
	SRMath::vec3 max;
	
	void Encapsulate(const AABB& other); // �ٸ� AABB�� �����ϵ��� Ȯ���ϴ� �Լ�
	void Encapsulate(const SRMath::vec3& point); // �� �ϳ��� �����ϵ��� Ȯ���ϴ� �Լ�
	const bool AABBIntersects(const AABB& other) const;

	static AABB CreateFromMesh(const Mesh& mesh);
	static AABB TransformAABB(const AABB& aabb, const SRMath::mat4& transform);
};


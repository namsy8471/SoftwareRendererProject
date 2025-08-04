#pragma once
#include "SRMath.h"

struct Mesh;
struct Frustum;

struct AABB
{
	SRMath::vec3 min;
	SRMath::vec3 max;
	
	void Encapsulate(const AABB& other); // 다른 AABB를 포함하도록 확장하는 함수
	void Encapsulate(const SRMath::vec3& point); // 점 하나를 포함하도록 확장하는 함수
	const bool AABBIntersects(const AABB& other) const;

	static AABB CreateFromMesh(const Mesh& mesh);
	static AABB TransformAABB(const AABB& aabb, const SRMath::mat4& transform);
};


#pragma once
#include "SRMath.h"

struct AABB
{
	SRMath::vec3 min;
	SRMath::vec3 max;
	
	static bool AABBIntersects(const AABB& a, const AABB& b);
	static bool AABBContains(const AABB& a, const AABB& b);
	static bool IsAABBInFrustum(const Frustum& frustum, const AABB& aabb);
};


#pragma once
#include "Math/SRMath.h"
#include "Math/AABB.h"

struct Plane
{
	SRMath::vec3 normal = {0.f, 1.f, 0.f};
	float distance; // Distance from origin

	float GetSignedDistanceToPoint(const SRMath::vec3& point) const
	{
		return SRMath::dot(normal, point) + distance;
	}
};

class Frustum
{
public:
	Plane planes[6]; // Left, Right, Bottom, Top, Near, Far

	void Update(const SRMath::mat4& viewProjectionMatrix);

	bool IsAABBInFrustum(const AABB& aabb) const;
};


#pragma once
#include <array>
#include "Math/SRMath.h"
#include "Math/AABB.h"

struct Plane
{
	SRMath::vec3 normal = {0.f, 1.f, 0.f};
	float distance = 0.0f; // 원점에서 평면까지의 signed distance

	[[nodiscard]] float SignedDistanceTo(const SRMath::vec3& point) const noexcept
	{
		return SRMath::dot(normal, point) + distance;
	}
};

class Frustum
{
public:
	// C 배열보다 std::array가 범위 알고리즘과 크기 안전성을 제공한다.
	std::array<Plane, 6> planes{}; // Left, Right, Bottom, Top, Near, Far

	void Update(const SRMath::mat4& viewProjectionMatrix) noexcept;
	[[nodiscard]] bool IsAABBInFrustum(const AABB& aabb) const noexcept;
};

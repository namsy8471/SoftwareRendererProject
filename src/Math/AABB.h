#pragma once
#include "Math/SRMath.h"
#include <array>
#include <limits>

struct Mesh;

struct AABB
{
	SRMath::vec3 min = 
	{ std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
	SRMath::vec3 max = 
	{ std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
	
	void Encapsulate(const AABB& other); // 다른 AABB를 포함하도록 확장하는 함수
	void Encapsulate(const SRMath::vec3& point); // 점 하나를 포함하도록 확장하는 함수
	const bool AABBIntersects(const AABB& other) const;
	const bool AABBContains(const AABB& other) const;

	const SRMath::vec3 GetCenter() const; // 중심점을 반환하는 함수
	const std::array<SRMath::vec3, 8> GetVertice() const; // 8개의 꼭짓점을 반환하는 함수
	const AABB GetAABB() const; // AABB를 반환하는 함수

	static AABB CreateFromMesh(const Mesh& mesh);
	const AABB Transform(const SRMath::mat4& transform) const;
};


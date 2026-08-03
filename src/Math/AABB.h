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
	
	void Encapsulate(const AABB& other); // �ٸ� AABB�� �����ϵ��� Ȯ���ϴ� �Լ�
	void Encapsulate(const SRMath::vec3& point); // �� �ϳ��� �����ϵ��� Ȯ���ϴ� �Լ�
	const bool AABBIntersects(const AABB& other) const;
	const bool AABBContains(const AABB& other) const;

	const SRMath::vec3 GetCenter() const; // �߽����� ��ȯ�ϴ� �Լ�
	const std::array<SRMath::vec3, 8> GetVertice() const; // 8���� �������� ��ȯ�ϴ� �Լ�
	const AABB GetAABB() const; // AABB�� ��ȯ�ϴ� �Լ�
	bool IsValid() const;

	static AABB CreateFromMesh(const Mesh& mesh);
	const AABB Transform(const SRMath::mat4& transform) const;
};

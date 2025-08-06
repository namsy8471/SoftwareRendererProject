#include "Frustum.h"

void Frustum::Update(const SRMath::mat4& vpMatrix)
{
	// View-Projection 행렬에서 각 평면의 방정식을 추출합니다.
	// Ax + By + Cz + D = 0 형태

	// Left plane
	planes[0].normal.x = vpMatrix[0][3] + vpMatrix[0][0];
	planes[0].normal.y = vpMatrix[1][3] + vpMatrix[1][0];
	planes[0].normal.z = vpMatrix[2][3] + vpMatrix[2][0];
	planes[0].distance = vpMatrix[3][3] + vpMatrix[3][0];

	// Right plane
	planes[1].normal.x = vpMatrix[0][3] - vpMatrix[0][0];
	planes[1].normal.y = vpMatrix[1][3] - vpMatrix[1][0];
	planes[1].normal.z = vpMatrix[2][3] - vpMatrix[2][0];
	planes[1].distance = vpMatrix[3][3] - vpMatrix[3][0];

	// Bottom plane
	planes[2].normal.x = vpMatrix[0][3] + vpMatrix[0][1];
	planes[2].normal.y = vpMatrix[1][3] + vpMatrix[1][1];
	planes[2].normal.z = vpMatrix[2][3] + vpMatrix[2][1];
	planes[2].distance = vpMatrix[3][3] + vpMatrix[3][1];

	// Top plane
	planes[3].normal.x = vpMatrix[0][3] - vpMatrix[0][1];
	planes[3].normal.y = vpMatrix[1][3] - vpMatrix[1][1];
	planes[3].normal.z = vpMatrix[2][3] - vpMatrix[2][1];
	planes[3].distance = vpMatrix[3][3] - vpMatrix[3][1];

	// Near plane
	planes[4].normal.x = vpMatrix[0][3] + vpMatrix[0][2];
	planes[4].normal.y = vpMatrix[1][3] + vpMatrix[1][2];
	planes[4].normal.z = vpMatrix[2][3] + vpMatrix[2][2];
	planes[4].distance = vpMatrix[3][3] + vpMatrix[3][2];

	// Far plane
	planes[5].normal.x = vpMatrix[0][3] - vpMatrix[0][2];
	planes[5].normal.y = vpMatrix[1][3] - vpMatrix[1][2];
	planes[5].normal.z = vpMatrix[2][3] - vpMatrix[2][2];
	planes[5].distance = vpMatrix[3][3] - vpMatrix[3][2];

	// 모든 평면을 정규화합니다.
	for (int i = 0; i < 6; ++i)
	{
		float length = SRMath::length(planes[i].normal);
		planes[i].normal = planes[i].normal / length;
		planes[i].distance /= length;
	}
}

bool Frustum::IsAABBInFrustum(const AABB& aabb) const
{
	// 6개의 모든 평면에 대해 검사
	for (int i = 0; i < 6; ++i)
	{
		// 평면의 법선과 반대 방향으로 가장 멀리 있는 꼭짓점(N-vertex)을 찾습니다.
		SRMath::vec3 p_vertex = aabb.min;
		if (planes[i].normal.x >= 0) p_vertex.x = aabb.max.x;
		if (planes[i].normal.y >= 0) p_vertex.y = aabb.max.y;
		if (planes[i].normal.z >= 0) p_vertex.z = aabb.max.z;

		// 이 꼭짓점이 평면의 '바깥쪽'에 있다면, AABB 전체가 절두체 밖에 있는 것입니다.
		if (planes[i].GetSignedDistanceToPoint(p_vertex) < 0)
		{
			return false;
		}
	}

	// 모든 평면 검사를 통과했다면, AABB는 절두체 안에 있거나 걸쳐 있습니다.
	return true;
}

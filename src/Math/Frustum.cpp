#include "Frustum.h"

void Frustum::Update(const SRMath::mat4& vpMatrix)
{
	// View-Projection ��Ŀ��� �� ����� �������� �����մϴ�.
	// Ax + By + Cz + D = 0 ����

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

	// ��� ����� ����ȭ�մϴ�.
	for (int i = 0; i < 6; ++i)
	{
		float length = SRMath::length(planes[i].normal);
		planes[i].normal = planes[i].normal / length;
		planes[i].distance /= length;
	}
}

bool Frustum::IsAABBInFrustum(const AABB& aabb) const
{
	// 6���� ��� ��鿡 ���� �˻�
	for (int i = 0; i < 6; ++i)
	{
		// ����� ������ �ݴ� �������� ���� �ָ� �ִ� ������(N-vertex)�� ã���ϴ�.
		SRMath::vec3 p_vertex = aabb.min;
		if (planes[i].normal.x >= 0) p_vertex.x = aabb.max.x;
		if (planes[i].normal.y >= 0) p_vertex.y = aabb.max.y;
		if (planes[i].normal.z >= 0) p_vertex.z = aabb.max.z;

		// �� �������� ����� '�ٱ���'�� �ִٸ�, AABB ��ü�� ����ü �ۿ� �ִ� ���Դϴ�.
		if (planes[i].GetSignedDistanceToPoint(p_vertex) < 0)
		{
			return false;
		}
	}

	// ��� ��� �˻縦 ����ߴٸ�, AABB�� ����ü �ȿ� �ְų� ���� �ֽ��ϴ�.
	return true;
}
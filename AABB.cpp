#include "AABB.h"

// �� AABB�� ��ġ����(�����ϴ���) Ȯ���ϴ� �Լ�
bool AABB::AABBIntersects(const AABB& a, const AABB& b)
{
	// X, Y, Z ��� �࿡�� ��ġ�� �κ��� �ִ��� Ȯ���մϴ�.
	// ��� �� ���̶� ������ �и��Ǿ� �ִٸ� �� ���ڴ� ��ġ�� �ʽ��ϴ�.
	return (a.min.x <= b.max.x && a.max.x >= b.min.x) &&
		(a.min.y <= b.max.y && a.max.y >= b.min.y) &&
		(a.min.z <= b.max.z && a.max.z >= b.min.z);
}

bool AABB::AABBContains(const AABB& a, const AABB& b)
{
	return (a.min.x <= b.min.x && a.max.x >= b.max.x) &&
		(a.min.y <= b.min.y && a.max.y >= b.max.y) &&
		(a.min.z <= b.min.z && a.max.z >= b.max.z);
}

// AABB�� ����ü ���ο� �ִ��� �˻��ϴ� �Լ�
bool AABB::IsAABBInFrustum(const Frustum& frustum, const AABB& aabb)
{
    // ����ü�� �����ϴ� 6���� ��鿡 ���� ��� �˻�
    for (int i = 0; i < 6; ++i)
    {
        const Plane& plane = frustum.planes[i];

        // ����� ���� �������� ���� �ָ� �ִ� AABB�� ������(p-vertex)�� ã���ϴ�.
        SRMath::vec3 p_vertex;
        p_vertex.x = (plane.normal.x > 0) ? aabb.max.x : aabb.min.x;
        p_vertex.y = (plane.normal.y > 0) ? aabb.max.y : aabb.min.y;
        p_vertex.z = (plane.normal.z > 0) ? aabb.max.z : aabb.min.z;

        // �� �������� ����� '�ٱ���'�� �ִٸ�, AABB ��ü�� ����ü �ۿ� �ִ� ���Դϴ�.
        if (plane.GetSignedDistance(p_vertex) < 0)
        {
            return false; // �ø� ���
        }
    }

    // 6�� ��� �׽�Ʈ�� ��� ����ߴٸ�, AABB�� ����ü �ȿ� �ְų� ���� �ֽ��ϴ�.
    return true; // ������ ���
}
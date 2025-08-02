#include "AABB.h"

// 두 AABB가 겹치는지(교차하는지) 확인하는 함수
bool AABB::AABBIntersects(const AABB& a, const AABB& b)
{
	// X, Y, Z 모든 축에서 겹치는 부분이 있는지 확인합니다.
	// 어느 한 축이라도 완전히 분리되어 있다면 두 상자는 겹치지 않습니다.
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

// AABB가 절두체 내부에 있는지 검사하는 함수
bool AABB::IsAABBInFrustum(const Frustum& frustum, const AABB& aabb)
{
    // 절두체를 구성하는 6개의 평면에 대해 모두 검사
    for (int i = 0; i < 6; ++i)
    {
        const Plane& plane = frustum.planes[i];

        // 평면의 법선 방향으로 가장 멀리 있는 AABB의 꼭짓점(p-vertex)을 찾습니다.
        SRMath::vec3 p_vertex;
        p_vertex.x = (plane.normal.x > 0) ? aabb.max.x : aabb.min.x;
        p_vertex.y = (plane.normal.y > 0) ? aabb.max.y : aabb.min.y;
        p_vertex.z = (plane.normal.z > 0) ? aabb.max.z : aabb.min.z;

        // 이 꼭짓점이 평면의 '바깥쪽'에 있다면, AABB 전체가 절두체 밖에 있는 것입니다.
        if (plane.GetSignedDistance(p_vertex) < 0)
        {
            return false; // 컬링 대상
        }
    }

    // 6개 평면 테스트를 모두 통과했다면, AABB는 절두체 안에 있거나 걸쳐 있습니다.
    return true; // 렌더링 대상
}
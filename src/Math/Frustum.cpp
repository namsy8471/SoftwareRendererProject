#include "Frustum.h"

#include <cmath>

void Frustum::Update(const SRMath::mat4 &vpMatrix) noexcept
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
    for (auto &plane : planes)
    {
        const float length = SRMath::length(plane.normal);
        plane.normal = plane.normal / length;
        plane.distance /= length;
    }
}

bool Frustum::IsAABBInFrustum(const AABB &aabb) const noexcept
{
    const auto zero = _mm_setzero_ps();
    const auto aabbMin = aabb.min.m128;
    const auto aabbMax = aabb.max.m128;

    for (const auto &plane : planes)
    {
        // vec3의 미사용 네 번째 lane은 비교에서도 제외한다. 그 lane에 NaN이
        // 있어도 기존 xyz 검사에는 없던 부동소수점 예외 상태(MXCSR invalid flag)를 만들지 않는다.
        const auto normalXYZ = _mm_blend_ps(plane.normal.m128, zero, 0b1000);
        const auto useMaximum = _mm_cmpge_ps(normalXYZ, zero);
        const auto positiveVertex = _mm_blendv_ps(aabbMin, aabbMax, useMaximum);

        // dot<3>와 거리를 SSE 레지스터 내에서 직접 가산 및 비교하여 스택 round-trip 제거
        const auto dot = _mm_dp_ps(normalXYZ, positiveVertex, 0x71);
        const auto dist = _mm_add_ss(dot, _mm_set_ss(plane.distance));
        if (_mm_comilt_ss(dist, zero))
        {
            return false;
        }
    }

    // 모든 평면 검사를 통과했다면, AABB는 절두체 안에 있거나 걸쳐 있습니다.
    return true;
}

FrustumTest Frustum::ClassifyAABB(const AABB &aabb) const noexcept
{
    // 거리 항의 크기에 대한 상대 여유. float 반올림(약 6e-8)보다 충분히 커서, 부모 상자 안의
    // 자식 상자를 따로 변환하고 검사해도 결과가 뒤집히지 않는다.
    constexpr float insideMargin = 1e-4f;
    const auto zero = _mm_setzero_ps();
    const auto signMask = _mm_set1_ps(-0.0f);
    const auto marginVec = _mm_set_ss(insideMargin);

    const auto aabbMin = aabb.min.m128;
    const auto aabbMax = aabb.max.m128;
    const auto absMin = _mm_andnot_ps(signMask, aabbMin);
    const auto absMax = _mm_andnot_ps(signMask, aabbMax);
    const auto largest = _mm_max_ps(absMin, absMax);

    bool inside = true;
    for (const auto &plane : planes)
    {
        const auto normalXYZ = _mm_blend_ps(plane.normal.m128, zero, 0b1000);
        const auto useMaximum = _mm_cmpge_ps(normalXYZ, zero);
        const auto positiveVertex = _mm_blendv_ps(aabbMin, aabbMax, useMaximum);

        const auto posDist = _mm_add_ss(_mm_dp_ps(normalXYZ, positiveVertex, 0x71), _mm_set_ss(plane.distance));
        if (_mm_comilt_ss(posDist, zero))
        {
            return FrustumTest::Outside;
        }

        // N-vertex(평면 쪽으로 가장 나온 꼭짓점)까지 안쪽이면 상자 전체가 안쪽이다.
        const auto negativeVertex = _mm_blendv_ps(aabbMax, aabbMin, useMaximum);
        const auto absNormal = _mm_andnot_ps(signMask, normalXYZ);
        const auto absDist = _mm_andnot_ps(signMask, _mm_set_ss(plane.distance));
        const auto scale = _mm_add_ss(_mm_dp_ps(absNormal, largest, 0x71), absDist);
        const auto negDist = _mm_add_ss(_mm_dp_ps(normalXYZ, negativeVertex, 0x71), _mm_set_ss(plane.distance));

        // _mm_comige_ss는 a >= b 이고 둘 다 NaN이 아닐 때만 1을 반환함
        inside &= (_mm_comige_ss(negDist, _mm_mul_ss(scale, marginVec)) != 0);
    }
    return inside ? FrustumTest::Inside : FrustumTest::Intersects;
}

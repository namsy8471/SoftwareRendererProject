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
    for (const auto &plane : planes)
    {
        // 법선의 각 성분이 0 이상이면 max, 아니면 min인 P-vertex를 선택한다.
        // 성분별 분기/스택 수정 대신 레지스터 안에서 선택한다. ordered 비교라
        // NaN은 min, +0/-0은 max를 택해 기존 scalar if와 같은 규칙이다.
        // vec3의 미사용 네 번째 lane은 비교에서도 제외한다. 그 lane에 NaN이
        // 있어도 기존 xyz 검사에는 없던 부동소수점 예외 상태를 만들지 않는다.
        const auto normalXYZ = _mm_blend_ps(plane.normal.m128, zero, 0b1000);
        const auto useMaximum = _mm_cmpge_ps(normalXYZ, zero);
        SRMath::vec3 positiveVertex;
        positiveVertex.m128 = _mm_blendv_ps(aabb.min.m128, aabb.max.m128, useMaximum);

        // dot<3>는 xyz만 사용한다. 거리의 합산 순서와 평면별 조기 반환은
        // 바꾸지 않아 경계/비유한 값의 보수적인 통과 동작을 유지한다.
        if (plane.SignedDistanceTo(positiveVertex) < 0)
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
    const auto absMin = _mm_andnot_ps(signMask, aabb.min.m128);
    const auto absMax = _mm_andnot_ps(signMask, aabb.max.m128);
    SRMath::vec3 largest;
    largest.m128 = _mm_max_ps(absMin, absMax);
    bool inside = true;
    for (const auto &plane : planes)
    {
        // IsAABBInFrustum과 같은 P-vertex 선택과 같은 거리 계산이다.
        const auto normalXYZ = _mm_blend_ps(plane.normal.m128, zero, 0b1000);
        const auto useMaximum = _mm_cmpge_ps(normalXYZ, zero);
        SRMath::vec3 positiveVertex;
        positiveVertex.m128 = _mm_blendv_ps(aabb.min.m128, aabb.max.m128, useMaximum);
        if (plane.SignedDistanceTo(positiveVertex) < 0)
            return FrustumTest::Outside;
        // N-vertex(평면 쪽으로 가장 나온 꼭짓점)까지 안쪽이면 상자 전체가 안쪽이다. 앞 평면에서 이미
        // Inside가 아니어도 분기하지 않고 계산한다(blend 1번, dot 2번). 분기를 두면 GCC가 6개 평면을
        // 풀면서 경우마다 코드를 복제해 분기가 12개가 됐다.
        SRMath::vec3 negativeVertex;
        negativeVertex.m128 = _mm_blendv_ps(aabb.max.m128, aabb.min.m128, useMaximum);
        SRMath::vec3 absNormal;
        absNormal.m128 = _mm_andnot_ps(signMask, normalXYZ);
        const float scale = SRMath::dot(absNormal, largest) + std::abs(plane.distance);
        // NaN이면 비교가 false라 Intersects로 남아 노드별 검사로 내려간다.
        inside &= plane.SignedDistanceTo(negativeVertex) >= scale * insideMargin;
    }
    return inside ? FrustumTest::Inside : FrustumTest::Intersects;
}

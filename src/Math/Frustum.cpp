#include "Frustum.h"

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

#include "AABB.h"
#include "Graphics/Mesh.h"
#include "Frustum.h"
#include "Math/SIMD.h"

import sr.math;

namespace
{
    static_assert(sr::math::VectorLike<SRMath::vec3>);

    template <sr::math::VectorLike T>
    [[nodiscard]] bool ordered_bounds(const T& minimum, const T& maximum) noexcept
    {
        for (std::size_t index = 0; index < T::dimension; ++index)
        {
            if (minimum[index] > maximum[index])
            {
                return false;
            }
        }
        return true;
    }
}

void AABB::Encapsulate(const AABB& other) noexcept
{
    // 현재 min과 다른 AABB의 min 중 더 작은 값을 새로운 min으로 설정
    min.x = std::min(min.x, other.min.x);
    min.y = std::min(min.y, other.min.y);
    min.z = std::min(min.z, other.min.z);

    // 현재 max와 다른 AABB의 max 중 더 큰 값을 새로운 max로 설정
    max.x = std::max(max.x, other.max.x);
    max.y = std::max(max.y, other.max.y);
    max.z = std::max(max.z, other.max.z);
}

void AABB::Encapsulate(const SRMath::vec3& point) noexcept
{
    // 현재 min과 점의 위치 중 더 작은 값을 새로운 min으로 설정
    min.x = std::min(min.x, point.x);
    min.y = std::min(min.y, point.y);
    min.z = std::min(min.z, point.z);

    // 현재 max와 점의 위치 중 더 큰 값을 새로운 max로 설정
    max.x = std::max(max.x, point.x);
    max.y = std::max(max.y, point.y);
    max.z = std::max(max.z, point.z);
}

// 두 AABB가 겹치는지(교차하는지) 확인하는 함수
bool AABB::Intersects(const AABB& other) const noexcept
{
    // X, Y, Z 모든 축에서 겹치는 부분이 있는지 확인합니다.
    // 어느 한 축이라도 완전히 분리되어 있다면 두 상자는 겹치지 않습니다.
    return (min.x <= other.max.x && max.x >= other.min.x) &&
        (min.y <= other.max.y && max.y >= other.min.y) &&
        (min.z <= other.max.z && max.z >= other.min.z);
}

bool AABB::Contains(const AABB& other) const noexcept
{
    // 다른 AABB가 현재 AABB에 완전히 포함되는지 확인합니다.
    return (min.x <= other.min.x && max.x >= other.max.x) &&
           (min.y <= other.min.y && max.y >= other.max.y) &&
           (min.z <= other.min.z && max.z >= other.max.z);
}

AABB AABB::Transform(const SRMath::mat4& transform) const noexcept
{
    AABB newAABB;

    // C 배열 대신 크기가 타입에 포함되는 std::array를 사용한다. 두 꼭짓점씩
    // 묶으면 AVX의 8개 float lane을 정확히 채울 수 있다.
    const auto corners = std::array{
        SRMath::vec3{ this->min.x, this->min.y, this->min.z },
        SRMath::vec3{ this->max.x, this->min.y, this->min.z },
        SRMath::vec3{ this->min.x, this->max.y, this->min.z },
        SRMath::vec3{ this->max.x, this->max.y, this->min.z },
        SRMath::vec3{ this->min.x, this->min.y, this->max.z },
        SRMath::vec3{ this->max.x, this->min.y, this->max.z },
        SRMath::vec3{ this->min.x, this->max.y, this->max.z },
        SRMath::vec3{ this->max.x, this->max.y, this->max.z }
    };
    for (std::size_t index = 0; index < corners.size(); index += 2)
    {
        const SRMath::SIMD::TransformPair transformed = SRMath::SIMD::transform_pair(
            transform,
            SRMath::vec4(corners[index], 1.0f),
            SRMath::vec4(corners[index + 1], 1.0f));
        newAABB.Encapsulate(SRMath::vec3(transformed.first));
        newAABB.Encapsulate(SRMath::vec3(transformed.second));
    }

    return newAABB;
}

SRMath::vec3 AABB::Center() const noexcept
{
    return { (min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f, (min.z + max.z) * 0.5f };
}

std::array<SRMath::vec3, 8> AABB::Corners() const noexcept
{
    return {{
        { min.x, min.y, min.z },
        { max.x, min.y, min.z },
        { min.x, max.y, min.z },
        { max.x, max.y, min.z },
        { min.x, min.y, max.z },
        { max.x, min.y, max.z },
        { min.x, max.y, max.z },
        { max.x, max.y, max.z }
    }};
}

bool AABB::IsValid() const noexcept
{
    return ordered_bounds(min, max);
}

AABB AABB::CreateFromMesh(const Mesh& mesh)
{
    if(mesh.vertices.empty())
    {
        return AABB{ {0, 0, 0}, {0, 0, 0} }; // 빈 메시의 경우, AABB는 (0,0,0)으로 설정
	}

    AABB bounds;

    for (const auto& vertex : mesh.vertices)
    {
        bounds.min.x = std::min(bounds.min.x, vertex.position.x);
        bounds.min.y = std::min(bounds.min.y, vertex.position.y);
        bounds.min.z = std::min(bounds.min.z, vertex.position.z);

        bounds.max.x = std::max(bounds.max.x, vertex.position.x);
        bounds.max.y = std::max(bounds.max.y, vertex.position.y);
        bounds.max.z = std::max(bounds.max.z, vertex.position.z);
    }
	return bounds;
}

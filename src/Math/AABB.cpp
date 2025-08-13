#include "AABB.h"
#include "Graphics/Mesh.h"
#include "Frustum.h"

void AABB::Encapsulate(const AABB& other)
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

void AABB::Encapsulate(const SRMath::vec3& point)
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
const bool AABB::AABBIntersects(const AABB& other) const
{
    // X, Y, Z 모든 축에서 겹치는 부분이 있는지 확인합니다.
    // 어느 한 축이라도 완전히 분리되어 있다면 두 상자는 겹치지 않습니다.
    return (min.x <= other.max.x && max.x >= other.min.x) &&
        (min.y <= other.max.y && max.y >= other.min.y) &&
        (min.z <= other.max.z && max.z >= other.min.z);
}

const bool AABB::AABBContains(const AABB& other) const
{
    // 다른 AABB가 현재 AABB에 완전히 포함되는지 확인합니다.
    return (min.x <= other.min.x && max.x >= other.max.x) &&
           (min.y <= other.min.y && max.y >= other.max.y) &&
           (min.z <= other.min.z && max.z >= other.max.z);
}

const AABB AABB::Transform(const SRMath::mat4& transform) const
{
    AABB newAABB;

    SRMath::vec3 corners[8] = {
        { this->min.x, this->min.y, this->min.z },
        { this->max.x, this->min.y, this->min.z },
        { this->min.x, this->max.y, this->min.z },
        { this->max.x, this->max.y, this->min.z },
        { this->min.x, this->min.y, this->max.z },
        { this->max.x, this->min.y, this->max.z },
        { this->min.x, this->max.y, this->max.z },
        { this->max.x, this->max.y, this->max.z }
    };
    for (const auto& corner : corners)
    {
        const SRMath::vec3 transformedCorner = transform * SRMath::vec4(corner, 1.0f);
        newAABB.Encapsulate(transformedCorner);
    }

    return newAABB;
}

const SRMath::vec3 AABB::GetCenter() const
{
    return { (min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f, (min.z + max.z) * 0.5f };
}

const std::array<SRMath::vec3, 8> AABB::GetVertice() const
{
    std::array<SRMath::vec3, 8> array; // 8개의 꼭짓점
    array[0] = { min.x, min.y, min.z };
    array[1] = { max.x, min.y, min.z };
    array[2] = { min.x, max.y, min.z };
    array[3] = { max.x, max.y, min.z };
    array[4] = { min.x, min.y, max.z };
    array[5] = { max.x, min.y, max.z };
    array[6] = { min.x, max.y, max.z };
    array[7] = { max.x, max.y, max.z };

    return array;
}

const AABB AABB::GetAABB() const
{
    return *this;
}

bool AABB::IsValid() const
{
    return (min.x <= max.x && min.y <= max.y && min.z <= max.z);
}

AABB AABB::CreateFromMesh(const Mesh& mesh)
{
    if(mesh.vertices.empty())
    {
        return AABB{ {0, 0, 0}, {0, 0, 0} }; // 빈 메시의 경우, AABB는 (0,0,0)으로 설정
	}

    AABB bounds;
	//bounds.min = { std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
    //bounds.max = { std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest() };
    
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

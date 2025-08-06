#include "AABB.h"
#include "Graphics/Mesh.h"
#include "Frustum.h"

void AABB::Encapsulate(const AABB& other)
{
    // ���� min�� �ٸ� AABB�� min �� �� ���� ���� ���ο� min���� ����
    min.x = std::min(min.x, other.min.x);
    min.y = std::min(min.y, other.min.y);
    min.z = std::min(min.z, other.min.z);

    // ���� max�� �ٸ� AABB�� max �� �� ū ���� ���ο� max�� ����
    max.x = std::max(max.x, other.max.x);
    max.y = std::max(max.y, other.max.y);
    max.z = std::max(max.z, other.max.z);
}

void AABB::Encapsulate(const SRMath::vec3& point)
{
    // ���� min�� ���� ��ġ �� �� ���� ���� ���ο� min���� ����
    min.x = std::min(min.x, point.x);
    min.y = std::min(min.y, point.y);
    min.z = std::min(min.z, point.z);

    // ���� max�� ���� ��ġ �� �� ū ���� ���ο� max�� ����
    max.x = std::max(max.x, point.x);
    max.y = std::max(max.y, point.y);
    max.z = std::max(max.z, point.z);
}

// �� AABB�� ��ġ����(�����ϴ���) Ȯ���ϴ� �Լ�
const bool AABB::AABBIntersects(const AABB& other) const
{
    // X, Y, Z ��� �࿡�� ��ġ�� �κ��� �ִ��� Ȯ���մϴ�.
    // ��� �� ���̶� ������ �и��Ǿ� �ִٸ� �� ���ڴ� ��ġ�� �ʽ��ϴ�.
    return (min.x <= other.max.x && max.x >= other.min.x) &&
        (min.y <= other.max.y && max.y >= other.min.y) &&
        (min.z <= other.max.z && max.z >= other.min.z);
}

AABB AABB::CreateFromMesh(const Mesh& mesh)
{
    if(mesh.vertices.empty())
    {
        return AABB{ {0, 0, 0}, {0, 0, 0} }; // �� �޽��� ���, AABB�� (0,0,0)���� ����
	}

    AABB bounds;
	bounds.min = { std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
    bounds.max = { std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest() };
    
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

AABB AABB::TransformAABB(const AABB& aabb, const SRMath::mat4& transform)
{
    AABB transformed;
    transformed.min = { std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
    transformed.max = { std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest() };
    
    SRMath::vec3 corners[8] = {
        { aabb.min.x, aabb.min.y, aabb.min.z },
        { aabb.max.x, aabb.min.y, aabb.min.z },
        { aabb.min.x, aabb.max.y, aabb.min.z },
        { aabb.max.x, aabb.max.y, aabb.min.z },
        { aabb.min.x, aabb.min.y, aabb.max.z },
        { aabb.max.x, aabb.min.y, aabb.max.z },
        { aabb.min.x, aabb.max.y, aabb.max.z },
        { aabb.max.x, aabb.max.y, aabb.max.z }
    };
    for (const auto& corner : corners)
    {
        SRMath::vec3 transformedCorner = transform * SRMath::vec4(corner, 1.0f);
        transformed.Encapsulate(transformedCorner);
    }

    return transformed;
}
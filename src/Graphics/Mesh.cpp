#include "Mesh.h"
#include "Octree.h"

#include <algorithm>

Mesh::Mesh() = default;
Mesh::~Mesh() = default;
Mesh::Mesh(Mesh &&) noexcept = default;
Mesh &Mesh::operator=(Mesh &&) noexcept = default;

AABB ComputeLocalAABB(const Mesh &mesh) noexcept
{
    if (mesh.vertices.empty())
    {
        return AABB{{0, 0, 0}, {0, 0, 0}}; // 빈 메시의 경우, AABB는 (0,0,0)으로 설정
    }

    AABB bounds;

    for (const auto &vertex : mesh.vertices)
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

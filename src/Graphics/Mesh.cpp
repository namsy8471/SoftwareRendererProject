#include "Mesh.h"
#include "Octree.h"

Mesh::Mesh() = default;
Mesh::~Mesh() = default;
Mesh::Mesh(Mesh&&) noexcept = default;
Mesh& Mesh::operator=(Mesh&&) noexcept = default;

// Octree를 생성하고 빌드하는 헬퍼 함수를 여기에 둘 수 있습니다.
void BuildOctreeForMesh(Mesh& mesh) {
    if (!mesh.octree) {
        mesh.octree = std::make_unique<Octree>();
    }
    mesh.octree->Build(mesh);
}

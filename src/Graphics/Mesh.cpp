#include "Mesh.h"
#include "Octree.h"

Mesh::Mesh() = default;
Mesh::~Mesh() = default;
Mesh::Mesh(Mesh&&) noexcept = default;
Mesh& Mesh::operator=(Mesh&&) noexcept = default;

// Octree�� �����ϰ� �����ϴ� ���� �Լ��� ���⿡ �� �� �ֽ��ϴ�.
void BuildOctreeForMesh(Mesh& mesh) {
    if (!mesh.octree) {
        mesh.octree = std::make_unique<Octree>();
    }
    mesh.octree->Build(mesh);
}

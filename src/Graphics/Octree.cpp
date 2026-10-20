#include "Octree.h"
#include <vector>
#include <array>
#include "Math/SRMath.h"
#include "Graphics/Mesh.h"
#include "Math/AABB.h"

// 옥트리 생성/소멸자 (기본 구현)
Octree::Octree() = default;
Octree::~Octree() = default;

// 리프 노드가 과도한 삼각형을 가질 때 8개 옥탄트로 분할하는 함수
void Octree::subdivide(Node *node, const Mesh &mesh)
{
    // 현재 노드 AABB의 중심점과 절반 크기
    SRMath::vec3 center = (node->bounds.min + node->bounds.max) * 0.5f;
    SRMath::vec3 halfSize = (node->bounds.max - node->bounds.min) * 0.5f;

    // 1. 8개의 자식 노드를 생성하고 경계를 설정합니다.
    //    인덱스 비트 의미: (i & 1) → X(+), (i & 2) → Y(+), (i & 4) → Z(+)
    for (std::size_t i = 0; i < node->children.size(); ++i)
    {
        AABB childBounds;
        // min은 비트에 따라 center 또는 부모 min에서 시작
        childBounds.min.x = (i & 1) ? center.x : node->bounds.min.x;
        childBounds.min.y = (i & 2) ? center.y : node->bounds.min.y;
        childBounds.min.z = (i & 4) ? center.z : node->bounds.min.z;
        // max는 min + halfSize로 설정 (축 정렬 유지)
        childBounds.max = childBounds.min + halfSize;
        // 자식 노드 생성
        node->children[i] = std::make_unique<Node>(childBounds);
    }

    // 2. 기존 노드의 삼각형 목록을 임시로 옮겨둡니다. (이후 재배치)
    std::vector<unsigned int> tempVertexIndices = std::move(node->triangleIndices);
    node->triangleIndices.clear(); // 이제 부모 노드는 경계에 걸친 삼각형만 가질 것입니다.

    // 3. 임시 목록의 모든 삼각형들을 다시 'insert'하여 올바른 위치(자신 또는 자식)에 재배치합니다.
    for (size_t i = 0; i < tempVertexIndices.size(); i += 3)
    {
        unsigned int i0 = tempVertexIndices[i];
        unsigned int i1 = tempVertexIndices[i + 1];
        unsigned int i2 = tempVertexIndices[i + 2];
        insert(node, mesh, i0, i1, i2); // 재귀 호출이 아닌, 현재 노드부터 다시 삽입 시작
    }
}

// 삼각형(i0,i1,i2)을 현재 노드/자식 노드에 삽입
void Octree::insert(Node *node, const Mesh &mesh, unsigned int i0, unsigned int i1, unsigned int i2)
{
    // 삼각형 정점의 로컬(메시) 공간 좌표
    const auto &v0 = mesh.vertices[i0].position;
    const auto &v1 = mesh.vertices[i1].position;
    const auto &v2 = mesh.vertices[i2].position;

    // 삼각형의 AABB 계산 (정확한 포함 판별용)
    AABB triBounds;
    triBounds.min = {std::min({v0.x, v1.x, v2.x}), std::min({v0.y, v1.y, v2.y}),
                     std::min({v0.z, v1.z, v2.z})};
    triBounds.max = {std::max({v0.x, v1.x, v2.x}), std::max({v0.y, v1.y, v2.y}),
                     std::max({v0.z, v1.z, v2.z})};

    // 현재 노드가 내부 노드(자식이 있음)일 경우
    if (node->children.front())
    {
        // 삼각형이 어떤 자식 노드 하나에 '완전히' 포함되는지 확인
        for (std::size_t i = 0; i < node->children.size(); ++i)
        {
            if (node->children[i]->bounds.Contains(triBounds))
            {
                // 완전히 포함되면 해당 자식으로 삽입 (더 아래로 내려감)
                insert(node->children[i].get(), mesh, i0, i1, i2);
                return; // 자식에게 삽입했으므로 현재 노드에서의 역할은 끝남
            }
        }
    }

    // 다음 두 가지 경우에 삼각형을 현재 노드에 추가합니다:
    // 1. 현재 노드가 리프 노드일 경우
    // 2. 현재 노드는 내부 노드지만, 삼각형이 경계에 걸쳐 있어 자식에게 줄 수 없는 경우
    node->triangleIndices.push_back(i0);
    node->triangleIndices.push_back(i1);
    node->triangleIndices.push_back(i2);

    // 리프 노드가 너무 많은 삼각형을 가지면 분할 (분할 후 재배치는 subdivide 함수가 담당)
    if (!node->children.front() && (node->triangleIndices.size() / 3) > maxTrianglesPerNode)
    {
        subdivide(node, mesh);
    }
}

// 메시를 바탕으로 옥트리를 빌드(루트 생성 → 모든 삼각형 삽입)
void Octree::Build(const Mesh &mesh)
{
    AABB rootBounds = ComputeLocalAABB(mesh); // 메시 전체를 감싸는 AABB

    m_root = std::make_unique<Node>(rootBounds); // 루트 노드 생성

    // 메시의 모든 삼각형을 루트로부터 삽입
    for (size_t i = 0; i < mesh.indices.size(); i += 3)
    {
        insert(m_root.get(), mesh, mesh.indices[i], mesh.indices[i + 1], mesh.indices[i + 2]);
    }
}

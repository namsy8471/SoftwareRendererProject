#pragma once
#include <array>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>
#include "Math/AABB.h"
#include "Math/Frustum.h"
#include "Math/SRMath.h"

struct Mesh;

// 메시 삼각형의 공간 인덱스. 무엇을 그릴지는 모르고 "보이는 노드"만 알려 준다.
// Build 이후 원본 Mesh를 가리키지 않으므로 Mesh가 이동해도 안전하다.
class Octree
{
  public:
    struct Node
    {
        AABB bounds; // 메시 로컬 공간
        // 자식 수는 옥트리 정의상 항상 8개이므로 동적 컨테이너가 아닌 array가 맞다.
        std::array<std::unique_ptr<Node>, 8> children{};
        std::vector<unsigned int> triangleIndices; // 원본 Mesh::indices 값의 부분집합

        explicit Node(const AABB &nodeBounds) : bounds(nodeBounds) {}
    };

    Octree();
    ~Octree();

    void Build(const Mesh &mesh);
    [[nodiscard]] const Node *GetRoot() const noexcept { return m_root.get(); }

    // 프러스텀과 겹치는 노드마다 visit(std::span<const unsigned int> indices, const AABB &worldBounds)를
    // 호출한다. 순서는 전위(자기 → children[0..7])로 고정이라 명령 순서가 실행마다 같다.
    // indices는 비어 있을 수 있다(경계에 걸친 삼각형이 없는 내부 노드). 템플릿이라 호출 비용이 없다.
    template <class Visitor>
    void ForEachVisibleNode(const Frustum &frustum, const SRMath::mat4 &worldTransform, Visitor &&visit) const
    {
        if (m_root)
            visitNode(*m_root, frustum, worldTransform, visit);
    }

  private:
    static constexpr std::size_t maxTrianglesPerNode = 16;

    void subdivide(Node *node, const Mesh &mesh);
    void insert(Node *node, const Mesh &mesh, unsigned int i0, unsigned int i1, unsigned int i2);

    template <class Visitor>
    static void visitNode(const Node &node, const Frustum &frustum, const SRMath::mat4 &worldTransform,
                          Visitor &visit)
    {
        const AABB worldBounds = node.bounds.Transform(worldTransform);
        if (!frustum.IsAABBInFrustum(worldBounds))
            return;
        visit(std::span<const unsigned int>{node.triangleIndices}, worldBounds);
        for (const auto &child : node.children)
            if (child)
                visitNode(*child, frustum, worldTransform, visit);
    }

    std::unique_ptr<Node> m_root;
};

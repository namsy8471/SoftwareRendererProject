#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>
#include "Math/AABB.h"
#include "Math/Frustum.h"
#include "Math/SRMath.h"

struct Mesh;

// 메시 삼각형의 공간 인덱스. 무엇을 그릴지는 모르고 "보이는 노드"만 알려 준다.
// Build 이후 원본 Mesh를 가리키지 않으므로 Mesh가 이동해도 안전하다.
//
// Build는 트리를 만든 뒤 전위 순서의 평탄한 배열로 옮긴다. 모든 노드의 삼각형 인덱스가 한 배열에
// 전위 순서로 이어져 있어서, 한 노드의 하위 트리 전체가 인덱스 배열의 연속 구간 하나다.
class Octree
{
  public:
    struct FlatNode
    {
        AABB bounds; // 메시 로컬 공간
        // 인덱스 배열에서 [indexBegin, indexEnd)가 이 노드 자신의 삼각형,
        // [indexBegin, subtreeIndexEnd)가 하위 트리 전체의 삼각형이다.
        std::uint32_t indexBegin = 0, indexEnd = 0, subtreeIndexEnd = 0;
        // 하위 트리 다음 노드의 번호. 하위 트리를 건너뛸 때 여기로 간다.
        std::uint32_t subtreeEnd = 0;
    };

    Octree();
    ~Octree();

    void Build(const Mesh &mesh);
    [[nodiscard]] std::span<const FlatNode> GetNodes() const noexcept { return m_nodes; }
    [[nodiscard]] std::span<const unsigned int> GetIndices() const noexcept { return m_indices; }

    // 프러스텀과 겹치는 노드마다 visit(std::span<const unsigned int> indices, const AABB &worldBounds)를
    // 호출한다. 순서는 전위(자기 → children[0..7])로 고정이라 명령 순서가 실행마다 같다.
    // indices는 비어 있을 수 있다(경계에 걸친 삼각형이 없는 내부 노드). 템플릿이라 호출 비용이 없다.
    template <class Visitor>
    void ForEachVisibleNode(const Frustum &frustum, const SRMath::mat4 &worldTransform, Visitor &&visit) const
    {
        // visit가 메모리를 할당하면 컴파일러는 m_nodes가 바뀌었을 수 있다고 보고 매 반복 size를 다시
        // 읽고 나눗셈(곱셈)으로 계산한다. 지역 span으로 받아 시작 주소와 개수를 레지스터에 둔다.
        const std::span<const FlatNode> nodes{m_nodes};
        const std::span<const unsigned int> allIndices{m_indices};
        const AABBTransformerAVX2 transformer(worldTransform);
        for (std::size_t index = 0; index < nodes.size();)
        {
            const FlatNode &node = nodes[index];
            const AABB worldBounds = transformer.Transform(node.bounds);
            if (!frustum.IsAABBInFrustum(worldBounds))
            {
                index = node.subtreeEnd;
                continue;
            }
            visit(allIndices.subspan(node.indexBegin, node.indexEnd - node.indexBegin), worldBounds);
            ++index;
        }
    }

    // ForEachVisibleNode가 내는 삼각형을 같은 순서로, 인덱스 배열에서 이어진 구간끼리 합쳐
    // visit(std::span<const unsigned int> indices)로 낸다. 빈 구간은 내지 않는다.
    // 완전히 안에 든 노드는 하위 트리를 검사하지 않고 구간 하나로 낸다(자식 상자는 부모 상자 안에 있다).
    template <class Visitor>
    void ForEachVisibleRange(const Frustum &frustum, const SRMath::mat4 &worldTransform, Visitor &&visit) const
    {
        const std::span<const FlatNode> nodes{m_nodes};
        const std::span<const unsigned int> allIndices{m_indices};
        const AABBTransformerAVX2 transformer(worldTransform);
        std::uint32_t pendingBegin = 0, pendingEnd = 0;
        const auto emit = [&](std::uint32_t begin, std::uint32_t end)
        {
            if (begin == end)
                return;
            // 전위 순서로 내므로 begin은 항상 pendingEnd 이상이다. 같으면 바로 이어진다.
            if (begin != pendingEnd && pendingBegin != pendingEnd)
            {
                visit(allIndices.subspan(pendingBegin, pendingEnd - pendingBegin));
                pendingBegin = begin;
            }
            else if (pendingBegin == pendingEnd)
                pendingBegin = begin;
            pendingEnd = end;
        };
        for (std::size_t index = 0; index < nodes.size();)
        {
            const FlatNode &node = nodes[index];
            switch (frustum.ClassifyAABB(transformer.Transform(node.bounds)))
            {
            case FrustumTest::Outside:
                index = node.subtreeEnd;
                break;
            case FrustumTest::Inside:
                emit(node.indexBegin, node.subtreeIndexEnd);
                index = node.subtreeEnd;
                break;
            case FrustumTest::Intersects:
                emit(node.indexBegin, node.indexEnd);
                ++index;
                break;
            }
        }
        if (pendingBegin != pendingEnd)
            visit(allIndices.subspan(pendingBegin, pendingEnd - pendingBegin));
    }

  private:
    static constexpr std::size_t maxTrianglesPerNode = 16;

    // 생성 중에만 쓰는 트리 노드. Build가 끝나면 평탄한 배열로 옮기고 버린다.
    struct Node
    {
        AABB bounds;
        // 자식 수는 옥트리 정의상 항상 8개이므로 동적 컨테이너가 아닌 array가 맞다.
        std::array<std::unique_ptr<Node>, 8> children{};
        std::vector<unsigned int> triangleIndices; // 원본 Mesh::indices 값의 부분집합

        explicit Node(const AABB &nodeBounds) : bounds(nodeBounds) {}
    };

    void subdivide(Node *node, const Mesh &mesh);
    void insert(Node *node, const Mesh &mesh, unsigned int i0, unsigned int i1, unsigned int i2);
    void flatten(const Node &node);

    std::vector<FlatNode> m_nodes;          // 전위 순서
    std::vector<unsigned int> m_indices;    // 모든 노드의 삼각형 인덱스, 전위 순서
};

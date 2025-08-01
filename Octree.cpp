#include "Octree.h"
#include "Model.h"

OctreeNode::OctreeNode(const AABB& bounds) : bounds(bounds) {
	for (int i = 0; i < 8; ++i) {
		children[i] = nullptr;
	}
}

// 두 AABB가 겹치는지(교차하는지) 확인하는 함수
bool AABBIntersects(const AABB& a, const AABB& b)
{
	// X, Y, Z 모든 축에서 겹치는 부분이 있는지 확인합니다.
	// 어느 한 축이라도 완전히 분리되어 있다면 두 상자는 겹치지 않습니다.
	return (a.min.x <= b.max.x && a.max.x >= b.min.x) &&
		(a.min.y <= b.max.y && a.max.y >= b.min.y) &&
		(a.min.z <= b.max.z && a.max.z >= b.min.z);
}

bool AABBContains(const AABB& a, const AABB& b)
{
	return (a.min.x <= b.min.x && a.max.x >= b.max.x) &&
		(a.min.y <= b.min.y && a.max.y >= b.max.y) &&
		(a.min.z <= b.min.z && a.max.z >= b.max.z);
}

void Octree::subdivide(OctreeNode* node)
{
	SRMath::vec3 center = (node->bounds.min + node->bounds.max) * 0.5f;
	SRMath::vec3 half_size = (node->bounds.max - node->bounds.min) * 0.5f;

	// 1. 8개의 자식 노드를 생성하고 경계를 설정합니다.
	for (int i = 0; i < 8; i++)
	{
		AABB childBounds;
		childBounds.min.x = (i & 1) ? center.x : node->bounds.min.x;
		childBounds.min.y = (i & 2) ? center.y : node->bounds.min.y;
		childBounds.min.z = (i & 4) ? center.z : node->bounds.min.z;
		childBounds.max = childBounds.min + half_size;
		node->children[i] = std::make_unique<OctreeNode>(childBounds);
	}

	// 2. 기존 노드의 삼각형 목록을 임시로 옮겨둡니다.
	std::vector<unsigned int> tempTriangleIndices = std::move(node->triangleIndices);
	node->triangleIndices.clear(); // 이제 부모 노드는 경계에 걸친 삼각형만 가질 것입니다.

	// 3. 임시 목록의 모든 삼각형들을 다시 'insert'하여 올바른 위치(자신 또는 자식)에 재배치합니다.
	for (unsigned int triIndex : tempTriangleIndices)
	{
		insert(node, triIndex); // 재귀 호출이 아닌, 현재 노드부터 다시 삽입 시작
	}
}

void Octree::insert(OctreeNode* node, unsigned int triangleIndex)
{
	const auto& v0 = sourceMesh->vertices[sourceMesh->indices[triangleIndex]].position;
	const auto& v1 = sourceMesh->vertices[sourceMesh->indices[triangleIndex + 1]].position;
	const auto& v2 = sourceMesh->vertices[sourceMesh->indices[triangleIndex + 2]].position;

	AABB triBounds;
	triBounds.min = { std::min({v0.x, v1.x, v2.x}), std::min({v0.y, v1.y, v2.y}), std::min({v0.z, v1.z, v2.z}) };
	triBounds.max = { std::max({v0.x, v1.x, v2.x}), std::max({v0.y, v1.y, v2.y}), std::max({v0.z, v1.z, v2.z}) };

	// 현재 노드가 내부 노드(자식이 있음)일 경우
	if (node->children[0] != nullptr)
	{
		// 삼각형이 어떤 자식 노드 하나에 '완전히' 포함되는지 확인
		for (int i = 0; i < 8; ++i)
		{
			if (AABBContains(node->children[i]->bounds, triBounds))
			{
				insert(node->children[i].get(), triangleIndex);
				return; // 자식에게 삽입했으므로 현재 노드에서의 역할은 끝남
			}
		}
	}

	// 다음 두 가지 경우에 삼각형을 현재 노드에 추가합니다:
	// 1. 현재 노드가 리프 노드일 경우
	// 2. 현재 노드는 내부 노드지만, 삼각형이 경계에 걸쳐 있어 자식에게 줄 수 없는 경우
	node->triangleIndices.push_back(triangleIndex);

	// 리프 노드가 너무 많은 삼각형을 가지면 분할 (분할 후 재배치는 subdivide 함수가 담당)
	if (node->children[0] == nullptr && node->triangleIndices.size() > MAX_TRIANGLES_PER_NODE)
	{
		subdivide(node);
	}
}

void Octree::Build(const Mesh& mesh)
{
	this->sourceMesh = &mesh;

	AABB rootBounds;
	rootBounds.min = SRMath::vec3(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
	rootBounds.max = SRMath::vec3(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());

	for (const auto& vertex : mesh.vertices)
	{
		rootBounds.min.x = std::min(rootBounds.min.x, vertex.position.x);
		rootBounds.min.y = std::min(rootBounds.min.y, vertex.position.y);
		rootBounds.min.z = std::min(rootBounds.min.z, vertex.position.z);

		rootBounds.max.x = std::max(rootBounds.max.x, vertex.position.x);
		rootBounds.max.y = std::max(rootBounds.max.y, vertex.position.y);
		rootBounds.max.z = std::max(rootBounds.max.z, vertex.position.z);
	}

	root = std::make_unique<OctreeNode>(rootBounds);

	for (size_t i = 0; i < mesh.indices.size(); i += 3)
	{
		insert(root.get(), i);
	}
}

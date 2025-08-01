#include "Octree.h"
#include "Model.h"

OctreeNode::OctreeNode(const AABB& bounds) : bounds(bounds) {
	for (int i = 0; i < 8; ++i) {
		children[i] = nullptr;
	}
}

// �� AABB�� ��ġ����(�����ϴ���) Ȯ���ϴ� �Լ�
bool AABBIntersects(const AABB& a, const AABB& b)
{
	// X, Y, Z ��� �࿡�� ��ġ�� �κ��� �ִ��� Ȯ���մϴ�.
	// ��� �� ���̶� ������ �и��Ǿ� �ִٸ� �� ���ڴ� ��ġ�� �ʽ��ϴ�.
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

	// 1. 8���� �ڽ� ��带 �����ϰ� ��踦 �����մϴ�.
	for (int i = 0; i < 8; i++)
	{
		AABB childBounds;
		childBounds.min.x = (i & 1) ? center.x : node->bounds.min.x;
		childBounds.min.y = (i & 2) ? center.y : node->bounds.min.y;
		childBounds.min.z = (i & 4) ? center.z : node->bounds.min.z;
		childBounds.max = childBounds.min + half_size;
		node->children[i] = std::make_unique<OctreeNode>(childBounds);
	}

	// 2. ���� ����� �ﰢ�� ����� �ӽ÷� �ŰܵӴϴ�.
	std::vector<unsigned int> tempTriangleIndices = std::move(node->triangleIndices);
	node->triangleIndices.clear(); // ���� �θ� ���� ��迡 ��ģ �ﰢ���� ���� ���Դϴ�.

	// 3. �ӽ� ����� ��� �ﰢ������ �ٽ� 'insert'�Ͽ� �ùٸ� ��ġ(�ڽ� �Ǵ� �ڽ�)�� ���ġ�մϴ�.
	for (unsigned int triIndex : tempTriangleIndices)
	{
		insert(node, triIndex); // ��� ȣ���� �ƴ�, ���� ������ �ٽ� ���� ����
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

	// ���� ��尡 ���� ���(�ڽ��� ����)�� ���
	if (node->children[0] != nullptr)
	{
		// �ﰢ���� � �ڽ� ��� �ϳ��� '������' ���ԵǴ��� Ȯ��
		for (int i = 0; i < 8; ++i)
		{
			if (AABBContains(node->children[i]->bounds, triBounds))
			{
				insert(node->children[i].get(), triangleIndex);
				return; // �ڽĿ��� ���������Ƿ� ���� ��忡���� ������ ����
			}
		}
	}

	// ���� �� ���� ��쿡 �ﰢ���� ���� ��忡 �߰��մϴ�:
	// 1. ���� ��尡 ���� ����� ���
	// 2. ���� ���� ���� �������, �ﰢ���� ��迡 ���� �־� �ڽĿ��� �� �� ���� ���
	node->triangleIndices.push_back(triangleIndex);

	// ���� ��尡 �ʹ� ���� �ﰢ���� ������ ���� (���� �� ���ġ�� subdivide �Լ��� ���)
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

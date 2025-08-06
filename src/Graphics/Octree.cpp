#include "Octree.h"
#include <vector>
#include "Math/SRMath.h"
#include "Graphics/Mesh.h"
#include "Math/AABB.h"
#include "Renderer/RenderQueue.h"
#include "Math/Frustum.h"

class Octree::OctreeNode {
public:
	AABB bounds;
	std::unique_ptr<OctreeNode> children[8];
	std::vector<unsigned int> triangleIndices;

	OctreeNode(const AABB& bounds);
};

Octree::OctreeNode::OctreeNode(const AABB& bounds) : bounds(bounds) {
	for (int i = 0; i < 8; ++i) {
		children[i] = nullptr;
	}
}

Octree::Octree() = default;
Octree::~Octree() = default;

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
			if (triBounds.AABBIntersects(node->children[i]->bounds))
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

	AABB rootBounds = AABB::CreateFromMesh(mesh);

	root = std::make_unique<OctreeNode>(rootBounds);

	for (size_t i = 0; i < mesh.indices.size(); i += 3)
	{
		insert(root.get(), i);
	}
}



void Octree::submitNodeRecursive(RenderQueue& renderQueue, const SRMath::mat4& modelMatrix, const OctreeNode* node)
{
	// ����ü �ø��� ���⼭ ���� (�ʿ� �� �߰�)

	//DebugPrimitiveCommand cmd;
	//cmd.vertices = node->bounds;
	//cmd.worldTransform = modelMatrix;
	//cmd.color = { 1.0f, 1.0f, 0.0f, 1.0f }; // �����
	//renderQueue.Submit(cmd);

	if (node->children[0]) {
		for (int i = 0; i < 8; ++i) {
			submitNodeRecursive(renderQueue, modelMatrix, node->children[i].get());
		}
	}
}

void Octree::SubmitDebugToRenderQueue(RenderQueue& renderQueue, const SRMath::mat4& modelMatrix)
{
	if (root) submitNodeRecursive(renderQueue, modelMatrix, root.get());

}

void Octree::submitNodeRecursive(RenderQueue& renderQueue, const Frustum& frustum, const SRMath::mat4& worldTransform, const OctreeNode* node)
{
	const AABB worldNodeAABB = AABB::TransformAABB(node->bounds, worldTransform);
	if(!frustum.IsAABBInFrustum(worldNodeAABB)) return; // ����ü �ۿ� ������ �ø�

	if (node->children[0] == nullptr) // ���� ����� ���
	{
		if(!node->triangleIndices.empty())
		{
			MeshRenderCommand cmd;
			cmd.sourceMesh = this->sourceMesh;
			cmd.indicesToDraw = &node->triangleIndices;
			cmd.worldTransform = worldTransform;
			renderQueue.Submit(cmd);
		}
		return;
	}

	for(const auto & child : node->children)
	{
		if (child) submitNodeRecursive(renderQueue, frustum, worldTransform, child.get());
	}
}


void Octree::SubmitVisibleNodes(RenderQueue& renderQueue, const Frustum& frustum, const SRMath::mat4& worldTransform)
{
	if (root) submitNodeRecursive(renderQueue, frustum, worldTransform, root.get());
}
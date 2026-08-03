#include "Octree.h"
#include <vector>
#include <array>
#include "Math/SRMath.h"
#include "Graphics/Mesh.h"
#include "Math/AABB.h"
#include "Renderer/RenderQueue.h"
#include "Math/Frustum.h"
#include "Utils/DebugUtils.h"

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

// ��Ʈ�� ����/�Ҹ��� (�⺻ ����)
Octree::Octree() = default;
Octree::~Octree() = default;

// ���� ��尡 ������ �ﰢ���� ���� �� 8�� ��źƮ�� �����ϴ� �Լ�
void Octree::subdivide(OctreeNode* node)
{
	// ���� ��� AABB�� �߽����� ���� ũ��
	SRMath::vec3 center = (node->bounds.min + node->bounds.max) * 0.5f;
	SRMath::vec3 half_size = (node->bounds.max - node->bounds.min) * 0.5f;

	// 1. 8���� �ڽ� ��带 �����ϰ� ��踦 �����մϴ�.
	//    �ε��� ��Ʈ �ǹ�: (i & 1) �� X(+), (i & 2) �� Y(+), (i & 4) �� Z(+)
	for (int i = 0; i < 8; i++)
	{
		AABB childBounds;
		// min�� ��Ʈ�� ���� center �Ǵ� �θ� min���� ����
		childBounds.min.x = (i & 1) ? center.x : node->bounds.min.x;
		childBounds.min.y = (i & 2) ? center.y : node->bounds.min.y;
		childBounds.min.z = (i & 4) ? center.z : node->bounds.min.z;
		// max�� min + half_size�� ���� (�� ���� ����)
		childBounds.max = childBounds.min + half_size;
		// �ڽ� ��� ����
		node->children[i] = std::make_unique<OctreeNode>(childBounds);
	}

	// 2. ���� ����� �ﰢ�� ����� �ӽ÷� �ŰܵӴϴ�. (���� ���ġ)
	std::vector<unsigned int> tempVertexIndices = std::move(node->triangleIndices);
	node->triangleIndices.clear(); // ���� �θ� ���� ��迡 ��ģ �ﰢ���� ���� ���Դϴ�.

	// 3. �ӽ� ����� ��� �ﰢ������ �ٽ� 'insert'�Ͽ� �ùٸ� ��ġ(�ڽ� �Ǵ� �ڽ�)�� ���ġ�մϴ�.
	for (size_t i = 0; i < tempVertexIndices.size(); i += 3)
	{
		unsigned int i0 = tempVertexIndices[i];
		unsigned int i1 = tempVertexIndices[i + 1];
		unsigned int i2 = tempVertexIndices[i + 2];
		insert(node, i0, i1, i2); // ��� ȣ���� �ƴ�, ���� ������ �ٽ� ���� ����
	}
}

// �ﰢ��(i0,i1,i2)�� ���� ���/�ڽ� ��忡 ����
void Octree::insert(OctreeNode* node, unsigned int i0, unsigned int i1, unsigned int i2)
{
	// �ﰢ�� ������ ����(�޽�) ���� ��ǥ
	const auto& v0 = sourceMesh->vertices[i0].position;
	const auto& v1 = sourceMesh->vertices[i1].position;
	const auto& v2 = sourceMesh->vertices[i2].position;;

	// �ﰢ���� AABB ��� (��Ȯ�� ���� �Ǻ���)
	AABB triBounds;
	triBounds.min = { std::min({v0.x, v1.x, v2.x}), std::min({v0.y, v1.y, v2.y}), std::min({v0.z, v1.z, v2.z}) };
	triBounds.max = { std::max({v0.x, v1.x, v2.x}), std::max({v0.y, v1.y, v2.y}), std::max({v0.z, v1.z, v2.z}) };

	// ���� ��尡 ���� ���(�ڽ��� ����)�� ���
	if (node->children[0] != nullptr)
	{
		// �ﰢ���� � �ڽ� ��� �ϳ��� '������' ���ԵǴ��� Ȯ��
		for (int i = 0; i < 8; ++i)
		{
			if (node->children[i]->bounds.AABBContains(triBounds))
			{
				// ������ ���ԵǸ� �ش� �ڽ����� ���� (�� �Ʒ��� ������)
				insert(node->children[i].get(), i0, i1, i2);
				return; // �ڽĿ��� ���������Ƿ� ���� ��忡���� ������ ����
			}
		}
	}

	// ���� �� ���� ��쿡 �ﰢ���� ���� ��忡 �߰��մϴ�:
	// 1. ���� ��尡 ���� ����� ���
	// 2. ���� ���� ���� �������, �ﰢ���� ��迡 ���� �־� �ڽĿ��� �� �� ���� ���
	node->triangleIndices.push_back(i0);
	node->triangleIndices.push_back(i1);
	node->triangleIndices.push_back(i2);

	// ���� ��尡 �ʹ� ���� �ﰢ���� ������ ���� (���� �� ���ġ�� subdivide �Լ��� ���)
	if (node->children[0] == nullptr && (node->triangleIndices.size() / 3) > MAX_TRIANGLES_PER_NODE)
	{
		subdivide(node);
	}
}

// �޽ø� �������� ��Ʈ���� ���(��Ʈ ���� �� ��� �ﰢ�� ����)
void Octree::Build(const Mesh& mesh)
{
	this->sourceMesh = &mesh;                 // �ﰢ�� ���� ������ ���� �޽� ������ ����

	AABB rootBounds = AABB::CreateFromMesh(mesh); // �޽� ��ü�� ���δ� AABB

	root = std::make_unique<OctreeNode>(rootBounds); // ��Ʈ ��� ����

	// �޽��� ��� �ﰢ���� ��Ʈ�κ��� ����
	for (size_t i = 0; i < mesh.indices.size(); i += 3)
	{
		insert(root.get(), mesh.indices[i], mesh.indices[i + 1], mesh.indices[i + 2]);
	}
}

// ��������� �������� �ø� �� ���� ť ����(����� AABB ����)
void Octree::submitNodeRecursive(RenderQueue& renderQueue, const Frustum& frustum, const SRMath::mat4& worldTransform, 
	std::vector<MeshRenderCommand>& threadLocalCmd, std::vector<DebugPrimitiveCommand>& threadlocalDebugCmd, 
	const DebugFlags& debugFlags, const OctreeNode* node)
{
	// ��� ��踦 ���� �������� ��ȯ
	const AABB worldNodeAABB = node->bounds.Transform(worldTransform);
	// �������� ���̸� ���� ���� (�ø�)
	if(!frustum.IsAABBInFrustum(worldNodeAABB)) return; // ����ü �ۿ� ������ �ø�

	// �� ��忡 �ﰢ���� ������ ���� ť�� �޽� ���� ��� ����
	if(!node->triangleIndices.empty())
	{
		MeshRenderCommand cmd;
		cmd.sourceMesh = this->sourceMesh;                 // ���� �޽�
		cmd.indicesToDraw = &node->triangleIndices;        // �� ��忡 ���� �ﰢ�� �ε��� �����
		cmd.worldTransform = worldTransform;               // ������Ʈ�� ���� ��ȯ
		cmd.material = &this->sourceMesh->material;        // �޽��� ������ ���
			
		// ���̾�/�� ��� ��ȯ (����� �÷��׿� ����)
		if(debugFlags.bShowWireframe)
		{
			cmd.rasterizeMode = ERasterizeMode::Wireframe;
		}
		else
		{
			cmd.rasterizeMode = ERasterizeMode::Fill;
		}

		threadLocalCmd.push_back(cmd); // ��Ƽ������ ���� ���� ��� ť�� �߰�
	}

	// �����: ��� AABB�� �������� ���� ť�� ����
	if (debugFlags.bShowAABB)
	{
		// AABB 8�� ������ ���
		std::array<SRMath::vec3, 8> vertices = worldNodeAABB.GetVertice();
		SRMath::vec4 color = SRMath::vec4(1.0f, 0.0f, 0.0f, 1.0f); // ������
		std::vector<DebugVertex> debugVertices;

		// 12���� ������ �������� �߰� (Bottom/Top/Sides)
		debugVertices.insert(debugVertices.end(), {
			{vertices[0], color}, {vertices[1], color}, {vertices[1], color}, {vertices[3], color}, {vertices[3], color}, {vertices[2], color}, {vertices[2], color}, {vertices[0], color}, // Bottom
			{vertices[4], color}, {vertices[5], color}, {vertices[5], color}, {vertices[7], color}, {vertices[7], color}, {vertices[6], color}, {vertices[6], color}, {vertices[4], color}, // Top
			{vertices[0], color}, {vertices[4], color}, {vertices[1], color}, {vertices[5], color}, {vertices[2], color}, {vertices[6], color}, {vertices[3], color}, {vertices[7], color}  // Sides
			});

		// ����� ������Ƽ��(����) ����
		DebugPrimitiveCommand cmd;
		cmd.vertices = debugVertices;
		cmd.worldTransform = SRMath::mat4::identity(); // �̹� ����� ��ȯ�� ��ǥ�̹Ƿ� �������
		cmd.type = DebugPrimitiveType::Line;
		threadlocalDebugCmd.push_back(cmd);
	}
	
	// �ڽ� ���鿡 ���� ���� ó�� (�����ϴ� ��쿡��)
	for(const auto & child : node->children)
	{
		if (child) submitNodeRecursive(renderQueue, frustum, worldTransform, threadLocalCmd, threadlocalDebugCmd, debugFlags, child.get());
	}
}

// ��Ʈ���� �����Ͽ� ���̴� ������ ���� ť�� �����ϴ� ������
void Octree::SubmitNodesToRenderQueue(RenderQueue& renderQueue, const Frustum& frustum, const SRMath::mat4& worldTransform,
	std::vector<MeshRenderCommand>& threadLocalCmd, std::vector<DebugPrimitiveCommand>& threadlocalDebugCmd, const DebugFlags& debugFlags)
{
	if (!root) return; // ������ ���� ��� ����

	submitNodeRecursive(renderQueue, frustum, worldTransform, threadLocalCmd, threadlocalDebugCmd, debugFlags, root.get());
}
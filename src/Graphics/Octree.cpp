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
			if (triBounds.AABBIntersects(node->children[i]->bounds))
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

	AABB rootBounds = AABB::CreateFromMesh(mesh);

	root = std::make_unique<OctreeNode>(rootBounds);

	for (size_t i = 0; i < mesh.indices.size(); i += 3)
	{
		insert(root.get(), i);
	}
}



void Octree::submitNodeRecursive(RenderQueue& renderQueue, const SRMath::mat4& modelMatrix, const OctreeNode* node)
{
	// 절두체 컬링은 여기서 생략 (필요 시 추가)

	//DebugPrimitiveCommand cmd;
	//cmd.vertices = node->bounds;
	//cmd.worldTransform = modelMatrix;
	//cmd.color = { 1.0f, 1.0f, 0.0f, 1.0f }; // 노란색
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
	if(!frustum.IsAABBInFrustum(worldNodeAABB)) return; // 절두체 밖에 있으면 컬링

	if (node->children[0] == nullptr) // 리프 노드인 경우
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
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

// 옥트리 생성/소멸자 (기본 구현)
Octree::Octree() = default;
Octree::~Octree() = default;

// 리프 노드가 과도한 삼각형을 가질 때 8개 옥탄트로 분할하는 함수
void Octree::subdivide(OctreeNode* node)
{
	// 현재 노드 AABB의 중심점과 절반 크기
	SRMath::vec3 center = (node->bounds.min + node->bounds.max) * 0.5f;
	SRMath::vec3 half_size = (node->bounds.max - node->bounds.min) * 0.5f;

	// 1. 8개의 자식 노드를 생성하고 경계를 설정합니다.
	//    인덱스 비트 의미: (i & 1) → X(+), (i & 2) → Y(+), (i & 4) → Z(+)
	for (int i = 0; i < 8; i++)
	{
		AABB childBounds;
		// min은 비트에 따라 center 또는 부모 min에서 시작
		childBounds.min.x = (i & 1) ? center.x : node->bounds.min.x;
		childBounds.min.y = (i & 2) ? center.y : node->bounds.min.y;
		childBounds.min.z = (i & 4) ? center.z : node->bounds.min.z;
		// max는 min + half_size로 설정 (축 정렬 유지)
		childBounds.max = childBounds.min + half_size;
		// 자식 노드 생성
		node->children[i] = std::make_unique<OctreeNode>(childBounds);
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
		insert(node, i0, i1, i2); // 재귀 호출이 아닌, 현재 노드부터 다시 삽입 시작
	}
}

// 삼각형(i0,i1,i2)을 현재 노드/자식 노드에 삽입
void Octree::insert(OctreeNode* node, unsigned int i0, unsigned int i1, unsigned int i2)
{
	// 삼각형 정점의 로컬(메시) 공간 좌표
	const auto& v0 = sourceMesh->vertices[i0].position;
	const auto& v1 = sourceMesh->vertices[i1].position;
	const auto& v2 = sourceMesh->vertices[i2].position;;

	// 삼각형의 AABB 계산 (정확한 포함 판별용)
	AABB triBounds;
	triBounds.min = { std::min({v0.x, v1.x, v2.x}), std::min({v0.y, v1.y, v2.y}), std::min({v0.z, v1.z, v2.z}) };
	triBounds.max = { std::max({v0.x, v1.x, v2.x}), std::max({v0.y, v1.y, v2.y}), std::max({v0.z, v1.z, v2.z}) };

	// 현재 노드가 내부 노드(자식이 있음)일 경우
	if (node->children[0] != nullptr)
	{
		// 삼각형이 어떤 자식 노드 하나에 '완전히' 포함되는지 확인
		for (int i = 0; i < 8; ++i)
		{
			if (node->children[i]->bounds.AABBContains(triBounds))
			{
				// 완전히 포함되면 해당 자식으로 삽입 (더 아래로 내려감)
				insert(node->children[i].get(), i0, i1, i2);
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
	if (node->children[0] == nullptr && (node->triangleIndices.size() / 3) > MAX_TRIANGLES_PER_NODE)
	{
		subdivide(node);
	}
}

// 메시를 바탕으로 옥트리를 빌드(루트 생성 → 모든 삼각형 삽입)
void Octree::Build(const Mesh& mesh)
{
	this->sourceMesh = &mesh;                 // 삼각형 정점 참조용 원본 메시 포인터 보관

	AABB rootBounds = AABB::CreateFromMesh(mesh); // 메시 전체를 감싸는 AABB

	root = std::make_unique<OctreeNode>(rootBounds); // 루트 노드 생성

	// 메시의 모든 삼각형을 루트로부터 삽입
	for (size_t i = 0; i < mesh.indices.size(); i += 3)
	{
		insert(root.get(), mesh.indices[i], mesh.indices[i + 1], mesh.indices[i + 2]);
	}
}

// 재귀적으로 프러스텀 컬링 및 렌더 큐 제출(디버그 AABB 포함)
void Octree::submitNodeRecursive(RenderQueue& renderQueue, const Frustum& frustum, const SRMath::mat4& worldTransform, 
	const DebugFlags& debugFlags, const OctreeNode* node)
{
	// 노드 경계를 월드 공간으로 변환
	const AABB worldNodeAABB = node->bounds.Transform(worldTransform);
	// 프러스텀 밖이면 조기 리턴 (컬링)
	if(!frustum.IsAABBInFrustum(worldNodeAABB)) return; // 절두체 밖에 있으면 컬링

	// 이 노드에 삼각형이 있으면 렌더 큐에 메시 렌더 명령 제출
	if(!node->triangleIndices.empty())
	{
		MeshRenderCommand cmd;
		cmd.sourceMesh = this->sourceMesh;                 // 원본 메시
		cmd.indicesToDraw = &node->triangleIndices;        // 이 노드에 속한 삼각형 인덱스 서브셋
		cmd.worldTransform = worldTransform;               // 오브젝트의 월드 변환
		cmd.material = &this->sourceMesh->material;        // 메시의 재질을 사용
			
		// 와이어/필 모드 전환 (디버그 플래그에 따름)
		if(debugFlags.bShowWireframe)
		{
			cmd.rasterizeMode = ERasterizeMode::Wireframe;
		}
		else
		{
			cmd.rasterizeMode = ERasterizeMode::Fill;
		}

		renderQueue.Submit(cmd);                           // 메시 렌더 명령 큐에 추가
	}

	// 디버그: 노드 AABB를 선분으로 렌더 큐에 제출
	if (debugFlags.bShowAABB)
	{
		// AABB 8개 꼭짓점 계산
		std::array<SRMath::vec3, 8> vertices = worldNodeAABB.GetVertice();
		SRMath::vec4 color = SRMath::vec4(1.0f, 0.0f, 0.0f, 1.0f); // 빨간색
		std::vector<DebugVertex> debugVertices;

		// 12개의 엣지를 선분으로 추가 (Bottom/Top/Sides)
		debugVertices.insert(debugVertices.end(), {
			{vertices[0], color}, {vertices[1], color}, {vertices[1], color}, {vertices[3], color}, {vertices[3], color}, {vertices[2], color}, {vertices[2], color}, {vertices[0], color}, // Bottom
			{vertices[4], color}, {vertices[5], color}, {vertices[5], color}, {vertices[7], color}, {vertices[7], color}, {vertices[6], color}, {vertices[6], color}, {vertices[4], color}, // Top
			{vertices[0], color}, {vertices[4], color}, {vertices[1], color}, {vertices[5], color}, {vertices[2], color}, {vertices[6], color}, {vertices[3], color}, {vertices[7], color}  // Sides
			});

		// 디버그 프리미티브(선분) 제출
		DebugPrimitiveCommand cmd;
		cmd.vertices = debugVertices;
		cmd.worldTransform = SRMath::mat4::identity(); // 이미 월드로 변환된 좌표이므로 단위행렬
		cmd.type = DebugPrimitiveType::Line;
		renderQueue.Submit(cmd);
	}
	
	// 자식 노드들에 대해 동일 처리 (존재하는 경우에만)
	for(const auto & child : node->children)
	{
		if (child) submitNodeRecursive(renderQueue, frustum, worldTransform, debugFlags, child.get());
	}
}

// 루트부터 시작하여 보이는 노드들을 렌더 큐에 제출하는 진입점
void Octree::SubmitNodesToRenderQueue(RenderQueue& renderQueue, const Frustum& frustum, const SRMath::mat4& worldTransform, const DebugFlags& debugFlags)
{
	if (!root) return; // 빌드되지 않은 경우 무시

	submitNodeRecursive(renderQueue, frustum, worldTransform, debugFlags, root.get());
}
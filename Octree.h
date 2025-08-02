#pragma once
#include <memory>
#include "SRMath.h"

class RenderQueue;
struct Mesh;

class Octree
{
private:
	friend class Renderer; // Renderer.h에서 OctreeNode를 사용하기 때문에 필요
	class OctreeNode;

	void subdivide(OctreeNode* node);
	void insert(OctreeNode* node, unsigned int triangleIndex);

	std::unique_ptr<OctreeNode> root;
	const Mesh* sourceMesh = nullptr;

	void submitNodeRecursive(RenderQueue& renderQueue, const SRMath::mat4& modelMatrix, const OctreeNode* node);

	static const int MAX_TRIANGLES_PER_NODE = 16;
	static const int MAX_DEPTH = 8;
public:
	Octree();
	~Octree();

	void Build(const Mesh& mesh);
	const OctreeNode* GetRoot() const { return root.get(); }
	void SubmitDebugToRenderQueue(RenderQueue& renderQueue, const SRMath::mat4& modelMatrix);
};


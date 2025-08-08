#pragma once
#include <memory>
#include "Math/SRMath.h"

class RenderQueue;
struct Mesh;
class Frustum;
struct DebugFlags;

class Octree
{
private:
	friend class Renderer; // Renderer.h에서 OctreeNode를 사용하기 때문에 필요
	class OctreeNode;

	void subdivide(OctreeNode* node);
	void insert(OctreeNode* node, unsigned int i0, unsigned int i1, unsigned int i2);

	std::unique_ptr<OctreeNode> root;
	const Mesh* sourceMesh = nullptr;

	void submitNodeRecursive(RenderQueue& renderQueue, const Frustum& frustum, const SRMath::mat4& worldTransform, const DebugFlags& debugFlags, const OctreeNode* node);

	static const int MAX_TRIANGLES_PER_NODE = 16;
	static const int MAX_DEPTH = 8;
public:
	Octree();
	~Octree();

	void Build(const Mesh& mesh);
	const OctreeNode* GetRoot() const { return root.get(); }
	void SubmitNodesToRenderQueue(RenderQueue& renderQueue, const Frustum& frustum, const SRMath::mat4& worldTransform, const DebugFlags& debugFlags);
};


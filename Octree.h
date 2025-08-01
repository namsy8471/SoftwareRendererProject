#pragma once
#include <memory>
#include <vector>
#include "SRMath.h"

struct Mesh;

struct AABB {
	SRMath::vec3 min;
	SRMath::vec3 max;
};

class OctreeNode {
public:
	AABB bounds;
	std::unique_ptr<OctreeNode> children[8];
	std::vector<unsigned int> triangleIndices;

	OctreeNode(const AABB& bounds);
};


class Octree
{
private:
	void subdivide(OctreeNode* node);
	void insert(OctreeNode* node, unsigned int triangleIndex);

	std::unique_ptr<OctreeNode> root;
	const Mesh* sourceMesh = nullptr;

	const int MAX_TRIANGLES_PER_NODE = 16;
	const int MAX_DEPTH = 8;
public:
	void Build(const Mesh& mesh);
	const OctreeNode* GetRoot() const { return root.get(); }
};


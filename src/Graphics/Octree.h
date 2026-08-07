#pragma once
#include <cstddef>
#include <memory>
#include <vector>
#include "Math/SRMath.h"
#include "Renderer/RenderCommand.h"

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

	void submitNodeRecursive(RenderQueue& renderQueue, const Frustum& frustum, const SRMath::mat4& worldTransform,
		std::vector<MeshRenderCommand>& threadLocalCmd, std::vector<DebugPrimitiveCommand>& localDebugCmd,
		const DebugFlags& debugFlags, const OctreeNode* node);



	// 매크로나 별도 정의가 필요한 static const 대신 C++17 inline constexpr를
	// 사용한다. 타입과 값이 선언 위치에 함께 있어 ODR 문제도 없다.
	static constexpr std::size_t max_triangles_per_node = 16;
public:
	Octree();
	~Octree();

	void Build(const Mesh& mesh);
	[[nodiscard]] const OctreeNode* GetRoot() const noexcept { return root.get(); }
	void SubmitNodesToRenderQueue(RenderQueue& renderQueue, const Frustum& frustum, const SRMath::mat4& worldTransform,
		std::vector<MeshRenderCommand>& threadLocalCmd, std::vector<DebugPrimitiveCommand>& localDebugCmd, const DebugFlags& debugFlags);
};

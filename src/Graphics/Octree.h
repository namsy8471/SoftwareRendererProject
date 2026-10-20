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
    class OctreeNode;

    void subdivide(OctreeNode *node);
    void insert(OctreeNode *node, unsigned int i0, unsigned int i1, unsigned int i2);

    std::unique_ptr<OctreeNode> m_root;
    const Mesh *m_sourceMesh = nullptr;

    void submitNodeRecursive(const Frustum &frustum, const SRMath::mat4 &worldTransform,
                             std::vector<MeshRenderCommand> &threadLocalCmd,
                             std::vector<DebugPrimitiveCommand> &localDebugCmd, const DebugFlags &debugFlags,
                             const OctreeNode *node);

    static constexpr std::size_t maxTrianglesPerNode = 16;

  public:
    Octree();
    ~Octree();

    void Build(const Mesh &mesh);
    [[nodiscard]] const OctreeNode *GetRoot() const noexcept { return m_root.get(); }
    void SubmitNodesToRenderQueue(const Frustum &frustum, const SRMath::mat4 &worldTransform,
                                  std::vector<MeshRenderCommand> &threadLocalCmd,
                                  std::vector<DebugPrimitiveCommand> &localDebugCmd,
                                  const DebugFlags &debugFlags);
};

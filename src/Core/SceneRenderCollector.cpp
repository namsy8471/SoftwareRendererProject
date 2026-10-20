#include "Core/SceneRenderCollector.h"
#include "Core/DebugFlags.h"
#include "Graphics/Model.h"
#include "Graphics/Octree.h"
#include "Math/Frustum.h"
#include "Renderer/DebugGizmos.h"
#include "Renderer/RenderQueue.h"
#include "Scene/GameObject.h"

#include <utility>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

void SceneRenderCollector::Collect(std::span<const std::shared_ptr<GameObject>> roots, const Frustum &frustum,
                                   const DebugFlags &flags, RenderQueue &queue)
{
    for (const auto &root : roots)
        if (root)
            Collect(*root, frustum, flags, queue);
}

void SceneRenderCollector::Collect(const GameObject &object, const Frustum &frustum, const DebugFlags &flags,
                                   RenderQueue &queue)
{
    if (object.GetModel() && frustum.IsAABBInFrustum(object.GetWorldAABB()))
        collectOwnMeshes(object, frustum, flags, queue);

    // 부모 AABB는 자식 전체를 포함하지 않는다. 부모가 보이지 않아도 자식은 검사한다.
    for (const auto &child : object.GetChildren())
        Collect(*child, frustum, flags, queue);
}

void SceneRenderCollector::collectOwnMeshes(const GameObject &object, const Frustum &frustum,
                                            const DebugFlags &flags, RenderQueue &queue)
{
    const std::span<const Mesh> meshes = object.GetModel()->GetMeshes();
    const std::size_t meshCount = meshes.size();

    // resize/clear는 병렬 작업 전에만 수행한다. 서로 다른 메시의 batch를
    // 각 작업이 독점하므로 worker 배치가 달라도 최종 제출 순서는 같다.
    if (m_batches.size() < meshCount)
        m_batches.resize(meshCount);
    for (std::size_t i = 0; i < meshCount; ++i)
    {
        auto &batch = m_batches[i];
        batch.commands.clear();
        batch.debugCommands.clear();
        if (flags.bShowNormal)
            batch.normalLines = queue.AcquireDebugVertices();
    }

    tbb::parallel_for(tbb::blocked_range<std::size_t>{0, meshCount},
                      [&](const tbb::blocked_range<std::size_t> &range)
                      {
                          for (std::size_t i = range.begin(); i != range.end(); ++i)
                              collectMeshCommands(object, meshes[i], frustum, flags, m_batches[i]);
                      });

    for (std::size_t i = 0; i < meshCount; ++i)
        for (const auto &command : m_batches[i].commands)
            queue.Submit(command);
    for (std::size_t i = 0; i < meshCount; ++i)
        for (auto &command : m_batches[i].debugCommands)
            queue.Submit(std::move(command));
}

void SceneRenderCollector::collectMeshCommands(const GameObject &object, const Mesh &mesh,
                                               const Frustum &frustum, const DebugFlags &flags,
                                               MeshCommandBatch &batch)
{
    const SRMath::mat4 &world = object.GetWorldMatrix();
    const ERasterizeMode rasterizeMode = flags.bShowWireframe ? ERasterizeMode::Wireframe : ERasterizeMode::Fill;

    if (mesh.octree)
    {
        mesh.octree->ForEachVisibleNode(
            frustum, world,
            [&](std::span<const unsigned int> indices, const AABB &worldBounds)
            {
                if (!indices.empty())
                    batch.commands.push_back(MeshRenderCommand{.sourceMesh = &mesh,
                                                               .indicesToDraw = indices,
                                                               .worldTransform = world,
                                                               .material = &mesh.material,
                                                               .rasterizeMode = rasterizeMode});
                if (flags.bShowAABB)
                    batch.debugCommands.push_back(sr::debug::MakeAABBGizmo(worldBounds));
            });
    }
    else
    {
        batch.commands.push_back(MeshRenderCommand{.sourceMesh = &mesh,
                                                   .indicesToDraw = mesh.indices,
                                                   .worldTransform = world,
                                                   .material = &mesh.material,
                                                   .rasterizeMode = rasterizeMode});
    }

    if (flags.bShowNormal)
    {
        auto &normalLines = batch.normalLines;
        normalLines.clear();
        normalLines.reserve(mesh.vertices.size() * 2); // 각 정점마다 시작점과 끝점이 있으므로 2배 크기
        sr::debug::AppendNormalLines(normalLines, mesh.vertices, world, object.GetNormalMatrix());

        if (!normalLines.empty())
        {
            batch.debugCommands.push_back(DebugPrimitiveCommand{.vertices = std::move(normalLines),
                                                                .worldTransform = SRMath::mat4::identity(),
                                                                .type = DebugPrimitiveType::Line,
                                                                .depthPolicy = DebugDepthPolicy::DepthTested});
        }
    }
}

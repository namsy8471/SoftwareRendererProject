#include "Core/SceneRenderCollector.h"
#include "Animation/Skinning.h"
#include "Core/DebugFlags.h"
#include "Graphics/Model.h"
#include "Graphics/Octree.h"
#include "Math/Frustum.h"
#include "Renderer/DebugGizmos.h"
#include "Renderer/RenderQueue.h"
#include "Scene/GameObject.h"

#include <algorithm>
#include <utility>
#include "Utils/Jobs.h"

void SceneRenderCollector::Collect(std::span<const std::shared_ptr<GameObject>> roots, const Frustum &frustum,
                                   const DebugFlags &flags, RenderQueue &queue)
{
    m_visibleMeshTasks.clear();
    for (const auto &root : roots)
        if (root)
            traverseObject(*root, frustum, flags, queue);
    processVisibleMeshTasks(frustum, flags, queue);
}

void SceneRenderCollector::Collect(const GameObject &object, const Frustum &frustum, const DebugFlags &flags,
                                   RenderQueue &queue)
{
    m_visibleMeshTasks.clear();
    traverseObject(object, frustum, flags, queue);
    processVisibleMeshTasks(frustum, flags, queue);
}

void SceneRenderCollector::traverseObject(const GameObject &object, const Frustum &frustum,
                                          const DebugFlags &flags, RenderQueue &queue)
{
    if (const auto *skinned = object.GetSkinnedMesh())
    {
        // Model의 정적 AABB·Octree는 바인드 포즈 기준이라 쓰지 않는다.
        if (skinned->GetWorldAABB().IsValid() && frustum.IsAABBInFrustum(skinned->GetWorldAABB()))
            collectSkinnedMeshes(*skinned, flags, queue);
    }
    else if (object.GetModel() && frustum.IsAABBInFrustum(object.GetWorldAABB()))
    {
        const std::span<const Mesh> meshes = object.GetModel()->GetMeshes();
        for (const auto &mesh : meshes)
            m_visibleMeshTasks.push_back(VisibleMeshTask{.object = &object, .mesh = &mesh});
    }

    // 부모 AABB는 자식 전체를 포함하지 않는다. 부모가 보이지 않아도 자식은 검사한다.
    for (const auto &child : object.GetChildren())
        if (child)
            traverseObject(*child, frustum, flags, queue);
}

void SceneRenderCollector::processVisibleMeshTasks(const Frustum &frustum,
                                            const DebugFlags &flags, RenderQueue &queue)
{
    const std::size_t taskCount = m_visibleMeshTasks.size();
    if (taskCount == 0)
        return;
    const std::size_t meshCount = taskCount;

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

    sr::jobs::ParallelFor(std::size_t{0}, meshCount,
                          [&](std::size_t first, std::size_t last)
                          {
                              for (std::size_t i = first; i != last; ++i)
                              {
                                  const auto &task = m_visibleMeshTasks[i];
                                  collectMeshCommands(*task.object, *task.mesh, frustum, flags, m_batches[i]);
                              }
                          });

    for (std::size_t i = 0; i < meshCount; ++i)
        for (const auto &command : m_batches[i].commands)
            queue.Submit(command);
    for (std::size_t i = 0; i < meshCount; ++i)
        for (auto &command : m_batches[i].debugCommands)
            queue.Submit(std::move(command));
}

void SceneRenderCollector::collectSkinnedMeshes(const sr::anim::SkinnedMesh &skinned, const DebugFlags &flags,
                                                RenderQueue &queue)
{
    const ERasterizeMode rasterizeMode = flags.bShowWireframe ? ERasterizeMode::Wireframe : ERasterizeMode::Fill;
    const SRMath::mat4 identity = SRMath::mat4::identity();
    const auto primitives = skinned.GetPrimitives();
    for (const auto &primitive : primitives)
        queue.Submit(MeshRenderCommand{.sourceMesh = &primitive.deformed,
                                       .indicesToDraw = primitive.source->indices,
                                       .worldTransform = identity,
                                       .material = &primitive.source->material,
                                       .rasterizeMode = rasterizeMode});
    for (const auto &primitive : primitives)
    {
        if (flags.bShowAABB && primitive.worldBounds.IsValid())
            queue.Submit(sr::debug::MakeAABBGizmo(primitive.worldBounds));
        if (flags.bShowNormal)
        {
            auto normalLines = queue.AcquireDebugVertices();
            normalLines.clear();
            normalLines.reserve(primitive.deformed.vertices.size() * 2);
            sr::debug::AppendNormalLines(normalLines, primitive.deformed.vertices, identity, identity);
            if (!normalLines.empty())
                queue.Submit(DebugPrimitiveCommand{.vertices = std::move(normalLines),
                                                   .worldTransform = identity,
                                                   .type = DebugPrimitiveType::Line,
                                                   .depthPolicy = DebugDepthPolicy::DepthTested});
        }
    }
}

void SceneRenderCollector::collectMeshCommands(const GameObject &object, const Mesh &mesh,
                                               const Frustum &frustum, const DebugFlags &flags,
                                               MeshCommandBatch &batch)
{
    const SRMath::mat4 &world = object.GetWorldMatrix();
    const SRMath::mat4 &normalMatrix = object.GetNormalMatrix();
    const ERasterizeMode rasterizeMode = flags.bShowWireframe ? ERasterizeMode::Wireframe : ERasterizeMode::Fill;

    // 보이는 삼각형을 이 개수씩 명령 하나로 묶는다. 옥트리 노드(최대 16개)마다 명령을 내면 1080p 데모에서
    // 프레임당 명령이 약 1만 1천 개가 되고, 명령마다 renderer 작업 하나와 변환 캐시 키 비교가 붙는다.
    // 삼각형 순서와 primitive ID는 명령을 어떻게 나누든 같다(ID는 제출 순서의 누적 삼각형 번호).
    // 너무 크면 큰 메시 하나가 한 worker를 오래 잡아 분류 단계 부하가 한쪽으로 쏠린다.
    constexpr std::size_t commandTriangleBudget = 1024;
    const auto submitRange = [&](std::span<const unsigned int> indices)
    {
        constexpr std::size_t chunk = commandTriangleBudget * 3;
        for (std::size_t first = 0; first < indices.size(); first += chunk)
            batch.commands.push_back(
                MeshRenderCommand{.sourceMesh = &mesh,
                                  .indicesToDraw = indices.subspan(first, std::min(chunk, indices.size() - first)),
                                  .worldTransform = world,
                                  .material = &mesh.material,
                                  .rasterizeMode = rasterizeMode,
                                  .normalMatrix = &normalMatrix});
    };

    if (mesh.octree && !flags.bShowAABB)
        mesh.octree->ForEachVisibleRange(frustum, world, submitRange);
    else if (mesh.octree)
    {
        // AABB 표시는 보이는 노드마다 상자를 그려야 하므로 노드 단위로 돈다.
        mesh.octree->ForEachVisibleNode(
            frustum, world,
            [&](std::span<const unsigned int> indices, const AABB &worldBounds)
            {
                if (!indices.empty())
                    batch.commands.push_back(MeshRenderCommand{.sourceMesh = &mesh,
                                                               .indicesToDraw = indices,
                                                               .worldTransform = world,
                                                               .material = &mesh.material,
                                                               .rasterizeMode = rasterizeMode,
                                                               .normalMatrix = &normalMatrix});
                if (flags.bShowAABB)
                    batch.debugCommands.push_back(sr::debug::MakeAABBGizmo(worldBounds));
            });
    }
    else
        submitRange(mesh.indices);

    if (flags.bShowNormal)
    {
        auto &normalLines = batch.normalLines;
        normalLines.clear();
        normalLines.reserve(mesh.vertices.size() * 2); // 각 정점마다 시작점과 끝점이 있으므로 2배 크기
        sr::debug::AppendNormalLines(normalLines, mesh.vertices, world, normalMatrix);

        if (!normalLines.empty())
        {
            batch.debugCommands.push_back(DebugPrimitiveCommand{.vertices = std::move(normalLines),
                                                                .worldTransform = SRMath::mat4::identity(),
                                                                .type = DebugPrimitiveType::Line,
                                                                .depthPolicy = DebugDepthPolicy::DepthTested});
        }
    }
}

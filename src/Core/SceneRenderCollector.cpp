#include "Core/SceneRenderCollector.h"
#include "Animation/Skinning.h"
#include "Core/DebugFlags.h"
#include "Graphics/Model.h"
#include "Graphics/Octree.h"
#include "Math/AABB.h"
#include "Math/Frustum.h"
#include "Renderer/DebugGizmos.h"
#include "Renderer/RenderQueue.h"
#include "Scene/GameObject.h"

#include <algorithm>
#include <cmath>
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
        const float lodError = flags.bShowAABB || flags.bDisableLod ? 0.f : allowedLodError(object);
        for (const auto &mesh : meshes)
            m_visibleMeshTasks.push_back(VisibleMeshTask{.object = &object, .mesh = &mesh, .allowedLodError = lodError});
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
                                  collectMeshCommands(*task.object, *task.mesh, task.allowedLodError, frustum, flags,
                                                      m_batches[i]);
                              }
                          });

    for (std::size_t i = 0; i < meshCount; ++i)
        for (const auto &command : m_batches[i].commands)
            queue.Submit(command);
    for (std::size_t i = 0; i < meshCount; ++i)
        for (auto &command : m_batches[i].debugCommands)
            queue.Submit(std::move(command));
}

float SceneRenderCollector::allowedLodError(const GameObject &object) const noexcept
{
    if (m_pixelsPerUnitAtUnitDepth <= 0.f)
        return 0.f;
    // 원근 투영에서 clip w는 시점 깊이다. w는 위치의 일차식이라 상자 위의 최솟값은 축마다
    // 계수 부호에 맞는 쪽(min 또는 max)을 고르면 된다. 가장 가까운 점이 가장 크게 보이므로 보수적이다.
    const AABB &bounds = object.GetWorldAABB();
    const SRMath::mat4 &vp = m_viewProjection;
    float nearestDepth = vp[3][3];
    for (int axis = 0; axis < 3; ++axis)
    {
        const float k = vp[axis][3];
        nearestDepth += std::min(k * bounds.min[axis], k * bounds.max[axis]);
    }
    // 카메라가 상자 안에 있거나 아주 가까우면 원본을 그린다.
    constexpr float minDepth = 1e-3f;
    if (!(nearestDepth > minDepth))
        return 0.f;
    // 로컬 길이 1은 world 행렬의 가장 긴 축 길이만큼 늘어난다(비균일 스케일에서도 과소평가하지 않는다).
    const SRMath::mat4 &world = object.GetWorldMatrix();
    float maxScale2 = 0.f;
    for (int column = 0; column < 3; ++column)
        maxScale2 = std::max(maxScale2, world[column][0] * world[column][0] + world[column][1] * world[column][1] +
                                            world[column][2] * world[column][2]);
    const float pixelsPerLocalUnit = m_pixelsPerUnitAtUnitDepth * std::sqrt(maxScale2) / nearestDepth;
    return pixelsPerLocalUnit > 0.f ? lodErrorPixels / pixelsPerLocalUnit : 0.f;
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

void SceneRenderCollector::collectMeshCommands(const GameObject &object, const Mesh &mesh, float allowedLodError,
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

    // 오차가 허용치 이하인 가장 거친 단계. 단계 오차는 오름차순이다.
    const MeshLod *lod = nullptr;
    for (const auto &candidate : mesh.lods)
    {
        if (candidate.error > allowedLodError)
            break;
        lod = &candidate;
    }

    if (lod)
    {
        // LOD 인덱스에는 옥트리가 없다. 화면에서 작게 보이는 메시라 메시 상자 하나로 컬링한다.
        if (frustum.IsAABBInFrustum(AABBTransformer(world).Transform(mesh.localAABB)))
            submitRange(lod->indices);
    }
    else if (mesh.octree && !flags.bShowAABB)
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

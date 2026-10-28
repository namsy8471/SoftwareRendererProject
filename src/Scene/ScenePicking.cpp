#include "Scene/ScenePicking.h"
#include "Animation/Skinning.h"
#include "Graphics/Mesh.h"
#include "Graphics/Model.h"
#include "Graphics/Octree.h"
#include "Scene/Camera.h"
#include "Scene/GameObject.h"
#include "Scene/Scene.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <span>
#include <utility>

namespace
{
[[nodiscard]] SRMath::vec3 unprojectPoint(const SRMath::mat4 &invViewProjection,
                                          const SRMath::vec4 &ndcPoint) noexcept
{
    const SRMath::vec4 world = invViewProjection * ndcPoint;
    if (std::abs(world.w) > 1e-7f)
    {
        const float invW = 1.0f / world.w;
        return {world.x * invW, world.y * invW, world.z * invW};
    }
    return {world.x, world.y, world.z};
}

void testTriangleRange(const Ray &ray, std::span<const Vertex> vertices,
                       std::span<const unsigned int> indices, float &bestRayT) noexcept
{
    for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
    {
        const unsigned int i0 = indices[i];
        const unsigned int i1 = indices[i + 1];
        const unsigned int i2 = indices[i + 2];
        if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size())
            continue;
        float hitT = 0.f;
        if (RayIntersectsTriangle(ray, vertices[i0].position, vertices[i1].position, vertices[i2].position,
                                  hitT) &&
            hitT < bestRayT)
        {
            bestRayT = hitT;
        }
    }
}

void pickObjectRecursive(const std::shared_ptr<GameObject> &object,
                         const std::shared_ptr<GameObject> &selectableRoot, const Ray &ray,
                         float &bestDistance, std::optional<PickHit> &bestHit)
{
    if (!object)
        return;

    // 자신에게 고유 EntityInfo.id가 있으면 자신이 선택 대상이 되고, glTF 내부 자식 노드처럼
    // EntityInfo.id가 비어 있으면 상위 엔티티(또는 루트 물체)를 선택 대상으로 유지한다.
    const std::shared_ptr<GameObject> &targetObject =
        (!object->GetEntityInfo().id.empty() || !selectableRoot) ? object : selectableRoot;

    if (const auto *skinned = object->GetSkinnedMesh())
    {
        float aabbMin = 0.f, aabbMax = 0.f;
        if (skinned->GetWorldAABB().IsValid() &&
            RayIntersectsAABB(ray, skinned->GetWorldAABB(), aabbMin, aabbMax) && aabbMin <= bestDistance)
        {
            const auto primitives = skinned->GetPrimitives();
            for (std::size_t p = 0; p < primitives.size(); ++p)
            {
                const auto &primitive = primitives[p];
                if (!primitive.source)
                    continue;
                if (primitive.worldBounds.IsValid())
                {
                    float primMin = 0.f, primMax = 0.f;
                    if (!RayIntersectsAABB(ray, primitive.worldBounds, primMin, primMax) ||
                        primMin > bestDistance)
                        continue;
                }
                const auto deformedVertices = skinned->GetDeformedVertices(p);
                float bestTriT = std::numeric_limits<float>::max();
                testTriangleRange(ray, deformedVertices, primitive.source->indices, bestTriT);
                if (bestTriT < std::numeric_limits<float>::max())
                {
                    const SRMath::vec3 worldPoint = ray.origin + ray.direction * bestTriT;
                    const float worldDist = SRMath::length(worldPoint - ray.origin);
                    if (worldDist < bestDistance)
                    {
                        bestDistance = worldDist;
                        bestHit = PickHit{
                            .object = targetObject, .worldPoint = worldPoint, .distance = worldDist};
                    }
                }
            }
        }
    }
    else if (const Model *model = object->GetModel())
    {
        float aabbMin = 0.f, aabbMax = 0.f;
        if (object->GetWorldAABB().IsValid() &&
            RayIntersectsAABB(ray, object->GetWorldAABB(), aabbMin, aabbMax) && aabbMin <= bestDistance)
        {
            if (const auto invWorld = SRMath::inverse(object->GetWorldMatrix()))
            {
                const SRMath::vec3 localOrigin{(*invWorld) * SRMath::vec4(ray.origin, 1.0f)};
                const SRMath::vec3 localDirRaw{(*invWorld) * SRMath::vec4(ray.direction, 0.0f)};
                const float localDirLen = SRMath::length(localDirRaw);
                if (localDirLen > 1e-8f)
                {
                    const Ray localRay{.origin = localOrigin, .direction = localDirRaw / localDirLen};
                    float modelMin = 0.f, modelMax = 0.f;
                    if (!model->GetLocalAABB().IsValid() ||
                        RayIntersectsAABB(localRay, model->GetLocalAABB(), modelMin, modelMax))
                    {
                        float bestLocalT = std::numeric_limits<float>::max();
                        for (const Mesh &mesh : model->GetMeshes())
                        {
                            if (mesh.octree && !mesh.octree->GetNodes().empty())
                            {
                                const auto nodes = mesh.octree->GetNodes();
                                const auto allIndices = mesh.octree->GetIndices();
                                for (std::size_t nodeIdx = 0; nodeIdx < nodes.size();)
                                {
                                    const Octree::FlatNode &node = nodes[nodeIdx];
                                    float nodeMin = 0.f, nodeMax = 0.f;
                                    if (!RayIntersectsAABB(localRay, node.bounds, nodeMin, nodeMax) ||
                                        nodeMin > bestLocalT)
                                    {
                                        nodeIdx = node.subtreeEnd;
                                        continue;
                                    }
                                    testTriangleRange(
                                        localRay, mesh.vertices,
                                        allIndices.subspan(node.indexBegin, node.indexEnd - node.indexBegin),
                                        bestLocalT);
                                    ++nodeIdx;
                                }
                            }
                            else
                            {
                                testTriangleRange(localRay, mesh.vertices, mesh.indices, bestLocalT);
                            }
                        }
                        if (bestLocalT < std::numeric_limits<float>::max())
                        {
                            const SRMath::vec3 localPoint = localRay.origin + localRay.direction * bestLocalT;
                            const SRMath::vec3 worldPoint{object->GetWorldMatrix() *
                                                          SRMath::vec4(localPoint, 1.0f)};
                            const float worldDist = SRMath::length(worldPoint - ray.origin);
                            if (worldDist < bestDistance)
                            {
                                bestDistance = worldDist;
                                bestHit = PickHit{
                                    .object = targetObject, .worldPoint = worldPoint, .distance = worldDist};
                            }
                        }
                    }
                }
            }
        }
    }

    for (const auto &child : object->GetChildren())
        pickObjectRecursive(child, targetObject, ray, bestDistance, bestHit);
}
} // namespace

Ray ScreenPointToRay(float screenX, float screenY, float viewportWidth, float viewportHeight,
                     const ViewParams &view) noexcept
{
    if (viewportWidth <= 0.f || viewportHeight <= 0.f)
        return Ray{.origin = view.position, .direction = {0.f, 0.f, 0.f}};

    const float xNdc = (2.0f * screenX) / viewportWidth - 1.0f;
    const float yNdc = 1.0f - (2.0f * screenY) / viewportHeight;

    const SRMath::mat4 viewProjection = view.projection * view.view;
    if (const auto invVP = SRMath::inverse(viewProjection))
    {
        const SRMath::vec3 nearWorld = unprojectPoint(*invVP, SRMath::vec4(xNdc, yNdc, -1.0f, 1.0f));
        const SRMath::vec3 farWorld = unprojectPoint(*invVP, SRMath::vec4(xNdc, yNdc, 1.0f, 1.0f));
        const SRMath::vec3 dir = farWorld - nearWorld;
        const float len = SRMath::length(dir);
        if (len > 1e-8f)
            return Ray{.origin = view.position, .direction = dir / len};
    }
    return Ray{.origin = view.position, .direction = {0.f, 0.f, 0.f}};
}

Ray ScreenPointToRay(const Camera &camera, float screenX, float screenY, int viewportWidth,
                     int viewportHeight) noexcept
{
    return ScreenPointToRay(screenX, screenY, static_cast<float>(viewportWidth),
                            static_cast<float>(viewportHeight), camera.GetViewParams());
}

bool RayIntersectsAABB(const Ray &ray, const AABB &aabb, float &outTMin, float &outTMax) noexcept
{
    if (!aabb.IsValid())
        return false;

    float tMin = -std::numeric_limits<float>::max();
    float tMax = std::numeric_limits<float>::max();

    for (std::size_t axis = 0; axis < 3; ++axis)
    {
        const float dir = ray.direction[axis];
        const float orig = ray.origin[axis];
        const float boxMin = aabb.min[axis];
        const float boxMax = aabb.max[axis];

        if (std::abs(dir) < 1e-8f)
        {
            if (orig < boxMin || orig > boxMax)
                return false;
        }
        else
        {
            const float invDir = 1.0f / dir;
            float t0 = (boxMin - orig) * invDir;
            float t1 = (boxMax - orig) * invDir;
            if (t0 > t1)
                std::swap(t0, t1);
            tMin = std::max(tMin, t0);
            tMax = std::min(tMax, t1);
            if (tMax < tMin)
                return false;
        }
    }

    if (tMax < 0.0f)
        return false;

    outTMin = std::max(0.0f, tMin);
    outTMax = tMax;
    return true;
}

std::optional<float> RayIntersectsAABB(const Ray &ray, const AABB &aabb) noexcept
{
    float tMin = 0.f, tMax = 0.f;
    if (RayIntersectsAABB(ray, aabb, tMin, tMax))
        return tMin;
    return std::nullopt;
}

bool RayIntersectsTriangle(const Ray &ray, const SRMath::vec3 &v0, const SRMath::vec3 &v1,
                           const SRMath::vec3 &v2, float &outT) noexcept
{
    constexpr float epsilon = 1e-7f;
    const SRMath::vec3 edge1 = v1 - v0;
    const SRMath::vec3 edge2 = v2 - v0;
    const SRMath::vec3 pvec = SRMath::cross(ray.direction, edge2);
    const float det = SRMath::dot(edge1, pvec);
    if (std::abs(det) < epsilon)
        return false;

    const float invDet = 1.0f / det;
    const SRMath::vec3 tvec = ray.origin - v0;
    const float u = SRMath::dot(tvec, pvec) * invDet;
    if (u < 0.0f || u > 1.0f)
        return false;

    const SRMath::vec3 qvec = SRMath::cross(tvec, edge1);
    const float v = SRMath::dot(ray.direction, qvec) * invDet;
    if (v < 0.0f || u + v > 1.0f)
        return false;

    const float t = SRMath::dot(edge2, qvec) * invDet;
    if (t < epsilon)
        return false;

    outT = t;
    return true;
}

std::optional<float> RayIntersectsTriangle(const Ray &ray, const SRMath::vec3 &v0, const SRMath::vec3 &v1,
                                           const SRMath::vec3 &v2) noexcept
{
    float hitT = 0.f;
    if (RayIntersectsTriangle(ray, v0, v1, v2, hitT))
        return hitT;
    return std::nullopt;
}

std::optional<PickHit> PickSceneObject(const Scene &scene, const Ray &ray)
{
    float bestDistance = std::numeric_limits<float>::max();
    std::optional<PickHit> bestHit;
    for (const auto &root : scene.GetRootObjects())
        pickObjectRecursive(root, root, ray, bestDistance, bestHit);
    return bestHit;
}

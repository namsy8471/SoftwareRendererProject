#include "Renderer/Baker/Baker.h"
#include "Graphics/Octree.h"
#include "Scene/ScenePicking.h"
#include "Utils/Jobs.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <span>
#include <vector>

namespace sr::baker
{

namespace
{

bool rayTriangleIntersect(const Ray &ray, const SRMath::vec3 &v0, const SRMath::vec3 &v1,
                          const SRMath::vec3 &v2, float minT, float maxT, float &outT,
                          float &outU, float &outV) noexcept
{
    constexpr float epsilon = 1e-8f;
    const SRMath::vec3 edge1 = v1 - v0;
    const SRMath::vec3 edge2 = v2 - v0;
    const SRMath::vec3 pvec = SRMath::cross(ray.direction, edge2);
    const float det = SRMath::dot(edge1, pvec);
    if (std::abs(det) < epsilon)
        return false;

    const float invDet = 1.0f / det;
    const SRMath::vec3 tvec = ray.origin - v0;
    const float u = SRMath::dot(tvec, pvec) * invDet;
    if (u < -1e-6f || u > 1.0f + 1e-6f)
        return false;

    const SRMath::vec3 qvec = SRMath::cross(tvec, edge1);
    const float v = SRMath::dot(ray.direction, qvec) * invDet;
    if (v < -1e-6f || u + v > 1.0f + 1e-6f)
        return false;

    const float t = SRMath::dot(edge2, qvec) * invDet;
    if (t < minT || t > maxT)
        return false;

    outT = t;
    outU = std::clamp(u, 0.0f, 1.0f);
    outV = std::clamp(v, 0.0f, 1.0f);
    return true;
}

void ensureOctreeBuilt(Mesh &mesh)
{
    if (!mesh.octree || mesh.octree->GetNodes().empty())
    {
        mesh.octree = std::make_unique<Octree>();
        mesh.octree->Build(mesh);
    }
}

void raycastMeshClosest(const Mesh &mesh, const Ray &ray, float minT, HighPolyRayHit &bestHit) noexcept
{
    if (mesh.vertices.empty() || mesh.indices.size() < 3)
        return;

    if (mesh.octree && !mesh.octree->GetNodes().empty())
    {
        const auto nodes = mesh.octree->GetNodes();
        const auto allIndices = mesh.octree->GetIndices();
        for (std::size_t nodeIdx = 0; nodeIdx < nodes.size();)
        {
            const Octree::FlatNode &node = nodes[nodeIdx];
            float nodeMin = 0.f, nodeMax = 0.f;
            if (!RayIntersectsAABB(ray, node.bounds, nodeMin, nodeMax) || nodeMin > bestHit.distance)
            {
                nodeIdx = node.subtreeEnd;
                continue;
            }

            const auto indices = allIndices.subspan(node.indexBegin, node.indexEnd - node.indexBegin);
            for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
            {
                const unsigned int i0 = indices[i];
                const unsigned int i1 = indices[i + 1];
                const unsigned int i2 = indices[i + 2];
                if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() || i2 >= mesh.vertices.size())
                    continue;

                const Vertex &v0 = mesh.vertices[i0];
                const Vertex &v1 = mesh.vertices[i1];
                const Vertex &v2 = mesh.vertices[i2];

                float t = 0.f, u = 0.f, v = 0.f;
                if (rayTriangleIntersect(ray, v0.position, v1.position, v2.position, minT,
                                         bestHit.distance, t, u, v))
                {
                    bestHit.hit = true;
                    bestHit.distance = t;
                    bestHit.point = ray.origin + ray.direction * t;
                    const float w = 1.0f - u - v;
                    SRMath::vec3 n = v0.normal * w + v1.normal * u + v2.normal * v;
                    if (SRMath::lengthSq(n) > 1e-10f)
                        bestHit.normal = SRMath::normalize(n);
                    else
                    {
                        SRMath::vec3 fn = SRMath::cross(v1.position - v0.position, v2.position - v0.position);
                        bestHit.normal = (SRMath::lengthSq(fn) > 1e-10f) ? SRMath::normalize(fn)
                                                                         : SRMath::vec3{0.f, 1.f, 0.f};
                    }
                }
            }
            ++nodeIdx;
        }
    }
    else
    {
        for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
        {
            const unsigned int i0 = mesh.indices[i];
            const unsigned int i1 = mesh.indices[i + 1];
            const unsigned int i2 = mesh.indices[i + 2];
            if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() || i2 >= mesh.vertices.size())
                continue;

            const Vertex &v0 = mesh.vertices[i0];
            const Vertex &v1 = mesh.vertices[i1];
            const Vertex &v2 = mesh.vertices[i2];

            float t = 0.f, u = 0.f, v = 0.f;
            if (rayTriangleIntersect(ray, v0.position, v1.position, v2.position, minT,
                                     bestHit.distance, t, u, v))
            {
                bestHit.hit = true;
                bestHit.distance = t;
                bestHit.point = ray.origin + ray.direction * t;
                const float w = 1.0f - u - v;
                SRMath::vec3 n = v0.normal * w + v1.normal * u + v2.normal * v;
                if (SRMath::lengthSq(n) > 1e-10f)
                    bestHit.normal = SRMath::normalize(n);
                else
                {
                    SRMath::vec3 fn = SRMath::cross(v1.position - v0.position, v2.position - v0.position);
                    bestHit.normal = (SRMath::lengthSq(fn) > 1e-10f) ? SRMath::normalize(fn)
                                                                     : SRMath::vec3{0.f, 1.f, 0.f};
                }
            }
        }
    }
}

bool raycastMeshAny(const Mesh &mesh, const Ray &ray, float minT, float maxT) noexcept
{
    if (mesh.vertices.empty() || mesh.indices.size() < 3)
        return false;

    if (mesh.octree && !mesh.octree->GetNodes().empty())
    {
        const auto nodes = mesh.octree->GetNodes();
        const auto allIndices = mesh.octree->GetIndices();
        for (std::size_t nodeIdx = 0; nodeIdx < nodes.size();)
        {
            const Octree::FlatNode &node = nodes[nodeIdx];
            float nodeMin = 0.f, nodeMax = 0.f;
            if (!RayIntersectsAABB(ray, node.bounds, nodeMin, nodeMax) || nodeMin > maxT)
            {
                nodeIdx = node.subtreeEnd;
                continue;
            }

            const auto indices = allIndices.subspan(node.indexBegin, node.indexEnd - node.indexBegin);
            for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
            {
                const unsigned int i0 = indices[i];
                const unsigned int i1 = indices[i + 1];
                const unsigned int i2 = indices[i + 2];
                if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() || i2 >= mesh.vertices.size())
                    continue;

                float t = 0.f, u = 0.f, v = 0.f;
                if (rayTriangleIntersect(ray, mesh.vertices[i0].position, mesh.vertices[i1].position,
                                         mesh.vertices[i2].position, minT, maxT, t, u, v))
                {
                    return true;
                }
            }
            ++nodeIdx;
        }
    }
    else
    {
        for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
        {
            const unsigned int i0 = mesh.indices[i];
            const unsigned int i1 = mesh.indices[i + 1];
            const unsigned int i2 = mesh.indices[i + 2];
            if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() || i2 >= mesh.vertices.size())
                continue;

            float t = 0.f, u = 0.f, v = 0.f;
            if (rayTriangleIntersect(ray, mesh.vertices[i0].position, mesh.vertices[i1].position,
                                     mesh.vertices[i2].position, minT, maxT, t, u, v))
            {
                return true;
            }
        }
    }

    return false;
}

HighPolyRayHit raycastModelClosest(const Model &model, const Ray &ray, float minT, float maxT) noexcept
{
    HighPolyRayHit bestHit;
    bestHit.distance = maxT;
    for (const Mesh &mesh : model.GetMeshes())
    {
        raycastMeshClosest(mesh, ray, minT, bestHit);
    }
    return bestHit;
}

bool raycastModelAny(const Model &model, const Ray &ray, float minT, float maxT) noexcept
{
    for (const Mesh &mesh : model.GetMeshes())
    {
        if (raycastMeshAny(mesh, ray, minT, maxT))
            return true;
    }
    return false;
}

std::vector<SRMath::vec3> precomputeHemisphereSamples(int sampleCount)
{
    std::vector<SRMath::vec3> samples;
    samples.reserve(sampleCount);

    for (int k = 0; k < sampleCount; ++k)
    {
        const float xi1 = (static_cast<float>(k) + 0.5f) / static_cast<float>(sampleCount);

        std::uint32_t bits = static_cast<std::uint32_t>(k);
        bits = (bits << 16) | (bits >> 16);
        bits = ((bits & 0x55555555) << 1) | ((bits & 0xAAAAAAAA) >> 1);
        bits = ((bits & 0x33333333) << 2) | ((bits & 0xCCCCCCCC) >> 2);
        bits = ((bits & 0x0F0F0F0F) << 4) | ((bits & 0xF0F0F0F0) >> 4);
        bits = ((bits & 0x00FF00FF) << 8) | ((bits & 0xFF00FF00) >> 8);
        const float xi2 = static_cast<float>(bits) * 2.3283064365386963e-10f; // / 2^32

        const float cosTheta = std::sqrt(xi1);
        const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - xi1));
        const float phi = 2.0f * 3.14159265358979323846f * xi2;

        samples.push_back(
            SRMath::vec3{std::cos(phi) * sinTheta, std::sin(phi) * sinTheta, cosTheta});
    }

    return samples;
}

} // namespace

BakeResult Bake(const Model &highPolyModel, const Model &lowPolyModel, const BakeOptions &options)
{
    const int width = options.width;
    const int height = options.height;

    BakeResult result;
    result.width = width;
    result.height = height;

    const std::size_t totalPixels = static_cast<std::size_t>(width) * height;

    result.normalMap.width = width;
    result.normalMap.height = height;
    result.normalMap.pixels.resize(totalPixels, 0x008080FF); // Flat normal default: (128, 128, 255)

    result.aoMap.width = width;
    result.aoMap.height = height;
    result.aoMap.pixels.resize(totalPixels, 0x00FFFFFF); // Fully unoccluded default: (255, 255, 255)

    // Ensure octrees are built for high-poly model
    for (const Mesh &mesh : highPolyModel.GetMeshes())
    {
        ensureOctreeBuilt(const_cast<Mesh &>(mesh));
    }

    // Rasterize low-poly mesh into UV space
    const UVBuffer uvBuffer = RasterizeModelUV(lowPolyModel, width, height);

    // Compute bounding box diagonal of high-poly mesh for adaptive ray offsets
    AABB highBounds = highPolyModel.GetLocalAABB();
    if (!highBounds.IsValid())
    {
        for (const Mesh &mesh : highPolyModel.GetMeshes())
            highBounds.Encapsulate(mesh.localAABB);
    }
    const float diagonal = highBounds.IsValid() ? SRMath::length(highBounds.max - highBounds.min) : 10.0f;

    float cageDist = options.cageDistance;
    if (cageDist <= 0.0f)
        cageDist = std::max(0.05f * diagonal, 0.01f);

    float aoMaxDist = options.aoMaxDistance;
    if (aoMaxDist <= 0.0f)
        aoMaxDist = std::max(diagonal, 1.0f);

    const float aoEpsilon = std::max(1e-4f * diagonal, 1e-4f);

    const int aoSampleCount = std::max(options.aoSamples, 1);
    const auto hemisphereSamples = precomputeHemisphereSamples(aoSampleCount);

    std::vector<std::uint8_t> validMask(totalPixels, 0);

    // Multithreaded row-by-row baking
    sr::jobs::ParallelFor(0, height, 1, [&](int yBegin, int yEnd) {
        for (int y = yBegin; y < yEnd; ++y)
        {
            const int rowOffset = y * width;
            for (int x = 0; x < width; ++x)
            {
                const int idx = rowOffset + x;
                const UVTexelSample &sample = uvBuffer.samples[idx];
                if (!sample.valid)
                    continue;

                validMask[idx] = 1;

                // 1. Tangent-Space Normal Map Baking
                if (options.bakeNormal)
                {
                    const SRMath::vec3 &P = sample.position;
                    const SRMath::vec3 &N = sample.normal;
                    const SRMath::vec3 &T = sample.tangent;
                    const SRMath::vec3 &B = sample.bitangent;

                    // Bidirectional cage ray along low-poly normal
                    const Ray forwardRay{.origin = P, .direction = N};
                    const Ray backwardRay{.origin = P, .direction = -1.0f * N};

                    const HighPolyRayHit hitF = raycastModelClosest(highPolyModel, forwardRay, 0.0f, cageDist);
                    const HighPolyRayHit hitB = raycastModelClosest(highPolyModel, backwardRay, 0.0f, cageDist);

                    SRMath::vec3 highNormal = N;
                    if (hitF.hit && hitB.hit)
                    {
                        highNormal = (hitF.distance <= hitB.distance) ? hitF.normal : hitB.normal;
                    }
                    else if (hitF.hit)
                    {
                        highNormal = hitF.normal;
                    }
                    else if (hitB.hit)
                    {
                        highNormal = hitB.normal;
                    }

                    // Project high-poly normal into low-poly tangent space: n_ts = M_TBN^T * N_high
                    const float nTS_x = SRMath::dot(T, highNormal);
                    const float nTS_y = SRMath::dot(B, highNormal);
                    const float nTS_z = SRMath::dot(N, highNormal);

                    SRMath::vec3 nTS{nTS_x, nTS_y, nTS_z};
                    if (SRMath::lengthSq(nTS) > 1e-10f)
                        nTS = SRMath::normalize(nTS);
                    else
                        nTS = {0.f, 0.f, 1.f};

                    // Encode into RGB [0, 255]: round((n_ts * 0.5 + 0.5) * 255)
                    const auto r = static_cast<std::uint32_t>(
                        std::clamp(static_cast<int>(std::round((nTS.x * 0.5f + 0.5f) * 255.0f)), 0, 255));
                    const auto g = static_cast<std::uint32_t>(
                        std::clamp(static_cast<int>(std::round((nTS.y * 0.5f + 0.5f) * 255.0f)), 0, 255));
                    const auto b = static_cast<std::uint32_t>(
                        std::clamp(static_cast<int>(std::round((nTS.z * 0.5f + 0.5f) * 255.0f)), 0, 255));

                    result.normalMap.pixels[idx] = (r << 16) | (g << 8) | b;
                }

                // 2. Ambient Occlusion (AO) Map Baking
                if (options.bakeAO)
                {
                    const SRMath::vec3 &P = sample.position;
                    const SRMath::vec3 &N = sample.normal;
                    const SRMath::vec3 &T = sample.tangent;
                    const SRMath::vec3 &B = sample.bitangent;

                    const SRMath::vec3 rayOrigin = P + N * aoEpsilon;
                    int unoccludedCount = 0;

                    for (const SRMath::vec3 &sampleDir : hemisphereSamples)
                    {
                        const SRMath::vec3 worldDir = SRMath::normalize(
                            T * sampleDir.x + B * sampleDir.y + N * sampleDir.z);

                        const Ray aoRay{.origin = rayOrigin, .direction = worldDir};
                        if (!raycastModelAny(highPolyModel, aoRay, aoEpsilon, aoMaxDist))
                        {
                            ++unoccludedCount;
                        }
                    }

                    const float visibility = static_cast<float>(unoccludedCount) /
                                             static_cast<float>(aoSampleCount);
                    const auto gray = static_cast<std::uint32_t>(
                        std::clamp(static_cast<int>(std::round(visibility * 255.0f)), 0, 255));

                    result.aoMap.pixels[idx] = (gray << 16) | (gray << 8) | gray;
                }
            }
        }
    });

    // 3. Texel Dilation Filter (Bleed Filter)
    if (options.dilationRadius > 0)
    {
        if (options.bakeNormal)
        {
            std::vector<std::uint8_t> maskCopy = validMask;
            DilateNormalMap(width, height, result.normalMap.pixels, maskCopy, options.dilationRadius);
        }
        if (options.bakeAO)
        {
            std::vector<std::uint8_t> maskCopy = validMask;
            DilateRGB(width, height, result.aoMap.pixels, maskCopy, options.dilationRadius);
        }
    }

    return result;
}

BakeResult Bake(const Mesh &highPolyMesh, const Mesh &lowPolyMesh, const BakeOptions &options)
{
    std::vector<Mesh> highMeshes;
    Mesh highCopy;
    highCopy.vertices = highPolyMesh.vertices;
    highCopy.indices = highPolyMesh.indices;
    highCopy.localAABB = highPolyMesh.localAABB;
    highCopy.ComputeTangents();
    highMeshes.push_back(std::move(highCopy));
    auto highModel = ModelLoader::FromMeshes(std::move(highMeshes));

    std::vector<Mesh> lowMeshes;
    Mesh lowCopy;
    lowCopy.vertices = lowPolyMesh.vertices;
    lowCopy.indices = lowPolyMesh.indices;
    lowCopy.localAABB = lowPolyMesh.localAABB;
    lowCopy.ComputeTangents();
    lowMeshes.push_back(std::move(lowCopy));
    auto lowModel = ModelLoader::FromMeshes(std::move(lowMeshes));

    return Bake(*highModel, *lowModel, options);
}

} // namespace sr::baker

#include "Renderer/Baker/UVSpaceRasterizer.h"

#include <algorithm>
#include <cmath>
#include <span>
#include <vector>

namespace sr::baker
{

namespace
{

struct TriangleTangents
{
    std::vector<SRMath::vec4> tangents; // xyz = tangent, w = handedness
};

TriangleTangents computeTangentsIfMissing(const Mesh &mesh)
{
    TriangleTangents result;
    if (!mesh.GetTangents().empty() && mesh.GetTangents().size() == mesh.vertices.size())
    {
        result.tangents.assign(mesh.GetTangents().begin(), mesh.GetTangents().end());
        return result;
    }

    const std::size_t vertexCount = mesh.vertices.size();
    if (vertexCount == 0)
        return result;

    std::vector<SRMath::vec3> tan1(vertexCount, SRMath::vec3{0.f, 0.f, 0.f});
    std::vector<SRMath::vec3> tan2(vertexCount, SRMath::vec3{0.f, 0.f, 0.f});

    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
    {
        const unsigned int i0 = mesh.indices[i];
        const unsigned int i1 = mesh.indices[i + 1];
        const unsigned int i2 = mesh.indices[i + 2];
        if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount)
            continue;

        const SRMath::vec3 &p0 = mesh.vertices[i0].position;
        const SRMath::vec3 &p1 = mesh.vertices[i1].position;
        const SRMath::vec3 &p2 = mesh.vertices[i2].position;
        const SRMath::vec2 &uv0 = mesh.vertices[i0].texcoord;
        const SRMath::vec2 &uv1 = mesh.vertices[i1].texcoord;
        const SRMath::vec2 &uv2 = mesh.vertices[i2].texcoord;

        const SRMath::vec3 dp1 = p1 - p0;
        const SRMath::vec3 dp2 = p2 - p0;
        const float du1 = uv1.x - uv0.x;
        const float dv1 = uv1.y - uv0.y;
        const float du2 = uv2.x - uv0.x;
        const float dv2 = uv2.y - uv0.y;

        const float det = du1 * dv2 - du2 * dv1;
        if (!std::isfinite(det) || std::abs(det) <= 1e-8f)
            continue;

        const float invDet = 1.0f / det;
        const SRMath::vec3 sdir = (dp1 * dv2 - dp2 * dv1) * invDet;
        const SRMath::vec3 tdir = (dp2 * du1 - dp1 * du2) * invDet;

        tan1[i0] += sdir;
        tan1[i1] += sdir;
        tan1[i2] += sdir;
        tan2[i0] += tdir;
        tan2[i1] += tdir;
        tan2[i2] += tdir;
    }

    result.tangents.resize(vertexCount);
    for (std::size_t v = 0; v < vertexCount; ++v)
    {
        SRMath::vec3 n = mesh.vertices[v].normal;
        if (SRMath::lengthSq(n) > 1e-10f)
            n = SRMath::normalize(n);
        else
            n = {0.f, 1.f, 0.f};

        const SRMath::vec3 &t = tan1[v];
        SRMath::vec3 ortho = t - n * SRMath::dot(n, t);
        const float lenSq = SRMath::lengthSq(ortho);
        if (std::isfinite(lenSq) && lenSq > 1e-10f)
        {
            ortho /= std::sqrt(lenSq);
            const float handedness = (SRMath::dot(SRMath::cross(n, t), tan2[v]) < 0.0f) ? -1.0f : 1.0f;
            result.tangents[v] = SRMath::vec4(ortho, handedness);
        }
        else
        {
            const SRMath::vec3 axis =
                std::abs(n.x) < 0.9f ? SRMath::vec3{1.f, 0.f, 0.f} : SRMath::vec3{0.f, 1.f, 0.f};
            ortho = SRMath::normalize(SRMath::cross(axis, n));
            result.tangents[v] = SRMath::vec4(ortho, 1.0f);
        }
    }

    return result;
}

void rasterizeMeshIntoBuffer(const Mesh &mesh, UVBuffer &buffer)
{
    if (mesh.vertices.empty() || mesh.indices.size() < 3)
        return;

    const auto tangentData = computeTangentsIfMissing(mesh);
    const std::span<const SRMath::vec4> tangents = tangentData.tangents;

    const float fWidth = static_cast<float>(buffer.width);
    const float fHeight = static_cast<float>(buffer.height);

    const std::size_t triangleCount = mesh.indices.size() / 3;
    for (std::size_t t = 0; t < triangleCount; ++t)
    {
        const unsigned int i0 = mesh.indices[t * 3];
        const unsigned int i1 = mesh.indices[t * 3 + 1];
        const unsigned int i2 = mesh.indices[t * 3 + 2];
        if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() || i2 >= mesh.vertices.size())
            continue;

        const Vertex &v0 = mesh.vertices[i0];
        const Vertex &v1 = mesh.vertices[i1];
        const Vertex &v2 = mesh.vertices[i2];

        // UV coordinates in pixel units
        const float x0 = v0.texcoord.x * fWidth;
        const float y0 = v0.texcoord.y * fHeight;
        const float x1 = v1.texcoord.x * fWidth;
        const float y1 = v1.texcoord.y * fHeight;
        const float x2 = v2.texcoord.x * fWidth;
        const float y2 = v2.texcoord.y * fHeight;

        // Bounding box in texels
        const int minX = std::max(0, static_cast<int>(std::floor(std::min({x0, x1, x2}))));
        const int maxX = std::min(buffer.width - 1, static_cast<int>(std::ceil(std::max({x0, x1, x2}))));
        const int minY = std::max(0, static_cast<int>(std::floor(std::min({y0, y1, y2}))));
        const int maxY = std::min(buffer.height - 1, static_cast<int>(std::ceil(std::max({y0, y1, y2}))));

        if (minX > maxX || minY > maxY)
            continue;

        // 2D cross product (determinant)
        const float det = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
        if (std::abs(det) < 1e-9f)
            continue; // Degenerate triangle in UV space

        const float invDet = 1.0f / det;
        const bool ccw = (det > 0.f);

        // Fallback face normal if vertex normals are zero
        SRMath::vec3 faceNormal = SRMath::cross(v1.position - v0.position, v2.position - v0.position);
        if (SRMath::lengthSq(faceNormal) > 1e-10f)
            faceNormal = SRMath::normalize(faceNormal);
        else
            faceNormal = {0.f, 1.f, 0.f};

        const SRMath::vec4 tan0 = (i0 < tangents.size()) ? tangents[i0] : SRMath::vec4{1.f, 0.f, 0.f, 1.f};
        const SRMath::vec4 tan1Val = (i1 < tangents.size()) ? tangents[i1] : SRMath::vec4{1.f, 0.f, 0.f, 1.f};
        const SRMath::vec4 tan2Val = (i2 < tangents.size()) ? tangents[i2] : SRMath::vec4{1.f, 0.f, 0.f, 1.f};

        for (int y = minY; y <= maxY; ++y)
        {
            const float py = static_cast<float>(y) + 0.5f;
            for (int x = minX; x <= maxX; ++x)
            {
                const float px = static_cast<float>(x) + 0.5f;

                // Edge functions
                const float w0 = (x2 - x1) * (py - y1) - (y2 - y1) * (px - x1);
                const float w1 = (x0 - x2) * (py - y2) - (y0 - y2) * (px - x2);
                const float w2 = (x1 - x0) * (py - y0) - (y1 - y0) * (px - x0);

                float l0 = 0.f, l1 = 0.f, l2 = 0.f;
                constexpr float epsilon = 1e-5f;
                if (ccw)
                {
                    if (w0 < -epsilon || w1 < -epsilon || w2 < -epsilon)
                        continue;
                    l0 = std::max(0.0f, w0 * invDet);
                    l1 = std::max(0.0f, w1 * invDet);
                    l2 = std::max(0.0f, w2 * invDet);
                }
                else
                {
                    if (w0 > epsilon || w1 > epsilon || w2 > epsilon)
                        continue;
                    l0 = std::max(0.0f, w0 * invDet);
                    l1 = std::max(0.0f, w1 * invDet);
                    l2 = std::max(0.0f, w2 * invDet);
                }

                const float sum = l0 + l1 + l2;
                if (sum > 1e-6f)
                {
                    const float invSum = 1.0f / sum;
                    l0 *= invSum;
                    l1 *= invSum;
                    l2 *= invSum;
                }

                UVTexelSample &sample = buffer.At(x, y);

                // Interpolate 3D position
                sample.position = v0.position * l0 + v1.position * l1 + v2.position * l2;

                // Interpolate 3D normal
                SRMath::vec3 rawNormal = v0.normal * l0 + v1.normal * l1 + v2.normal * l2;
                if (SRMath::lengthSq(rawNormal) > 1e-10f)
                    sample.normal = SRMath::normalize(rawNormal);
                else
                    sample.normal = faceNormal;

                // Interpolate tangent
                SRMath::vec3 rawTan = SRMath::vec3(tan0.x, tan0.y, tan0.z) * l0 +
                                      SRMath::vec3(tan1Val.x, tan1Val.y, tan1Val.z) * l1 +
                                      SRMath::vec3(tan2Val.x, tan2Val.y, tan2Val.z) * l2;

                // Orthonormalize tangent against normal via Gram-Schmidt
                SRMath::vec3 orthoTan = rawTan - sample.normal * SRMath::dot(sample.normal, rawTan);
                if (SRMath::lengthSq(orthoTan) > 1e-10f)
                {
                    sample.tangent = SRMath::normalize(orthoTan);
                }
                else
                {
                    const SRMath::vec3 axis = std::abs(sample.normal.x) < 0.9f ? SRMath::vec3{1.f, 0.f, 0.f}
                                                                               : SRMath::vec3{0.f, 1.f, 0.f};
                    sample.tangent = SRMath::normalize(SRMath::cross(axis, sample.normal));
                }

                // Interpolate handedness
                const float handedness = (tan0.w * l0 + tan1Val.w * l1 + tan2Val.w * l2) < 0.0f ? -1.0f : 1.0f;

                // Bitangent: B = normalize(cross(N, T)) * handedness
                sample.bitangent = SRMath::normalize(SRMath::cross(sample.normal, sample.tangent)) * handedness;

                sample.coverage = 1.0f;
                sample.valid = true;
                sample.triangleIndex = static_cast<std::uint32_t>(t);
            }
        }
    }
}

} // namespace

UVBuffer RasterizeMeshUV(const Mesh &mesh, int width, int height)
{
    UVBuffer buffer;
    buffer.width = width;
    buffer.height = height;
    buffer.samples.resize(static_cast<std::size_t>(width) * height);
    rasterizeMeshIntoBuffer(mesh, buffer);
    return buffer;
}

UVBuffer RasterizeModelUV(const Model &model, int width, int height)
{
    UVBuffer buffer;
    buffer.width = width;
    buffer.height = height;
    buffer.samples.resize(static_cast<std::size_t>(width) * height);
    for (const Mesh &mesh : model.GetMeshes())
    {
        rasterizeMeshIntoBuffer(mesh, buffer);
    }
    return buffer;
}

} // namespace sr::baker

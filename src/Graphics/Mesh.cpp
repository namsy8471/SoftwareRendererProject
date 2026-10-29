#include "Mesh.h"
#include "Octree.h"

#include <algorithm>
#include <cmath>

Mesh::Mesh() = default;
Mesh::~Mesh() = default;
Mesh::Mesh(Mesh &&) noexcept = default;
Mesh &Mesh::operator=(Mesh &&) noexcept = default;

void Mesh::ComputeTangents()
{
    m_tangents.clear();
    if (vertices.empty())
        return;

    const std::size_t vertexCount = vertices.size();
    std::vector<SRMath::vec3> tan1(vertexCount, SRMath::vec3{0.f, 0.f, 0.f});
    std::vector<SRMath::vec3> tan2(vertexCount, SRMath::vec3{0.f, 0.f, 0.f});

    for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
    {
        const unsigned int i0 = indices[i];
        const unsigned int i1 = indices[i + 1];
        const unsigned int i2 = indices[i + 2];
        if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount)
            continue;

        const SRMath::vec3 &p0 = vertices[i0].position;
        const SRMath::vec3 &p1 = vertices[i1].position;
        const SRMath::vec3 &p2 = vertices[i2].position;
        const SRMath::vec2 &uv0 = vertices[i0].texcoord;
        const SRMath::vec2 &uv1 = vertices[i1].texcoord;
        const SRMath::vec2 &uv2 = vertices[i2].texcoord;

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

    m_tangents.resize(vertexCount);
    for (std::size_t v = 0; v < vertexCount; ++v)
    {
        SRMath::vec3 n = vertices[v].normal;
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
            m_tangents[v] = SRMath::vec4(ortho, handedness);
        }
        else
        {
            const SRMath::vec3 axis =
                std::abs(n.x) < 0.9f ? SRMath::vec3{1.f, 0.f, 0.f} : SRMath::vec3{0.f, 1.f, 0.f};
            ortho = SRMath::normalize(SRMath::cross(axis, n));
            m_tangents[v] = SRMath::vec4(ortho, 1.0f);
        }
    }
}

void Mesh::BuildLODs()
{
    const std::size_t triCount = indices.size() / 3;
    if (triCount <= 2)
    {
        lodIndices = indices;
        lodIndicesFar = indices;
        return;
    }

    lodIndices.clear();
    lodIndices.reserve(((triCount + 1) / 2) * 3);
    for (std::size_t t = 0; t < triCount; t += 2)
    {
        lodIndices.push_back(indices[t * 3]);
        lodIndices.push_back(indices[t * 3 + 1]);
        lodIndices.push_back(indices[t * 3 + 2]);
    }

    lodIndicesFar.clear();
    lodIndicesFar.reserve(((triCount + 2) / 3) * 3);
    for (std::size_t t = 0; t < triCount; t += 3)
    {
        lodIndicesFar.push_back(indices[t * 3]);
        lodIndicesFar.push_back(indices[t * 3 + 1]);
        lodIndicesFar.push_back(indices[t * 3 + 2]);
    }
}

AABB ComputeLocalAABB(const Mesh &mesh) noexcept
{
    if (mesh.vertices.empty())
    {
        return AABB{{0, 0, 0}, {0, 0, 0}}; // 빈 메시의 경우, AABB는 (0,0,0)으로 설정
    }

    AABB bounds;

    for (const auto &vertex : mesh.vertices)
    {
        bounds.min.x = std::min(bounds.min.x, vertex.position.x);
        bounds.min.y = std::min(bounds.min.y, vertex.position.y);
        bounds.min.z = std::min(bounds.min.z, vertex.position.z);

        bounds.max.x = std::max(bounds.max.x, vertex.position.x);
        bounds.max.y = std::max(bounds.max.y, vertex.position.y);
        bounds.max.z = std::max(bounds.max.z, vertex.position.z);
    }
    return bounds;
}

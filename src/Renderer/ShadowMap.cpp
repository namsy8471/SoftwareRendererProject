#include "Renderer/ShadowMap.h"
#include "Renderer/RenderQueue.h"
#include "Renderer/RenderCommand.h"
#include "Graphics/Mesh.h"
#include "Utils/Jobs.h"
#include <algorithm>
#include <cmath>
#include <bit>
#include <intrin.h>

namespace
{
struct ClipPoly
{
    SRMath::vec4 v[16];
    int count = 0;
};

bool isInsidePlane(const SRMath::vec4 &p, int plane) noexcept
{
    switch (plane)
    {
    case 0: return p.x >= -p.w; // Left
    case 1: return p.x <=  p.w; // Right
    case 2: return p.y >= -p.w; // Bottom
    case 3: return p.y <=  p.w; // Top
    case 4: return p.z >= -p.w; // Near
    case 5: return p.z <=  p.w; // Far
    default: return false;
    }
}

SRMath::vec4 intersectPlane(const SRMath::vec4 &a, const SRMath::vec4 &b, int plane) noexcept
{
    float da = 0.0f;
    float db = 0.0f;
    switch (plane)
    {
    case 0: da = a.x + a.w; db = b.x + b.w; break;
    case 1: da = a.w - a.x; db = b.w - b.x; break;
    case 2: da = a.y + a.w; db = b.y + b.w; break;
    case 3: da = a.w - a.y; db = b.w - b.y; break;
    case 4: da = a.z + a.w; db = b.z + b.w; break;
    case 5: da = a.w - a.z; db = b.w - b.z; break;
    default: break;
    }

    const float denom = da - db;
    const float t = (std::abs(denom) > 1e-6f) ? (da / denom) : 0.0f;
    return a + (b - a) * t;
}

void clipPolyAgainstPlane(ClipPoly &outPoly, const ClipPoly &inPoly, int plane) noexcept
{
    outPoly.count = 0;
    if (inPoly.count == 0)
        return;

    for (int i = 0; i < inPoly.count; ++i)
    {
        const SRMath::vec4 &cur = inPoly.v[i];
        const SRMath::vec4 &next = inPoly.v[(i + 1) % inPoly.count];

        const bool curIn = isInsidePlane(cur, plane);
        const bool nextIn = isInsidePlane(next, plane);

        if (curIn)
        {
            if (outPoly.count < 16)
                outPoly.v[outPoly.count++] = cur;
            if (!nextIn && outPoly.count < 16)
                outPoly.v[outPoly.count++] = intersectPlane(cur, next, plane);
        }
        else if (nextIn && outPoly.count < 16)
        {
            outPoly.v[outPoly.count++] = intersectPlane(cur, next, plane);
        }
    }
}
} // namespace

ShadowMap::ShadowMap()
{
    Clear();
}

void ShadowMap::Clear() noexcept
{
    const std::size_t targetSize = static_cast<std::size_t>(width) * height;
    if (depthBuffer.size() != targetSize)
        depthBuffer.assign(targetSize, 1.0f);
    else
        std::fill(depthBuffer.begin(), depthBuffer.end(), 1.0f);
}

void ShadowMap::Render(const RenderQueue &queue, const SRMath::mat4 &lightVP)
{
    lightViewProjection = lightVP;
    Clear();

    const auto &commands = queue.GetRenderCommands();
    if (commands.empty())
        return;

    const float fWidth = static_cast<float>(width);
    const float fHeight = static_cast<float>(height);

    sr::jobs::ParallelFor(std::size_t{0}, commands.size(), [&](std::size_t firstCmd, std::size_t lastCmd) {
        ClipPoly polyA, polyB;

        for (std::size_t c = firstCmd; c < lastCmd; ++c)
        {
            const auto &cmd = commands[c];
            if (cmd.rasterizeMode != ERasterizeMode::Fill || !cmd.sourceMesh || cmd.indicesToDraw.empty())
                continue;

            const Mesh &mesh = *cmd.sourceMesh;
            const auto &indices = cmd.indicesToDraw;
            const auto &vertices = mesh.vertices;
            const SRMath::mat4 mvp = lightViewProjection * cmd.worldTransform;
            const std::size_t triangleCount = indices.size() / 3;

            const auto rasterizeTriangle = [&](const SRMath::vec4 &v0, const SRMath::vec4 &v1, const SRMath::vec4 &v2) {
                const float invW0 = 1.0f / v0.w;
                const float invW1 = 1.0f / v1.w;
                const float invW2 = 1.0f / v2.w;

                const float xn0 = v0.x * invW0, yn0 = v0.y * invW0, zn0 = v0.z * invW0;
                const float xn1 = v1.x * invW1, yn1 = v1.y * invW1, zn1 = v1.z * invW1;
                const float xn2 = v2.x * invW2, yn2 = v2.y * invW2, zn2 = v2.z * invW2;

                float xs0 = (xn0 + 1.0f) * 0.5f * fWidth;
                float ys0 = (1.0f - yn0) * 0.5f * fHeight;
                float ds0 = 0.5f * zn0 + 0.5f;

                float xs1 = (xn1 + 1.0f) * 0.5f * fWidth;
                float ys1 = (1.0f - yn1) * 0.5f * fHeight;
                float ds1 = 0.5f * zn1 + 0.5f;

                float xs2 = (xn2 + 1.0f) * 0.5f * fWidth;
                float ys2 = (1.0f - yn2) * 0.5f * fHeight;
                float ds2 = 0.5f * zn2 + 0.5f;

                float area = (xs1 - xs0) * (ys2 - ys0) - (xs2 - xs0) * (ys1 - ys0);
                if (std::abs(area) < 1e-6f)
                    return;

                if (area < 0.0f)
                {
                    std::swap(xs1, xs2);
                    std::swap(ys1, ys2);
                    std::swap(ds1, ds2);
                    area = -area;
                }

                const float invArea = 1.0f / area;
                const int minX = std::max(0, static_cast<int>(std::floor(std::min({xs0, xs1, xs2}))));
                const int maxX = std::min(width - 1, static_cast<int>(std::ceil(std::max({xs0, xs1, xs2}))));
                const int minY = std::max(0, static_cast<int>(std::floor(std::min({ys0, ys1, ys2}))));
                const int maxY = std::min(height - 1, static_cast<int>(std::ceil(std::max({ys0, ys1, ys2}))));

                if (minX > maxX || minY > maxY)
                    return;

                const float dx12 = xs2 - xs1, dy12 = ys2 - ys1;
                const float dx20 = xs0 - xs2, dy20 = ys0 - ys2;
                const float dx01 = xs1 - xs0, dy01 = ys1 - ys0;
                const float ddx = ((ds1 - ds0) * (ys2 - ys0) - (ds2 - ds0) * (ys1 - ys0)) * invArea;

                for (int y = minY; y <= maxY; ++y)
                {
                    const float py = static_cast<float>(y) + 0.5f;
                    const float px = static_cast<float>(minX) + 0.5f;

                    float w0 = dx12 * (py - ys1) - dy12 * (px - xs1);
                    float w1 = dx20 * (py - ys2) - dy20 * (px - xs2);
                    float w2 = dx01 * (py - ys0) - dy01 * (px - xs0);
                    float d = (w0 * ds0 + w1 * ds1 + w2 * ds2) * invArea;

                    float *rowPtr = &depthBuffer[static_cast<std::size_t>(y) * width + minX];

                    for (int x = minX; x <= maxX; ++x)
                    {
                        if (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f)
                        {
                            const float clampedD = std::clamp(d, 0.0f, 1.0f);
                            const long dBits = static_cast<long>(std::bit_cast<uint32_t>(clampedD));
                            volatile long *targetPixel = reinterpret_cast<volatile long*>(rowPtr);
                            long cur = *targetPixel;
                            while (dBits < cur)
                            {
                                const long prev = _InterlockedCompareExchange(targetPixel, dBits, cur);
                                if (prev == cur)
                                    break;
                                cur = prev;
                            }
                        }

                        w0 -= dy12;
                        w1 -= dy20;
                        w2 -= dy01;
                        d += ddx;
                        ++rowPtr;
                    }
                }
            };

            for (std::size_t t = 0; t < triangleCount; ++t)
            {
                const std::size_t i0 = indices[t * 3];
                const std::size_t i1 = indices[t * 3 + 1];
                const std::size_t i2 = indices[t * 3 + 2];

                if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size())
                    continue;

                const SRMath::vec4 p0 = mvp * SRMath::vec4(vertices[i0].position, 1.0f);
                const SRMath::vec4 p1 = mvp * SRMath::vec4(vertices[i1].position, 1.0f);
                const SRMath::vec4 p2 = mvp * SRMath::vec4(vertices[i2].position, 1.0f);

                // Trivial rejection if all 3 vertices are outside any one plane
                if ((p0.x < -p0.w && p1.x < -p1.w && p2.x < -p2.w) ||
                    (p0.x >  p0.w && p1.x >  p1.w && p2.x >  p2.w) ||
                    (p0.y < -p0.w && p1.y < -p1.w && p2.y < -p2.w) ||
                    (p0.y >  p0.w && p1.y >  p1.w && p2.y >  p2.w) ||
                    (p0.z < -p0.w && p1.z < -p1.w && p2.z < -p2.w) ||
                    (p0.z >  p0.w && p1.z >  p1.w && p2.z >  p2.w))
                {
                    continue;
                }

                // Trivial acceptance fast-path: if all 3 vertices are strictly inside the 6-plane frustum
                const bool allInside =
                    (p0.x >= -p0.w && p0.x <= p0.w && p0.y >= -p0.w && p0.y <= p0.w && p0.z >= -p0.w && p0.z <= p0.w) &&
                    (p1.x >= -p1.w && p1.x <= p1.w && p1.y >= -p1.w && p1.y <= p1.w && p1.z >= -p1.w && p1.z <= p1.w) &&
                    (p2.x >= -p2.w && p2.x <= p2.w && p2.y >= -p2.w && p2.y <= p2.w && p2.z >= -p2.w && p2.z <= p2.w);

                if (allInside)
                {
                    rasterizeTriangle(p0, p1, p2);
                }
                else
                {
                    polyA.count = 3;
                    polyA.v[0] = p0;
                    polyA.v[1] = p1;
                    polyA.v[2] = p2;

                    clipPolyAgainstPlane(polyB, polyA, 0);
                    clipPolyAgainstPlane(polyA, polyB, 1);
                    clipPolyAgainstPlane(polyB, polyA, 2);
                    clipPolyAgainstPlane(polyA, polyB, 3);
                    clipPolyAgainstPlane(polyB, polyA, 4);
                    clipPolyAgainstPlane(polyA, polyB, 5);

                    if (polyA.count < 3)
                        continue;

                    for (int k = 1; k < polyA.count - 1; ++k)
                    {
                        rasterizeTriangle(polyA.v[0], polyA.v[k], polyA.v[k + 1]);
                    }
                }
            }
        }
    });
}

float ShadowMap::SampleVisibility(const SRMath::vec3 &positionWorld,
                                  const SRMath::vec3 &normalWorld,
                                  const SRMath::vec3 &lightDir) const noexcept
{
    if (depthBuffer.empty())
        return 1.0f;

    const SRMath::vec4 clipPos = lightViewProjection * SRMath::vec4(positionWorld, 1.0f);
    if (clipPos.w <= 0.0f)
        return 1.0f;

    const float invW = 1.0f / clipPos.w;
    const float xn = clipPos.x * invW;
    const float yn = clipPos.y * invW;
    const float zn = clipPos.z * invW;

    const float us = 0.5f * xn + 0.5f;
    const float vs = -0.5f * yn + 0.5f;
    const float ds = 0.5f * zn + 0.5f;

    if (us < 0.0f || us > 1.0f || vs < 0.0f || vs > 1.0f || ds < 0.0f || ds > 1.0f)
        return 1.0f;

    const float nDotL = std::max(0.0f, SRMath::dot(normalWorld, lightDir));
    const float bias = std::max(0.0025f * (1.0f - nDotL), 0.0005f);

    const float w = static_cast<float>(width);
    const float h = static_cast<float>(height);
    const float xTex = us * w - 0.5f;
    const float yTex = vs * h - 0.5f;

    const float floorX = std::floor(xTex);
    const float floorY = std::floor(yTex);
    const int x0 = static_cast<int>(floorX);
    const int y0 = static_cast<int>(floorY);
    const int x1 = x0 + 1;
    const int y1 = y0 + 1;

    const float fx = xTex - floorX;
    const float fy = yTex - floorY;

    const int i0 = std::clamp(x0, 0, width - 1);
    const int i1 = std::clamp(x1, 0, width - 1);
    const int j0 = std::clamp(y0, 0, height - 1);
    const int j1 = std::clamp(y1, 0, height - 1);

    const float d00 = depthBuffer[static_cast<std::size_t>(j0) * width + i0];
    const float d10 = depthBuffer[static_cast<std::size_t>(j0) * width + i1];
    const float d01 = depthBuffer[static_cast<std::size_t>(j1) * width + i0];
    const float d11 = depthBuffer[static_cast<std::size_t>(j1) * width + i1];

    const float s00 = (ds - bias <= d00) ? 1.0f : 0.0f;
    const float s10 = (ds - bias <= d10) ? 1.0f : 0.0f;
    const float s01 = (ds - bias <= d01) ? 1.0f : 0.0f;
    const float s11 = (ds - bias <= d11) ? 1.0f : 0.0f;

    const float s0 = s00 * (1.0f - fx) + s10 * fx;
    const float s1 = s01 * (1.0f - fx) + s11 * fx;
    return s0 * (1.0f - fy) + s1 * fy;
}

__m128 ShadowMap::SampleVisibility4(const Lanes3 &normal,
                                    const Lanes3 &position,
                                    const Lanes3 &lightDir) const noexcept
{
    __m128 px = position.x, py = position.y, pz = position.z, pw = _mm_setzero_ps();
    _MM_TRANSPOSE4_PS(px, py, pz, pw);
    SRMath::vec3 p0, p1, p2, p3;
    p0.m128 = px;
    p1.m128 = py;
    p2.m128 = pz;
    p3.m128 = pw;

    __m128 nx = normal.x, ny = normal.y, nz = normal.z, nw = _mm_setzero_ps();
    _MM_TRANSPOSE4_PS(nx, ny, nz, nw);
    SRMath::vec3 n0, n1, n2, n3;
    n0.m128 = nx;
    n1.m128 = ny;
    n2.m128 = nz;
    n3.m128 = nw;

    __m128 lx = lightDir.x, ly = lightDir.y, lz = lightDir.z, lw = _mm_setzero_ps();
    _MM_TRANSPOSE4_PS(lx, ly, lz, lw);
    SRMath::vec3 l0, l1, l2, l3;
    l0.m128 = lx;
    l1.m128 = ly;
    l2.m128 = lz;
    l3.m128 = lw;

    alignas(16) float vis[4];
    vis[0] = SampleVisibility(p0, n0, l0);
    vis[1] = SampleVisibility(p1, n1, l1);
    vis[2] = SampleVisibility(p2, n2, l2);
    vis[3] = SampleVisibility(p3, n3, l3);
    return _mm_load_ps(vis);
}

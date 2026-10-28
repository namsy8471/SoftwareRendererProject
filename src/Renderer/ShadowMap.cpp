#include "Renderer/ShadowMap.h"
#include "Renderer/RenderQueue.h"
#include "Renderer/RenderCommand.h"
#include "Graphics/Mesh.h"
#include "Math/AABB.h"
#include "Utils/Jobs.h"
#include <algorithm>
#include <cmath>
#include <bit>
#include <atomic>
#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace
{
inline void atomicMinDepth(float *depthAddress, float d) noexcept
{
    const uint32_t dBits = std::bit_cast<uint32_t>(d);
#if defined(_MSC_VER)
    volatile long *target = reinterpret_cast<volatile long*>(depthAddress);
    long cur = *target;
    while (static_cast<long>(dBits) < cur)
    {
        const long prev = _InterlockedCompareExchange(target, static_cast<long>(dBits), cur);
        if (prev == cur)
            break;
        cur = prev;
    }
#elif defined(__cpp_lib_atomic_ref)
    std::atomic_ref<uint32_t> target(*reinterpret_cast<uint32_t*>(depthAddress));
    uint32_t cur = target.load(std::memory_order_relaxed);
    while (dBits < cur)
    {
        if (target.compare_exchange_weak(cur, dBits, std::memory_order_relaxed, std::memory_order_relaxed))
            break;
    }
#else
    uint32_t *target = reinterpret_cast<uint32_t*>(depthAddress);
    uint32_t cur = __atomic_load_n(target, __ATOMIC_RELAXED);
    while (dBits < cur)
    {
        if (__atomic_compare_exchange_n(target, &cur, dBits, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED))
            break;
    }
#endif
}

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

struct PoissonSample
{
    float x, y;
};

// 16 precomputed Poisson disk sample offsets on the unit circle
constexpr std::array<PoissonSample, 16> POISSON_DISK_16 = {{
    {-0.94201624f, -0.39906216f},
    { 0.94558609f, -0.76890725f},
    {-0.09418410f, -0.92938870f},
    { 0.34495938f,  0.29387760f},
    {-0.91588581f,  0.45771432f},
    {-0.81544232f, -0.87912464f},
    {-0.38277543f,  0.27676845f},
    { 0.97484398f,  0.75648379f},
    { 0.44323325f, -0.97511554f},
    { 0.53742981f, -0.47373420f},
    {-0.26496911f, -0.41893023f},
    { 0.79197514f,  0.19090188f},
    {-0.24188840f,  0.99706507f},
    {-0.81409955f,  0.91437590f},
    { 0.19984126f,  0.78641367f},
    { 0.14383161f, -0.14100790f}
}};

void rasterizeCommandsToDepth(const RenderQueue &queue,
                              const SRMath::mat4 &lightVP,
                              std::vector<float> &depthBuffer,
                              int width,
                              int height)
{
    const auto &commands = queue.GetRenderCommands();
    if (commands.empty() || depthBuffer.empty())
        return;

    const float fWidth = static_cast<float>(width);
    const float fHeight = static_cast<float>(height);
    const bool isOrtho = (std::abs(lightVP[3][3] - 1.0f) < 1e-5f && std::abs(lightVP[2][3]) < 1e-5f);

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
            const SRMath::mat4 mvp = lightVP * cmd.worldTransform;
            const std::size_t triangleCount = indices.size() / 3;

            const auto rasterizeTriangle = [&](const SRMath::vec4 &v0, const SRMath::vec4 &v1, const SRMath::vec4 &v2) {
                float invW0 = 1.0f, invW1 = 1.0f, invW2 = 1.0f;
                if (!isOrtho)
                {
                    invW0 = 1.0f / v0.w;
                    invW1 = 1.0f / v1.w;
                    invW2 = 1.0f / v2.w;
                }

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
                            atomicMinDepth(rowPtr, clampedD);
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

float sampleSinglePassVisibility(const SRMath::mat4 &lightVP,
                                 const std::vector<float> &depthBuf,
                                 int width,
                                 int height,
                                 const SRMath::vec3 &positionWorld,
                                 const SRMath::vec3 &normalWorld,
                                 const SRMath::vec3 &lightDir,
                                 float filterRadiusTexels) noexcept
{
    if (depthBuf.empty())
        return 1.0f;

    const SRMath::vec4 clipPos = lightVP * SRMath::vec4(positionWorld, 1.0f);
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
    const float bias = std::max(0.0035f * (1.0f - nDotL), 0.0008f);

    const float w = static_cast<float>(width);
    const float h = static_cast<float>(height);
    const float du = filterRadiusTexels / w;
    const float dv = filterRadiusTexels / h;

    float visibility = 0.0f;
    for (int i = 0; i < 16; ++i)
    {
        const float uSample = std::clamp(us + POISSON_DISK_16[i].x * du, 0.0f, 1.0f);
        const float vSample = std::clamp(vs + POISSON_DISK_16[i].y * dv, 0.0f, 1.0f);

        const int px = std::clamp(static_cast<int>(uSample * w), 0, width - 1);
        const int py = std::clamp(static_cast<int>(vSample * h), 0, height - 1);

        const float sampledDepth = depthBuf[static_cast<std::size_t>(py) * width + px];
        if (ds - bias <= sampledDepth)
            visibility += 1.0f;
    }

    return visibility * (1.0f / 16.0f);
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

    if (useCascades)
    {
        for (auto &cascade : cascades)
        {
            if (cascade.depthBuffer.size() != targetSize)
                cascade.depthBuffer.assign(targetSize, 1.0f);
            else
                std::fill(cascade.depthBuffer.begin(), cascade.depthBuffer.end(), 1.0f);
        }
    }
}

void ShadowMap::Render(const RenderQueue &queue, const SRMath::mat4 &lightVP)
{
    lightViewProjection = lightVP;
    useCascades = false;
    Clear();
    rasterizeCommandsToDepth(queue, lightViewProjection, depthBuffer, width, height);
}

void ShadowMap::RenderCascades(const RenderQueue &queue,
                               const SRMath::vec3 &lightDir,
                               const SRMath::mat4 &camView,
                               const SRMath::mat4 &camProj,
                               float shadowNear,
                               float shadowFar,
                               const AABB &/*sceneBounds*/)
{
    useCascades = true;
    cameraView = camView;
    Clear();

    // 1. Calculate cascade split distances using practical split scheme (lambda = 0.75)
    constexpr float lambda = 0.75f;
    std::array<float, cascadeCount + 1> splitDistances;
    splitDistances[0] = shadowNear;
    for (int i = 1; i < cascadeCount; ++i)
    {
        const float p = static_cast<float>(i) / static_cast<float>(cascadeCount);
        const float logDist = shadowNear * std::pow(shadowFar / shadowNear, p);
        const float uniformDist = shadowNear + (shadowFar - shadowNear) * p;
        splitDistances[i] = lambda * logDist + (1.0f - lambda) * uniformDist;
        cascades[i - 1].splitDepth = splitDistances[i];
    }
    splitDistances[cascadeCount] = shadowFar;
    cascades[cascadeCount - 1].splitDepth = shadowFar;

    const auto invCamOpt = SRMath::inverse(camProj * camView);
    if (!invCamOpt)
        return;
    const SRMath::mat4 invCam = *invCamOpt;

    const SRMath::vec3 lightUp = std::abs(lightDir.y) > 0.99f ? SRMath::vec3(0.f, 0.f, 1.f) : SRMath::vec3(0.f, 1.f, 0.f);

    const auto getNDC_Z = [&](float z) {
        return (z * (shadowFar + shadowNear) - 2.0f * shadowFar * shadowNear) / (z * (shadowFar - shadowNear));
    };

    // 2. Render each cascade with texel snapping
    for (int c = 0; c < cascadeCount; ++c)
    {
        const float nearZ = splitDistances[c];
        const float farZ = splitDistances[c + 1];

        const float ndcNearZ = std::clamp(getNDC_Z(nearZ), -1.0f, 1.0f);
        const float ndcFarZ = std::clamp(getNDC_Z(farZ), -1.0f, 1.0f);

        const std::array<SRMath::vec4, 8> ndcCorners = {{
            {-1.f, -1.f, ndcNearZ, 1.f}, { 1.f, -1.f, ndcNearZ, 1.f},
            {-1.f,  1.f, ndcNearZ, 1.f}, { 1.f,  1.f, ndcNearZ, 1.f},
            {-1.f, -1.f, ndcFarZ,  1.f}, { 1.f, -1.f, ndcFarZ,  1.f},
            {-1.f,  1.f, ndcFarZ,  1.f}, { 1.f,  1.f, ndcFarZ,  1.f}
        }};

        SRMath::vec3 frustumCenter(0.f);
        std::array<SRMath::vec3, 8> worldCorners;
        for (int k = 0; k < 8; ++k)
        {
            SRMath::vec4 wc = invCam * ndcCorners[k];
            const float invW = (std::abs(wc.w) > 1e-6f) ? (1.0f / wc.w) : 1.0f;
            worldCorners[k] = SRMath::vec3(wc.x * invW, wc.y * invW, wc.z * invW);
            frustumCenter = frustumCenter + worldCorners[k];
        }
        frustumCenter = frustumCenter * (1.0f / 8.0f);

        float sphereRadius = 0.0f;
        for (int k = 0; k < 8; ++k)
        {
            sphereRadius = std::max(sphereRadius, SRMath::length(worldCorners[k] - frustumCenter));
        }
        sphereRadius = std::max(sphereRadius, 1.0f);

        const float worldUnitsPerTexel = (2.0f * sphereRadius) / static_cast<float>(width);

        const SRMath::vec3 lightEye = frustumCenter + lightDir * (sphereRadius * 3.0f);
        const SRMath::mat4 lightView = SRMath::lookAt(lightEye, frustumCenter, lightUp);

        const SRMath::vec4 centerLS = lightView * SRMath::vec4(frustumCenter, 1.0f);
        const float snappedX = std::floor(centerLS.x / worldUnitsPerTexel) * worldUnitsPerTexel;
        const float snappedY = std::floor(centerLS.y / worldUnitsPerTexel) * worldUnitsPerTexel;

        const float minX = snappedX - sphereRadius;
        const float maxX = snappedX + sphereRadius;
        const float minY = snappedY - sphereRadius;
        const float maxY = snappedY + sphereRadius;
        const float zNearOrtho = 0.1f;
        const float zFarOrtho = sphereRadius * 6.0f;

        const SRMath::mat4 lightProj = SRMath::ortho(minX, maxX, minY, maxY, zNearOrtho, zFarOrtho);
        cascades[c].viewProjection = lightProj * lightView;

        rasterizeCommandsToDepth(queue, cascades[c].viewProjection, cascades[c].depthBuffer, width, height);
    }
}

float ShadowMap::SampleVisibility(const SRMath::vec3 &positionWorld,
                                  const SRMath::vec3 &normalWorld,
                                  const SRMath::vec3 &lightDir,
                                  float viewDepth) const noexcept
{
    if (!useCascades)
    {
        return sampleSinglePassVisibility(lightViewProjection, depthBuffer, width, height,
                                          positionWorld, normalWorld, lightDir, 2.0f);
    }

    float zView = viewDepth;
    if (zView < 0.0f)
    {
        const SRMath::vec4 pView = cameraView * SRMath::vec4(positionWorld, 1.0f);
        zView = -pView.z;
    }

    int cascadeIdx = cascadeCount - 1;
    for (int c = 0; c < cascadeCount; ++c)
    {
        if (zView <= cascades[c].splitDepth)
        {
            cascadeIdx = c;
            break;
        }
    }

    const float split = cascades[cascadeIdx].splitDepth;
    const float blendBand = std::max(1.0f, split * 0.15f);
    const float blendStart = split - blendBand;

    const float vis0 = sampleSinglePassVisibility(cascades[cascadeIdx].viewProjection,
                                                 cascades[cascadeIdx].depthBuffer,
                                                 width, height, positionWorld, normalWorld, lightDir, 2.0f);

    if (cascadeIdx < cascadeCount - 1 && zView > blendStart)
    {
        const float t = std::clamp((zView - blendStart) / blendBand, 0.0f, 1.0f);
        const float vis1 = sampleSinglePassVisibility(cascades[cascadeIdx + 1].viewProjection,
                                                     cascades[cascadeIdx + 1].depthBuffer,
                                                     width, height, positionWorld, normalWorld, lightDir, 2.0f);
        return (1.0f - t) * vis0 + t * vis1;
    }

    return vis0;
}

__m128 ShadowMap::SampleVisibility4(const Lanes3 &normal,
                                    const Lanes3 &position,
                                    const Lanes3 &lightDir) const noexcept
{
    __m128 px = position.x, py = position.y, pz = position.z, pw = _mm_setzero_ps();
    _MM_TRANSPOSE4_PS(px, py, pz, pw);
    SRMath::vec3 p0, p1, p2, p3;
    p0.m128 = px; p1.m128 = py; p2.m128 = pz; p3.m128 = pw;

    __m128 nx = normal.x, ny = normal.y, nz = normal.z, nw = _mm_setzero_ps();
    _MM_TRANSPOSE4_PS(nx, ny, nz, nw);
    SRMath::vec3 n0, n1, n2, n3;
    n0.m128 = nx; n1.m128 = ny; n2.m128 = nz; n3.m128 = nw;

    __m128 lx = lightDir.x, ly = lightDir.y, lz = lightDir.z, lw = _mm_setzero_ps();
    _MM_TRANSPOSE4_PS(lx, ly, lz, lw);
    SRMath::vec3 l0, l1, l2, l3;
    l0.m128 = lx; l1.m128 = ly; l2.m128 = lz; l3.m128 = lw;

    alignas(16) float vis[4];
    vis[0] = SampleVisibility(p0, n0, l0);
    vis[1] = SampleVisibility(p1, n1, l1);
    vis[2] = SampleVisibility(p2, n2, l2);
    vis[3] = SampleVisibility(p3, n3, l3);
    return _mm_load_ps(vis);
}

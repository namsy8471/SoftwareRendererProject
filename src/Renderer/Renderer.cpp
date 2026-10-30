#include "Renderer/Renderer.h"
#include "Renderer/RenderQueue.h"
#include "Renderer/FXAA.h"
#include "Graphics/Light.h"
#include "Graphics/Material.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>
#include <immintrin.h>
#include "Utils/Jobs.h"

namespace
{
using FrameClock = std::chrono::steady_clock;
double elapsedMilliseconds(FrameClock::time_point start)
{
    return std::chrono::duration<double, std::milli>(FrameClock::now() - start).count();
}
} // namespace

void Renderer::ensureViewport(int width, int height)
{
    if (width == m_width && height == m_height)
        return;
    const auto tilesX = (static_cast<std::size_t>(width) + tileSize - 1) / tileSize;
    const auto tilesY = (static_cast<std::size_t>(height) + tileSize - 1) / tileSize;
    if (tilesY != 0 && tilesX > std::numeric_limits<std::size_t>::max() / tilesY)
        throw std::length_error("Viewport tile count exceeded");
    const auto tileCount = tilesX * tilesY;
    if (tileCount == std::numeric_limits<std::size_t>::max())
        throw std::length_error("Viewport tile offset count exceeded");
    // 할당 성공 후 viewport를 교체한다.
    std::vector<RenderStats> stats(tileCount);
    // 비용 기록이 없는 첫 프레임은 합이 0이라 개수로 나눈다.
    const auto chunkCount = (tileCount + tilesPerTask - 1) / tilesPerTask;
    std::vector<std::uint64_t> chunkCost(chunkCount);
    std::vector<std::uint64_t> chunkCostPrefix(chunkCount + 1);
    std::vector<std::uint64_t> occludeeChunkCost(chunkCount);
    std::vector<std::uint8_t> occluderTileDrawn(tileCount);
    std::vector<std::array<float, 16>> occluderBlockMinimum(tileCount);
    m_tileStats.swap(stats);
    m_tileChunkCost.swap(chunkCost);
    m_tileChunkCostPrefix.swap(chunkCostPrefix);
    m_occludeeChunkCost.swap(occludeeChunkCost);
    m_occluderTileDrawn.swap(occluderTileDrawn);
    m_occluderBlockMinimum.swap(occluderBlockMinimum);
    m_occluderTileIds.reset();
    m_occluderTileIdCount = 0;
    m_width = width;
    m_height = height;
    // 빈 target은 진입 전에 거른다. 덧셈 기반 ceil의 int overflow를 피한다.
    // 와이어 band는 프레임 전에 정해지는 고정 높이다. 64행이면 1080p에서 17개라 IronMan이 걸친
    // 몇 band가 선 패스 시간을 좌우했다. 최소 16행, 최대 64개로 나눠 작업을 잘게 나눈다.
    m_lineBandCount = std::min(maximumLineBands, (height - 1) / 16 + 1);
    m_lineBandHeight = (height - 1) / m_lineBandCount + 1;
}

std::span<const PreparedDirectionalLight> Renderer::prepareLights(std::span<const DirectionalLight> lights)
{
    // 이전 프레임의 광원 변경/삭제를 즉시 반영한다. reserve 실패 시 worker를 시작하지 않으며,
    // 부분 준비 상태를 다음 프레임으로 재사용하지 않는다. 같은 Renderer의 동시 호출은 금지한다.
    m_preparedLights.clear();
    m_preparedLights.reserve(lights.size());
    for (const auto &light : lights)
        m_preparedLights.push_back(PreparedDirectionalLight::From(light));
    return m_preparedLights;
}

std::span<const PreparedPointLight> Renderer::preparePointLights(std::span<const PointLight> lights)
{
    m_preparedPointLights.clear();
    m_preparedPointLights.reserve(lights.size());
    for (const auto &light : lights)
        m_preparedPointLights.push_back(PreparedPointLight::From(light));
    return m_preparedPointLights;
}

std::span<const PreparedSpotLight> Renderer::prepareSpotLights(std::span<const SpotLight> lights)
{
    m_preparedSpotLights.clear();
    m_preparedSpotLights.reserve(lights.size());
    for (const auto &light : lights)
        m_preparedSpotLights.push_back(PreparedSpotLight::From(light));
    return m_preparedSpotLights;
}

void Renderer::RenderScene(RenderTarget &target, const RenderQueue &queue, const ViewParams &view,
                           std::span<const DirectionalLight> lights,
                           std::span<const PointLight> pointLights,
                           std::span<const SpotLight> spotLights)
{
    renderScene(target, queue, view, lights, pointLights, spotLights, false);
}

void Renderer::ClearAndRenderScene(RenderTarget &target, const RenderQueue &queue, const ViewParams &view,
                                   std::span<const DirectionalLight> lights,
                                   std::span<const PointLight> pointLights,
                                   std::span<const SpotLight> spotLights)
{
    renderScene(target, queue, view, lights, pointLights, spotLights, true);
}

void Renderer::renderScene(RenderTarget &target, const RenderQueue &queue, const ViewParams &view,
                           std::span<const DirectionalLight> lights,
                           std::span<const PointLight> pointLights,
                           std::span<const SpotLight> spotLights, bool clearTarget)
{
    m_stats = {};
    if (target.Empty())
        return;
    const auto started = FrameClock::now();
    ensureViewport(target.Width(), target.Height());

    const ShadowMap *activeShadowMap = nullptr;
    if (m_options.shadows && (!lights.empty() || !spotLights.empty()) && !queue.GetRenderCommands().empty())
    {
        if (!m_shadowMap)
            m_shadowMap = std::make_unique<ShadowMap>();

        if (!lights.empty())
        {
            m_shadowMap->lightType = ShadowMap::LightType::Directional;
            AABB sceneBounds;
            for (const auto &cmd : queue.GetRenderCommands())
            {
                if (cmd.sourceMesh)
                {
                    const AABB localBox = cmd.sourceMesh->GetLocalAABB().IsValid()
                                              ? cmd.sourceMesh->GetLocalAABB()
                                              : ComputeLocalAABB(*cmd.sourceMesh);
                    sceneBounds.Encapsulate(localBox.Transform(cmd.worldTransform));
                }
            }
            if (!sceneBounds.IsValid() || SRMath::length(sceneBounds.max - sceneBounds.min) < 1e-3f)
            {
                sceneBounds.min = SRMath::vec3(-10.0f, -10.0f, -10.0f);
                sceneBounds.max = SRMath::vec3(10.0f, 10.0f, 10.0f);
            }
            const SRMath::vec3 center = sceneBounds.Center();
            float radius = 0.5f * SRMath::length(sceneBounds.max - sceneBounds.min);
            radius = std::max(radius, 10.0f);

            const PreparedDirectionalLight primaryLight = PreparedDirectionalLight::From(lights[0]);
            const SRMath::vec3 lightDir = primaryLight.directionToLight;
            const SRMath::vec3 eye = center + lightDir * (radius * 2.0f);
            const SRMath::vec3 up =
                std::abs(lightDir.y) > 0.99f ? SRMath::vec3(0.f, 0.f, 1.f) : SRMath::vec3(0.f, 1.f, 0.f);
            const SRMath::mat4 lightView = SRMath::lookAt(eye, center, up);

            const auto corners = sceneBounds.Corners();
            float minX = std::numeric_limits<float>::max();
            float maxX = std::numeric_limits<float>::lowest();
            float minY = std::numeric_limits<float>::max();
            float maxY = std::numeric_limits<float>::lowest();
            float minZ = std::numeric_limits<float>::max();
            float maxZ = std::numeric_limits<float>::lowest();
            for (const auto &c : corners)
            {
                const SRMath::vec4 lp = lightView * SRMath::vec4(c, 1.0f);
                minX = std::min(minX, lp.x);
                maxX = std::max(maxX, lp.x);
                minY = std::min(minY, lp.y);
                maxY = std::max(maxY, lp.y);
                minZ = std::min(minZ, lp.z);
                maxZ = std::max(maxZ, lp.z);
            }
            const float padXY = std::max(1.0f, (maxX - minX) * 0.05f);
            minX -= padXY;
            maxX += padXY;
            minY -= padXY;
            maxY += padXY;
            float zNear = -maxZ - std::max(1.0f, radius * 0.5f);
            float zFar = -minZ + std::max(1.0f, radius * 0.5f);
            if (zNear < 0.1f)
                zNear = 0.1f;
            if (zFar <= zNear + 1.0f)
                zFar = zNear + 20.0f;

            const SRMath::mat4 lightProj = SRMath::ortho(minX, maxX, minY, maxY, zNear, zFar);
            m_shadowMap->lightViewProjection = lightProj * lightView;
            m_shadowMap->RenderCascades(queue, lightDir, view.view, view.projection, 0.2f, 80.0f, sceneBounds);
        }
        else
        {
            m_shadowMap->lightType = ShadowMap::LightType::Spot;
            const PreparedSpotLight spot = PreparedSpotLight::From(spotLights[0]);
            const float range = 1.0f / spot.invRange;
            const SRMath::vec3 rayDir = -1.0f * spot.directionToLight;
            const SRMath::vec3 up =
                std::abs(rayDir.y) > 0.99f ? SRMath::vec3(0.f, 0.f, 1.f) : SRMath::vec3(0.f, 1.f, 0.f);
            const SRMath::mat4 lightView = SRMath::lookAt(spot.position, spot.position + rayDir * range, up);
            const float fov = 2.0f * std::acos(std::clamp(spot.outerConeCos, -1.0f, 1.0f));
            const SRMath::mat4 lightProj = SRMath::perspective(fov, 1.0f, 0.1f, range);
            m_shadowMap->Render(queue, lightProj * lightView);
        }
        activeShadowMap = m_shadowMap.get();
    }

    if (m_options.ibl && !m_defaultIblMap)
    {
        m_defaultIblMap = sr::IBLMap::CreateDefault();
    }
    const sr::IBLMap *activeIblMap = m_options.ibl ? (m_customIblMap ? m_customIblMap.get() : m_defaultIblMap.get()) : nullptr;

    const FrameContext frame{view.projection * view.view,
                             view.position,
                             prepareLights(lights),
                             (m_width + tileSize - 1) / tileSize,
                             (m_height + tileSize - 1) / tileSize,
                             preparePointLights(pointLights),
                             prepareSpotLights(spotLights),
                             m_options.bilinearFiltering,
                             m_options.mipmapping,
                             m_options.srgbGamma,
                             activeShadowMap,
                             m_options.pbr,
                             m_options.ibl,
                             m_options.tonemap,
                             activeIblMap,
                             m_options.shadingModel};
    prepareFrame();
    if (m_options.collectStats)
        m_stats.prepareMs = elapsedMilliseconds(started);
    const bool hasWireframe =
        std::ranges::any_of(queue.GetRenderCommands(), [](const auto &command)
                            { return command.rasterizeMode == ERasterizeMode::Wireframe; });
    const bool needsDepthPrepass = m_options.hiddenWireframe && hasWireframe;
    // 법선 선 band는 자기 행의 타일만 끝나면 그릴 수 있다. 타일 작업이 끝나기를 다 기다리지 않고 같은 작업에서
    // 이어 그린다. 와이어 band가 먼저 그려야 하는 프레임(와이어 + 법선)은 순서를 지키려고 따로 그린다.
    // 두 패스 프레임은 마지막(뒤) 패스에서 그린다.
    m_debugLinesDrawn = false;
    auto stage = FrameClock::now();
    if (planOcclusionPasses(queue, view, hasWireframe))
    {
        // 앞 패스: 가까운 물체만 분류·래스터한다. 타일마다 4x4 블록 최소 깊이를 남긴다.
        binCommands(queue, frame, m_occluderCommands, true);
        if (m_options.collectStats)
        {
            m_stats.binMs = elapsedMilliseconds(stage);
            accumulateBinStats();
        }
        stage = FrameClock::now();
        renderTiles(target, frame, false, clearTarget, false, TilePass::Occluders);
        if (m_options.collectStats)
        {
            m_stats.rasterMs = elapsedMilliseconds(stage);
            for (const auto &stats : m_tileStats)
                m_stats.MergeCounters(stats);
        }
        // 뒤 패스: 나머지 물체. 앞 패스 깊이에 완전히 가려지는 삼각형은 타일 참조를 만들지 않는다.
        stage = FrameClock::now();
        prepareFrame();
        m_occlusionTest = true;
        try
        {
            binCommands(queue, frame, m_occludeeCommands, false);
        }
        catch (...)
        {
            m_occlusionTest = false;
            throw;
        }
        m_occlusionTest = false;
        if (m_options.collectStats)
        {
            m_stats.binMs += elapsedMilliseconds(stage);
            accumulateBinStats();
        }
        stage = FrameClock::now();
        if (m_debugLinePlan == DebugLinePlan::Parallel)
            computeDebugLineBands();
        renderTiles(target, frame, false, clearTarget, m_debugLinePlan == DebugLinePlan::Parallel,
                    TilePass::Occludees);
    }
    else
    {
        binCommands(queue, frame);
        if (m_options.collectStats)
        {
            m_stats.binMs = elapsedMilliseconds(stage);
            accumulateBinStats();
        }
        stage = FrameClock::now();
        if (m_debugLinePlan == DebugLinePlan::Parallel)
            computeDebugLineBands();
        renderTiles(target, frame, needsDepthPrepass, clearTarget,
                    m_debugLinePlan == DebugLinePlan::Parallel && !hasWireframe);
    }
    if (m_options.collectStats)
    {
        m_stats.rasterMs += elapsedMilliseconds(stage);
        for (const auto &stats : m_tileStats)
            m_stats.MergeCounters(stats);
        if (target.HasPrimitiveIds())
            for (auto id : target.PrimitiveIds())
                m_stats.finalFilledPixels += id != RenderTarget::emptyOwner;
        else
            // 그려진 픽셀의 깊이는 양수(1/w > 0)라 빈 값(float 최솟값)과 같지 않다. ID로 센 값과 같다.
            for (auto depth : target.Depth())
                m_stats.finalFilledPixels += depth != RenderTarget::emptyDepth;
    }
    stage = FrameClock::now();
    renderLines(target, queue, frame);
    if (m_options.collectStats)
    {
        m_stats.lineMs = elapsedMilliseconds(stage);
        // scatter/법선 bin의 이번 프레임 할당까지 완료된 후 실제 보유 용량을 센다.
        updateRetainedBinBytes();
    }
    stage = FrameClock::now();
    applyPostProcessing(target);
    if (m_options.collectStats)
        m_stats.postMs = elapsedMilliseconds(stage);
}

void Renderer::accumulateBinStats()
{
    for (auto &scratch : m_workerScratch)
    {
        m_stats.preparedTriangles += scratch.triangles.size();
        m_stats.liveTriangleBytes += scratch.triangles.size() * sizeof(TriangleRef);
        m_stats.retainedTriangleBytes += scratch.triangles.capacity() * sizeof(TriangleRef);
        // 최종 scatter 복사본과 중복 합산하지 않는 논리적 triangle→tile 수다.
        m_stats.binReferences += scratch.tileReferences.size();
        m_stats.occlusionRejected += scratch.occlusionRejected;
        scratch.occlusionRejected = 0;
    }
}

bool Renderer::planOcclusionPasses(const RenderQueue &queue, const ViewParams &view, bool hasWireframe)
{
    m_occluderCommands.clear();
    m_occludeeCommands.clear();
    const auto commands = queue.GetRenderCommands();
    // 작은 장면은 패스를 하나 더 여는 비용(분류·타일 작업의 합류 두 번)이 버리는 삼각형보다 크다.
    constexpr std::size_t minimumCommands = 64;
    constexpr std::size_t minimumTriangles = 65536;
    if (!m_options.occlusionPasses || hasWireframe || commands.size() < minimumCommands ||
        commands.size() > std::numeric_limits<std::uint32_t>::max())
        return false;
    // 혼합 재질은 타일 안에서 뒤에서 앞으로 정렬해 섞는다. 패스를 나누면 그 순서가 깨지므로 하나라도 있으면 끈다.
    std::size_t triangles = 0;
    for (const auto &command : commands)
    {
        if (command.material && command.material->alphaMode == AlphaMode::Blend)
            return false;
        triangles += command.indicesToDraw.size() / 3;
    }
    if (triangles < minimumTriangles)
        return false;

    // 같은 world 행렬의 명령은 한 물체다(수집기가 메시·옥트리 범위별로 잘라 연속으로 낸다). 물체 원점과 카메라의
    // 거리로 가까운 순서를 정한다. 어느 쪽 패스에 들어가도 픽셀은 같으므로 이 기준은 속도에만 영향을 준다.
    m_occlusionObjects.clear();
    m_commandObject.resize(commands.size());
    for (std::uint32_t index = 0; index < commands.size(); ++index)
    {
        const auto &command = commands[index];
        if (m_occlusionObjects.empty() ||
            std::memcmp(&commands[m_occlusionObjects.back().firstCommand].worldTransform, &command.worldTransform,
                        sizeof(SRMath::mat4)) != 0)
        {
            const SRMath::vec4 origin = command.worldTransform * SRMath::vec4(0.f, 0.f, 0.f, 1.f);
            const SRMath::vec3 offset = SRMath::vec3(origin) - view.position;
            m_occlusionObjects.push_back({SRMath::dot(offset, offset), 0, index});
        }
        m_occlusionObjects.back().triangles += command.indicesToDraw.size() / 3;
        m_commandObject[index] = static_cast<std::uint32_t>(m_occlusionObjects.size() - 1);
    }
    if (m_occlusionObjects.size() < 2)
        return false;
    // 가까운 물체부터 삼각형이 절반이 될 때까지 앞 패스에 넣는다. 물체 순서는 거리, 같으면 제출 순서다.
    auto byDistance = m_occlusionObjects;
    std::ranges::sort(byDistance, [](const OcclusionObject &a, const OcclusionObject &b)
                      { return a.distanceSquared != b.distanceSquared ? a.distanceSquared < b.distanceSquared
                                                                     : a.firstCommand < b.firstCommand; });
    // 앞 패스로 고른 물체는 triangles를 0으로 표시한다.
    std::size_t front = 0;
    for (const auto &object : byDistance)
    {
        if (front >= triangles / 2)
            break;
        front += object.triangles;
        m_occlusionObjects[m_commandObject[object.firstCommand]].triangles = 0;
    }
    // 각 패스 안에서는 제출 순서를 지킨다.
    for (std::uint32_t index = 0; index < commands.size(); ++index)
        (m_occlusionObjects[m_commandObject[index]].triangles == 0 ? m_occluderCommands : m_occludeeCommands)
            .push_back(index);
    if (m_occluderCommands.empty() || m_occludeeCommands.empty())
        return false;
    // ID 면이 없는 타깃은 앞 패스 타일의 픽셀 주인 ID를 따로 보관한다(타일당 2KB, 화면 크기가 바뀔 때만
    // 할당). 초기화하지 않으므로 앞 패스가 그린 타일의 페이지만 실제로 쓰인다.
    const auto count = m_tileStats.size() * 256;
    if (m_occluderTileIdCount != count)
    {
        m_occluderTileIds = std::make_unique_for_overwrite<std::uint64_t[]>(count);
        m_occluderTileIdCount = count;
    }
    return true;
}

bool Renderer::isOccludedByOccluders(const PreparedTriangle &t) const noexcept
{
    float upper;
    if (!Rasterizer::ConservativeDepthUpper(t, upper))
        return false;
    // minX..maxY는 화면 안으로 자른 포함 경계다. 래스터라이저는 이 사각형 밖의 픽셀을 쓰지 않는다.
    const int tilesX = (m_width + tileSize - 1) / tileSize;
    for (int ty = t.minY >> 4; ty <= t.maxY >> 4; ++ty)
        for (int tx = t.minX >> 4; tx <= t.maxX >> 4; ++tx)
        {
            const auto tile = static_cast<std::size_t>(ty) * tilesX + static_cast<std::size_t>(tx);
            if (m_occluderTileDrawn[tile] != 2)
                return false;
            const auto &blocks = m_occluderBlockMinimum[tile];
            const int bx0 = (std::max(t.minX, tx << 4) >> 2) & 3, bx1 = (std::min(t.maxX, (tx << 4) + 15) >> 2) & 3;
            const int by0 = (std::max(t.minY, ty << 4) >> 2) & 3, by1 = (std::min(t.maxY, (ty << 4) + 15) >> 2) & 3;
            for (int by = by0; by <= by1; ++by)
                for (int bx = bx0; bx <= bx1; ++bx)
                    if (!(upper < blocks[static_cast<std::size_t>(by * 4 + bx)]))
                        return false;
        }
    return true;
}

void Renderer::prepareTileReferences()
{
    // 예전에는 main thread가 8160타일 x worker 수(1080p, 24스레드면 약 20만 번)를 혼자 돌며 전역 prefix를
    // 만들고 전역 배열을 0으로 resize한 뒤 scatter했다. PC 24스레드에서 이 직렬 구간이 프레임마다 약 0.26ms였고
    // 그동안 나머지 스레드는 놀았다. 이제 worker마다 자기 참조만 자기 배열에 타일별로 모은다(worker끼리 독립).
    // 타일 작업은 활성 worker 순서대로 각 worker의 구간을 이어 붙이므로 타일 안 순서는 예전 전역 배열과 같다.
    // 프레임 경로에서는 binCommands가 각 스레드의 분류 끝에 이미 정렬했다. 여기서는 그 뒤에 참조가 더해진
    // worker(테스트처럼 binCommands 밖에서 채운 경우)만 정렬하므로 보통 병렬 단계를 열지 않는다.
    m_tileReferenceCount = 0;
    if (m_activeTileWorkers.empty())
        return;
    if (!std::ranges::all_of(m_activeTileWorkers, [](const auto *worker) { return worker->tileReferencesSorted; }))
        sr::jobs::ParallelFor(std::size_t{0}, m_activeTileWorkers.size(),
                              [&](std::size_t first, std::size_t last)
                              {
                                  for (auto index = first; index != last; ++index)
                                      if (!m_activeTileWorkers[index]->tileReferencesSorted)
                                          m_activeTileWorkers[index]->SortTileReferences();
                              });
    for (const auto *worker : m_activeTileWorkers)
        m_tileReferenceCount += worker->sortedTileReferences.size();
}

void Renderer::WorkerScratch::SortTileReferences()
{
    // 타일 구간은 touched 순서로 배정한다. 타일 작업은 (시작, 개수)만 읽으므로 타일 번호 순일 필요가 없다.
    std::size_t total = 0;
    for (const auto tile : touchedTileIds)
    {
        auto &state = tileScatter[tile];
        if (state.count > sortedTileReferences.max_size() - total)
            throw std::length_error("Tile reference prefix capacity exceeded");
        state.cursor = total;
        total += state.count;
    }
    // 실패하면 이번 프레임은 래스터를 시작하지 않고 다음 BeginFrame이 count를 초기화한다.
    // clear()를 제거하고 resize()만 수행하여 이전 프레임의 버퍼와 용량을 재사용하며 memset 비용을 제거한다.
    sortedTileReferences.resize(total);

    const auto *const ref = tileReferences.data();
    const auto count = tileReferences.size();
    auto *const sorted = sortedTileReferences.data();
    auto *const scatter = tileScatter.data();

    // 4路 루프 언롤링 및 L1/L2 프리패치로 메모리 파이프라인 지연 은폐
    std::size_t i = 0;
    constexpr std::size_t prefetchDist = 16;
    for (; i + 4 <= count; i += 4)
    {
        if (i + prefetchDist < count)
        {
            _mm_prefetch(reinterpret_cast<const char *>(ref + i + prefetchDist), _MM_HINT_T0);
            _mm_prefetch(reinterpret_cast<const char *>(&scatter[ref[i + 8].tileIndex]), _MM_HINT_T0);
            _mm_prefetch(reinterpret_cast<const char *>(&scatter[ref[i + 9].tileIndex]), _MM_HINT_T0);
            _mm_prefetch(reinterpret_cast<const char *>(&scatter[ref[i + 10].tileIndex]), _MM_HINT_T0);
            _mm_prefetch(reinterpret_cast<const char *>(&scatter[ref[i + 11].tileIndex]), _MM_HINT_T0);
        }

        const auto idx0 = ref[i + 0].tileIndex;
        auto *tri0 = ref[i + 0].triangle;
        sorted[scatter[idx0].cursor++] = tri0;

        const auto idx1 = ref[i + 1].tileIndex;
        auto *tri1 = ref[i + 1].triangle;
        sorted[scatter[idx1].cursor++] = tri1;

        const auto idx2 = ref[i + 2].tileIndex;
        auto *tri2 = ref[i + 2].triangle;
        sorted[scatter[idx2].cursor++] = tri2;

        const auto idx3 = ref[i + 3].tileIndex;
        auto *tri3 = ref[i + 3].triangle;
        sorted[scatter[idx3].cursor++] = tri3;
    }
    for (; i < count; ++i)
        sorted[scatter[ref[i].tileIndex].cursor++] = ref[i].triangle;

    tileReferencesSorted = true;
}

void Renderer::gatherTileReferences(std::size_t tile, std::vector<TriangleRef *> &bin) const
{
    bin.clear();
    for (const auto *worker : m_activeTileWorkers)
    {
        const auto &state = worker->tileScatter[tile];
        if (state.count != 0)
        {
            const auto *end = worker->sortedTileReferences.data() + state.cursor;
            bin.insert(bin.end(), end - state.count, end);
        }
    }
}

void Renderer::renderTileRange(RenderTarget &target, const FrameContext &frame, std::size_t first,
                               std::size_t last, bool needsDepthPrepass, bool clearTiles,
                               std::vector<TriangleRef *> &gathered, TileBuffer &tileBuffer, TilePass pass)
{
    std::vector<const PreparedTriangle *> blendTriangles;
    for (auto index = first; index != last; ++index)
    {
        gatherTileReferences(index, gathered);
        auto bin = std::span<TriangleRef *>(gathered);
        auto &stats = m_tileStats[index];
        stats = {};
        const int x = static_cast<int>(index % frame.tilesX) * tileSize;
        const int y = static_cast<int>(index / frame.tilesX) * tileSize;
        // 타일은 화면을 겹침 없이 덮는다. 빈 타일만 여기서 지운다. 그릴 타일은 Rasterizer가
        // 지운 값으로 시작하는 L1 타일 버퍼에 그리고 끝에 한 번만 쓴다(먼저 지우고 다시 쓰지 않는다).
        if (bin.empty())
        {
            // 뒤 패스의 빈 타일은 앞 패스가 이미 지웠거나 그렸다.
            if (clearTiles && pass != TilePass::Occludees)
                target.ClearRect(x, y, std::min(x + tileSize, m_width), std::min(y + tileSize, m_height));
            if (pass == TilePass::Occluders)
                m_occluderTileDrawn[index] = 0;
            continue;
        }
        if (m_options.frontToBack)
            std::sort(bin.begin(), bin.end(),
                      [](const auto *a, const auto *b)
                      {
                          if (a->maximumInverseW != b->maximumInverseW)
                              return a->maximumInverseW > b->maximumInverseW;
                          return a->primitiveId < b->primitiveId;
                      });
        Rasterizer rasterizer(target, frame, m_options.collectStats ? &stats : nullptr);
        // 뒤 패스는 앞 패스가 그린 타일만 타깃에서 다시 읽는다(나머지 타일은 앞 패스가 지운 그대로다).
        bool cleared = clearTiles;
        const std::uint64_t *idsIn = nullptr;
        std::uint64_t *idsOut = nullptr;
        if (pass == TilePass::Occluders)
            idsOut = m_occluderTileIds.get() + index * 256;
        else if (pass == TilePass::Occludees && m_occluderTileDrawn[index])
        {
            cleared = false;
            idsIn = m_occluderTileIds.get() + index * 256;
        }
        rasterizer.BeginTile({x, y, std::min(x + tileSize, m_width), std::min(y + tileSize, m_height)},
                             m_options.hiZ, cleared, &tileBuffer, idsIn, idsOut);
        blendTriangles.clear();
        if (needsDepthPrepass)
        {
            // 혼합 모드도 모든 면의 깊이를 먼저 완성해야 뒤쪽 채움 색이 남지 않는다.
            for (const auto *triangle : bin)
            {
                const Material *mat = (triangle->sourceCommand && triangle->sourceCommand->material)
                                          ? triangle->sourceCommand->material
                                          : nullptr;
                if (!mat || mat->alphaMode == AlphaMode::Opaque)
                    rasterizer.DrawTriangle(*triangle, RasterPass::DepthOnly);
                else if (mat->alphaMode == AlphaMode::Mask)
                    rasterizer.DrawTriangleAlphaMask(*triangle, RasterPass::DepthOnly);
            }
            for (const auto *triangle : bin)
            {
                if (triangle->sourceCommand->rasterizeMode == ERasterizeMode::Fill)
                {
                    const Material *mat = (triangle->sourceCommand && triangle->sourceCommand->material)
                                              ? triangle->sourceCommand->material
                                              : nullptr;
                    if (!mat || mat->alphaMode == AlphaMode::Opaque)
                        rasterizer.DrawTriangle(*triangle, RasterPass::ShadeVisible);
                    else if (mat->alphaMode == AlphaMode::Mask)
                        rasterizer.DrawTriangleAlphaMask(*triangle, RasterPass::ShadeVisible);
                    else if (mat->alphaMode == AlphaMode::Blend)
                        blendTriangles.push_back(triangle);
                }
            }
        }
        else
        {
            // 겹친 픽셀(평균 1.8번 덮임)을 매번 셰이딩하지 않는다. 깊이 검사를 끝까지 한 뒤
            // 마지막에 색을 썼을 삼각형으로만 픽셀당 한 번 셰이딩한다. 같은 식이라 색이 같다.
            for (const auto *triangle : bin)
            {
                if (triangle->sourceCommand->rasterizeMode == ERasterizeMode::Fill)
                {
                    const Material *mat = (triangle->sourceCommand && triangle->sourceCommand->material)
                                              ? triangle->sourceCommand->material
                                              : nullptr;
                    if (!mat || mat->alphaMode == AlphaMode::Opaque)
                        rasterizer.DrawTriangle(*triangle, RasterPass::DepthOwner);
                    else if (mat->alphaMode == AlphaMode::Mask)
                        rasterizer.DrawTriangleAlphaMask(*triangle, RasterPass::DepthOwner);
                    else if (mat->alphaMode == AlphaMode::Blend)
                        blendTriangles.push_back(triangle);
                }
            }
            rasterizer.ShadeOwners();
        }

        if (!blendTriangles.empty())
        {
            std::sort(blendTriangles.begin(), blendTriangles.end(),
                      [](const PreparedTriangle *a, const PreparedTriangle *b) noexcept
                      {
                          const float avgA = (a->rv0.oneOverW + a->rv1.oneOverW + a->rv2.oneOverW) * (1.0f / 3.0f);
                          const float avgB = (b->rv0.oneOverW + b->rv1.oneOverW + b->rv2.oneOverW) * (1.0f / 3.0f);
                          if (avgA != avgB)
                              return avgA < avgB;
                          return a->primitiveId < b->primitiveId;
                      });
            for (const auto *triangle : blendTriangles)
                rasterizer.DrawTriangleAlphaBlend(*triangle);
        }
        if (pass == TilePass::Occluders)
            // 1: 그렸지만 깊이 요약이 없다(가림 판정에 쓰지 않는다), 2: 그렸고 요약이 있다.
            m_occluderTileDrawn[index] = rasterizer.ComputeBlockMinimums(m_occluderBlockMinimum[index]) ? 2 : 1;
    }
}

void Renderer::renderTiles(RenderTarget &target, const FrameContext &frame, bool needsDepthPrepass,
                           bool clearTiles, bool drawLineBands, TilePass pass)
{
    // scatter join 뒤에만 각 타일의 span을 정렬/래스터한다.
    prepareTileReferences();
    if (m_tileReferenceCount == 0)
    {
        // 빈 X-ray 프레임에는 타일 작업을 만들지 않되 이전 통계는 지운다.
        std::ranges::fill(m_tileStats, RenderStats{});
        if (clearTiles && pass != TilePass::Occludees)
            target.Clear();
        if (pass == TilePass::Occluders)
            std::ranges::fill(m_occluderTileDrawn, std::uint8_t{0});
        return;
    }
    // auto_partitioner는 1080p 8160타일을 스레드 수에 맞춰 크게 나눈 뒤 훔칠 때만 쪼갠다. IronMan이 걸친
    // 중앙 타일은 다른 타일의 수십 배라 큰 조각 하나가 끝을 늦췄다(PC 24스레드 타일 단계 사용률 76%).
    // 4타일(tilesPerTask)씩 묶어 처음부터 잘게 나눈다. 타일은 서로 독립이라 결과는 같다.
    // 스레드마다 화면 순서로 이어진 묶음 구간을 주되, 개수가 아니라 지난 프레임 시간이 같아지게 경계를 정한다.
    // 개수로 나누면 IronMan이 걸친 중앙 구간을 맡은 스레드가 끝까지 남고, 나머지는 훔친 뒤 기다린다.
    // 비싼 묶음부터 하나씩 나눠 주는 LPT(1차)는 이웃 묶음이 스레드마다 흩어져 더 느렸다. 순서와 무관하게 픽셀은
    // 같다.
    const auto tileCount = m_tileStats.size();
    auto &chunkCost = pass == TilePass::Occludees ? m_occludeeChunkCost : m_tileChunkCost;
    std::uint64_t total = 0;
    for (std::size_t chunk = 0; chunk != chunkCost.size(); ++chunk)
    {
        m_tileChunkCostPrefix[chunk] = total;
        total += chunkCost[chunk];
    }
    m_tileChunkCostPrefix.back() = total;
    const auto renderChunks = [&](std::size_t firstChunk, std::size_t lastChunk)
    {
        auto &gathered = m_tileGather.Local();
        auto &tileBuffer = m_tileBuffers.Local();
        for (auto chunk = firstChunk; chunk != lastChunk; ++chunk)
        {
            const auto started = FrameClock::now();
            const auto first = chunk * tilesPerTask;
            renderTileRange(target, frame, first, std::min(first + tilesPerTask, tileCount),
                            needsDepthPrepass, clearTiles, gathered, tileBuffer, pass);
            // 다음 프레임 경계를 정할 비용. 묶음마다 이 스레드만 쓴다.
            chunkCost[chunk] = static_cast<std::uint64_t>((FrameClock::now() - started).count());
        }
    };
    const int bands = drawLineBands ? m_debugLineBands : 0;
    if (bands == 0)
    {
        sr::jobs::ParallelForWeighted(m_tileChunkCostPrefix, renderChunks);
        return;
    }

    // 법선 선 band b는 자기 행 [start[b], start[b+1])을 덮는 타일 행이 모두 끝나야 그린다. 예전에는 타일 작업 전체가
    // 끝나기를 기다린 뒤 band 작업을 따로 열었다. 이제 타일 조각을 더 가져갈 수 없게 된 스레드(타일 작업의 끝
    // 대기 시간)가 준비된 band부터 그린다. 타일 분배는 위와 같다.
    const auto tilesX = static_cast<std::size_t>(frame.tilesX);
    const auto tilesY = static_cast<std::size_t>(frame.tilesY);
    if (m_tileRowRemaining.size() != tilesY)
        std::vector<std::atomic<std::uint32_t>>(tilesY).swap(m_tileRowRemaining);
    for (auto &remaining : m_tileRowRemaining)
        remaining.store(static_cast<std::uint32_t>(tilesX), std::memory_order_relaxed);
    std::array<int, maximumLineBands> bandFirstRow{}, bandLastRow{};
    std::array<std::atomic<int>, maximumLineBands> bandPending{}, ready{};
    // 서로 다른 스레드가 고치는 카운터는 캐시 줄을 나눠 쓰지 않게 떨어뜨린다(alignas 없이 채움).
    struct Counters
    {
        std::atomic<int> readyTail{0};
        char pad[64 - sizeof(std::atomic<int>)];
        std::atomic<int> readyHead{0};
        std::atomic<bool> aborted{false};
    } counters;
    const auto publish = [&](int band)
    {
        const int position = counters.readyTail.fetch_add(1, std::memory_order_relaxed);
        ready[static_cast<std::size_t>(position)].store(band, std::memory_order_release);
    };
    std::ranges::fill(m_debugLineBandStats, RenderStats{});
    for (int band = 0; band < bands; ++band)
    {
        const int minY = m_debugLineBandStart[band], maxY = m_debugLineBandStart[band + 1];
        ready[static_cast<std::size_t>(band)].store(-1, std::memory_order_relaxed);
        bandFirstRow[band] = minY / tileSize;
        bandLastRow[band] = maxY > minY ? (maxY - 1) / tileSize : -1;
        bandPending[band].store(std::max(0, bandLastRow[band] - bandFirstRow[band] + 1), std::memory_order_relaxed);
    }
    for (int band = 0; band < bands; ++band)
        if (bandPending[band].load(std::memory_order_relaxed) == 0)
            publish(band);
    // 행의 마지막 타일을 끝낸 스레드가 그 행을 덮는 band의 남은 행 수를 줄인다. 타일 픽셀 쓰기는
    // 행 카운터(acq_rel) → band 카운터(acq_rel) → ready(release/acquire) 순서로 band를 그리는 스레드에 보인다.
    const auto finishTiles = [&](std::size_t first, std::size_t last)
    {
        while (first != last)
        {
            const auto row = first / tilesX;
            const auto rowEnd = std::min(last, (row + 1) * tilesX);
            const auto finished = static_cast<std::uint32_t>(rowEnd - first);
            if (m_tileRowRemaining[row].fetch_sub(finished, std::memory_order_acq_rel) == finished)
                for (int band = 0; band < bands; ++band)
                    if (bandFirstRow[band] <= static_cast<int>(row) && static_cast<int>(row) <= bandLastRow[band] &&
                        bandPending[band].fetch_sub(1, std::memory_order_acq_rel) == 1)
                        publish(band);
            first = rowEnd;
        }
    };
    sr::jobs::ParallelForWeighted(
        m_tileChunkCostPrefix,
        [&](std::size_t firstChunk, std::size_t lastChunk)
        {
            try
            {
                renderChunks(firstChunk, lastChunk);
            }
            catch (...)
            {
                // 끝나지 않을 행의 band를 기다리는 스레드를 풀어 준다.
                counters.aborted.store(true, std::memory_order_relaxed);
                throw;
            }
            finishTiles(firstChunk * tilesPerTask, std::min(lastChunk * tilesPerTask, tileCount));
        },
        [&]
        {
            // 타일 조각이 모두 나갔다. 아직 그리는 타일은 다른 스레드가 맡고 있으므로 남은 band는 곧 준비된다.
            for (unsigned spin = 0;;)
            {
                auto head = counters.readyHead.load(std::memory_order_relaxed);
                if (head >= bands || counters.aborted.load(std::memory_order_relaxed))
                    return;
                const int band = ready[static_cast<std::size_t>(head)].load(std::memory_order_acquire);
                if (band < 0)
                {
                    // 래스터 중인 스레드와 코어 자원을 오래 다투지 않도록 일정 시간 뒤에는 양보한다.
                    if (++spin < 4096)
                        _mm_pause();
                    else
                        std::this_thread::yield();
                    continue;
                }
                if (counters.readyHead.compare_exchange_weak(head, head + 1, std::memory_order_relaxed))
                {
                    drawDebugLineBand(target, frame, band);
                    spin = 0;
                }
            }
        });
    // 조각을 하나도 맡지 못한 스레드는 band를 돕지 않는다. 남은 band는(보통 없음) 여기서 그린다.
    for (auto head = counters.readyHead.load(); head < bands; ++head)
        drawDebugLineBand(target, frame, ready[static_cast<std::size_t>(head)].load(std::memory_order_acquire));
    m_debugLinesDrawn = true;
}

void Renderer::renderLines(RenderTarget &target, const RenderQueue &queue, const FrameContext &frame)
{
    Rasterizer rasterizer(target, frame, m_options.collectStats ? &m_stats : nullptr);
    // 선은 완성된 면 깊이를 읽기만 한다. wire는 서로 다른 행을 맡겨
    // 색상 쓰기 경합 없이 분산하고, debug 명령의 겹침 순서는 뒤에서 보존한다.
    const bool hasWireframe =
        std::ranges::any_of(queue.GetRenderCommands(), [](const auto &command)
                            { return command.rasterizeMode == ERasterizeMode::Wireframe; });
    if (hasWireframe)
    {
        std::array<RenderStats, maximumLineBands> bandStats{};
        // 풀은 binCommands 완료 뒤 읽기 전용이다. 선의 기울기/오차는 원래
        // 끝점 기준이며 band는 쓰기 영역만 제한해 경계 픽셀도 동일하다.
        sr::jobs::ParallelFor(0, m_lineBandCount,
                              [&](int first, int last)
                              {
                                  for (int band = first; band != last; ++band)
                                  {
                                      const int minY = band * m_lineBandHeight;
                                      const int maxY = minY + std::min(m_lineBandHeight, m_height - minY);
                                      Rasterizer lines(target, frame,
                                                       m_options.collectStats ? &bandStats[band] : nullptr);
                                      lines.BeginLineRows(minY, maxY);
                                      for (const auto &scratch : m_workerScratch)
                                          for (const auto *triangle : scratch.wireBins[band])
                                              lines.DrawWireTriangle(*triangle, m_currentLineAlgorithm,
                                                                     m_options.hiddenWireframe);
                                  }
                              });
        if (m_options.collectStats)
            for (int band = 0; band < m_lineBandCount; ++band)
                m_stats.MergeCounters(bandStats[band]);
    }
    renderDepthTestedLines(target, queue, frame);
    // 행 병렬 작업이 join된 뒤 overlay를 원래 명령 순서로 표시한다.
    for (const auto &command : queue.GetDebugCommands())
        if (command.depthPolicy == DebugDepthPolicy::Overlay)
            rasterizer.DrawDebugPrimitive(command, m_currentLineAlgorithm);
    if (m_options.collectStats)
    {
        m_stats.liveDebugLineBytes =
            m_preparedDebugLines.size() * sizeof(decltype(m_preparedDebugLines)::value_type) +
            m_debugLineCommands.size() * sizeof(DebugLineCommand) +
            m_debugLineChunks.size() * sizeof(DebugLineChunk);
        m_stats.retainedDebugLineBytes =
            m_preparedDebugLines.capacity() * sizeof(decltype(m_preparedDebugLines)::value_type) +
            m_debugLineCommands.capacity() * sizeof(DebugLineCommand) +
            m_debugLineChunks.capacity() * sizeof(DebugLineChunk);
    }
}

void Renderer::updateRetainedBinBytes()
{
    // capacity 기반 저장소 계측이다. allocator의 내부 오버헤드/RSS는 포함하지
    // 않지만 private 참조/최종 포인터, count/cursor/offset, 헤더/목록은 모두 센다.
    std::uint64_t bytes = sizeof(m_activeTileWorkers) +
                          m_activeTileWorkers.capacity() * sizeof(WorkerScratch *);
    for (const auto &scratch : m_workerScratch)
    {
        // 쉬고 있는 과거 worker의 보유 용량도 실제 메모리이므로 빠뜨리지 않는다.
        bytes += sizeof(scratch.tileReferences) + scratch.tileReferences.capacity() * sizeof(TileReference) +
                 sizeof(scratch.tileScatter) + scratch.tileScatter.capacity() * sizeof(TileScatterState) +
                 sizeof(scratch.sortedTileReferences) +
                 scratch.sortedTileReferences.capacity() * sizeof(TriangleRef *) +
                 sizeof(scratch.touchedTileIds) + scratch.touchedTileIds.capacity() * sizeof(std::size_t) +
                 sizeof(scratch.wireBins);
        for (const auto &bin : scratch.wireBins)
            bytes += bin.capacity() * sizeof(TriangleRef *);
    }
    bytes += sizeof(m_debugLineRows) + m_debugLineRows.capacity() * sizeof(DebugLineRows) +
             m_debugLineRowWeights.capacity() * sizeof(std::uint64_t);
    for (const auto &counts : m_debugLineRowCounts)
        bytes += counts.capacity() * sizeof(std::uint32_t);
    for (const auto &gathered : m_tileGather)
        bytes += gathered.capacity() * sizeof(TriangleRef *);
    m_stats.retainedBinBytes = bytes;
}

void Renderer::planDepthTestedLines(const RenderQueue &queue, const FrameContext &frame)
{
    // 명령 포인터와 bin은 프레임마다 비운다. 준비 슬롯은 큰 프레임에서
    // resize한 현재 범위 전체를 병렬 덮어쓰므로 중복된 전체 초기화는 하지 않는다.
    m_debugLineCommands.clear();
    m_debugLineChunks.clear();
    m_debugLinePlan = DebugLinePlan::Serial;

    const auto commands = queue.GetDebugCommands();
    std::size_t totalLines = 0;
    for (const auto &command : commands)
        if (command.depthPolicy == DebugDepthPolicy::DepthTested && command.type == DebugPrimitiveType::Line)
        {
            const auto lineCount = command.vertices.size() / 2;
            if (lineCount > m_preparedDebugLines.max_size() - totalLines)
                throw std::length_error("Debug line preparation capacity exceeded");
            totalLines += lineCount;
        }

    // 256개 미만은 작업 분배/배열 준비 비용을 피하고 기존 순차 기준 경로를 쓴다.
    if (totalLines < debugLineChunkSize)
    {
        // 작은/빈 프레임에는 준비 패스가 없다. 이전 슬롯이 남아 통계나 다음
        // 처리에 재사용되지 않도록 이 경로에서만 논리 크기를 비운다.
        m_preparedDebugLines.clear();
        m_debugLineRows.clear();
        return;
    }

    // 현재 선 개수로 정확히 맞춘다. 줄어든 tail은 제거하고, 같은 크기면
    // 재초기화를 생략한다. 아래 무조건 대입은 기각된 선도 nullopt로 덮어쓴다.
    // 준비 중 예외가 나면 join 뒤 bin/그리기로 진행하지 않고 다음 프레임에 복구한다.
    m_preparedDebugLines.resize(totalLines);
    m_debugLineRows.resize(totalLines);
    std::size_t firstLine = 0;
    for (const auto &command : commands)
    {
        if (command.depthPolicy != DebugDepthPolicy::DepthTested || command.type != DebugPrimitiveType::Line)
            continue;
        const auto lineCount = command.vertices.size() / 2;
        if (lineCount == 0)
            continue;
        const auto commandIndex = m_debugLineCommands.size();
        m_debugLineCommands.push_back(
            {&command, frame.viewProjection * command.worldTransform, firstLine, lineCount});
        for (std::size_t line = 0; line < lineCount;)
        {
            const auto count = std::min(debugLineChunkSize, lineCount - line);
            m_debugLineChunks.push_back({commandIndex, line, count});
            line += count;
        }
        firstLine += lineCount;
    }
    for (auto &counts : m_debugLineRowCounts)
        counts.assign(static_cast<std::size_t>(m_height), 0u);
    m_debugLinePlan = DebugLinePlan::Parallel;
}

Renderer::DebugLineRows Renderer::debugLineRowRange(
    const std::optional<Rasterizer::PreparedDebugLine> &prepared) const noexcept
{
    // 선이 걸친 행 구간. 화면 밖/기각 선은 first > last인 빈 구간이다.
    // 과대한 viewport/긴 DDA는 누적 오차를 bbox로 단정하지 않고 모든
    // band에 보낸다. 실제 쓰기는 원래 시작점으로 진행하며 행 범위로 제한한다.
    constexpr DebugLineRows noRows{std::numeric_limits<int>::max(), std::numeric_limits<int>::min()};
    constexpr DebugLineRows allRows{std::numeric_limits<int>::min(), std::numeric_limits<int>::max()};
    if (!prepared)
        return noRows;
    const auto &line = *prepared;
    const int startY = static_cast<int>(line.screenA.y), endY = static_cast<int>(line.screenB.y);
    const int startX = static_cast<int>(line.screenA.x), endX = static_cast<int>(line.screenB.x);
    const bool longDda = m_currentLineAlgorithm == ELineAlgorithm::DDA &&
                         std::max(std::abs(endX - startX), std::abs(endY - startY)) > (1 << 20);
    if (m_width > 524288 || m_height > 524288 || longDda)
        return allRows;
    const int minY = std::min(startY, endY), maxY = std::max(startY, endY);
    if (maxY < 0 || minY >= m_height)
        return noRows;
    return DebugLineRows{std::clamp(minY, 0, m_height - 1), std::clamp(maxY, 0, m_height - 1)};
}

void Renderer::prepareDebugLineChunk(std::size_t chunkIndex)
{
    // 큰 메시의 법선 명령 하나도 256개 선 단위로 분배한다. 각 chunk는
    // 고유한 원본 순번 슬롯만 쓰며 명령 MVP와 입력 정점은 읽기 전용이다.
    const auto &chunk = m_debugLineChunks[chunkIndex];
    const auto &command = m_debugLineCommands[chunk.commandIndex];
    const auto &vertices = command.command->vertices;
    auto &counts = m_debugLineRowCounts.Local();
    if (counts.size() != static_cast<std::size_t>(m_height))
        counts.assign(static_cast<std::size_t>(m_height), 0u);
    const auto end = chunk.firstLine + chunk.lineCount;
    for (auto line = chunk.firstLine; line < end; ++line)
    {
        const auto index = command.firstLine + line;
        m_preparedDebugLines[index] = Rasterizer::PrepareDepthTestedLine(
            vertices[line * 2], vertices[line * 2 + 1], command.modelViewProjection, m_width, m_height);
        const auto rows = debugLineRowRange(m_preparedDebugLines[index]);
        m_debugLineRows[index] = rows;
        // 모든 band가 그리는 선은 경계 선택에 영향이 없어 세지 않는다.
        if (rows.first <= rows.last && rows.first >= 0)
            ++counts[static_cast<std::size_t>(rows.first)];
    }
}

void Renderer::renderDepthTestedLines(RenderTarget &target, const RenderQueue &queue,
                                      const FrameContext &frame)
{
    // 준비(PrepareDepthTestedLine)와 행별 선 수 집계는 binCommands의 분류 작업 안에서 끝났다.
    if (m_debugLinePlan == DebugLinePlan::Serial)
    {
        Rasterizer lines(target, frame, m_options.collectStats ? &m_stats : nullptr);
        for (const auto &command : queue.GetDebugCommands())
            if (command.depthPolicy == DebugDepthPolicy::DepthTested)
                lines.DrawDebugPrimitive(command, m_currentLineAlgorithm);
        return;
    }

    if (!m_debugLinesDrawn)
    {
        std::ranges::fill(m_debugLineBandStats, RenderStats{});
        sr::jobs::ParallelFor(0, m_debugLineBands,
                              [&](int first, int last)
                              {
                                  for (int band = first; band != last; ++band)
                                      drawDebugLineBand(target, frame, band);
                              });
    }
    if (m_options.collectStats)
        for (int band = 0; band < m_debugLineBands; ++band)
            m_stats.MergeCounters(m_debugLineBandStats[band]);
}

void Renderer::computeDebugLineBands()
{
    // 고정 64행 band에서는 IronMan 법선이 몰린 한 band가 그리기 시간의 대부분을 차지했다
    // (1080p: 17개 중 한 band 3.27ms, 전체 3.31ms). 행별 선 수의 누적이 같아지도록
    // band 경계를 정한다. 경계와 무관하게 각 행의 픽셀은 한 band만 쓰고, 각 band는
    // 원래 queue/정점 순번으로 자기 행에 걸친 선만 그리므로 결과는 같다.
    m_debugLineRowWeights.assign(static_cast<std::size_t>(m_height), 0u);
    for (const auto &counts : m_debugLineRowCounts)
        if (counts.size() == m_debugLineRowWeights.size())
            for (std::size_t row = 0; row < counts.size(); ++row)
                m_debugLineRowWeights[row] += counts[row];
    std::uint64_t totalWeight = 0;
    for (const auto weight : m_debugLineRowWeights)
        totalWeight += weight;
    const int bandCount = std::min(debugLineBandCount, m_height);
    auto &bandStart = m_debugLineBandStart;
    bandStart = {};
    int bands = 0;
    if (totalWeight == 0)
        for (bands = 1; bands < bandCount; ++bands)
            bandStart[bands] = static_cast<int>(static_cast<std::int64_t>(m_height) * bands / bandCount);
    else
    {
        std::uint64_t accumulated = 0;
        for (int row = 0; row + 1 < m_height && bands + 1 < bandCount; ++row)
        {
            accumulated += m_debugLineRowWeights[static_cast<std::size_t>(row)];
            if (accumulated * static_cast<std::uint64_t>(bandCount) >=
                totalWeight * static_cast<std::uint64_t>(bands + 1))
                bandStart[++bands] = row + 1;
        }
        ++bands;
    }
    // bandCount <= maximumLineBands이므로 실제로는 잘리지 않는다. 정적 분석에 범위를 드러낸다.
    bands = std::clamp(bands, 1, debugLineBandCount);
    bandStart[static_cast<std::size_t>(bands)] = m_height;
    m_debugLineBands = bands;
}

void Renderer::drawDebugLineBand(RenderTarget &target, const FrameContext &frame, int band)
{
    const int minY = m_debugLineBandStart[band], maxY = m_debugLineBandStart[band + 1];
    Rasterizer lines(target, frame, m_options.collectStats ? &m_debugLineBandStats[band] : nullptr);
    lines.BeginLineRows(minY, maxY);
    for (std::size_t index = 0; index < m_debugLineRows.size(); ++index)
    {
        const auto rows = m_debugLineRows[index];
        if (rows.first < maxY && rows.last >= minY)
            lines.DrawPreparedDebugLine(*m_preparedDebugLines[index], m_currentLineAlgorithm);
    }
}

void Renderer::applyPostProcessing(RenderTarget &target)
{
    if (m_currentAAAlgorithm != EAAAlgorithm::FXAA)
        return;
    const auto surface = target.Surface();
    const auto width = static_cast<std::size_t>(surface.width);
    m_fxaaSnapshot.resize(width * surface.height);
    for (int y = 0; y < surface.height; ++y)
        std::copy_n(surface.pixels.data() + static_cast<std::size_t>(y) * surface.stridePixels, width,
                    m_fxaaSnapshot.data() + static_cast<std::size_t>(y) * width);
    sr::fxaa::Apply(ConstPixelSurfaceView{m_fxaaSnapshot, surface.width, surface.height, width}, surface);
}

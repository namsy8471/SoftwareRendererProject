#include "Renderer/Renderer.h"
#include "Renderer/RenderQueue.h"
#include "Renderer/Rasterizer.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <xmmintrin.h>
#include "Utils/Jobs.h"

Renderer::WorkerScratch::WorkerScratch()
{
    // 준비 구조가 커졌으므로 작은 초기 용량으로 시작하고 실제 high-water를 재사용한다.
    triangles.reserve(131072);
    tileReferences.reserve(131072);
    sortedTileReferences.reserve(131072);
    touchedTileIds.reserve(8192);
    vertexCache.resize(vertexCacheSize);
    vertexTags.resize(vertexCacheSize);
}
void Renderer::WorkerScratch::BeginFrame()
{
    // 같은 주소의 정점을 프레임 사이에 수정하거나 자산을 재로드할 수 있다.
    // 주소/행렬이 같아도 이전 프레임의 정점 결과는 절대 재사용하지 않는다.
    transformCacheValid = false;
    hiZTests = 0;
    hiZRejected = 0;
    // resize 전의 메타데이터로 사용한 tile만 초기화한다. 참조 배열을 풀보다
    // 먼저 비우므로 예외로 중단된 프레임도 다음 호출에서 안전하게 재사용한다.
    for (const auto tile : touchedTileIds)
        tileScatter[tile] = {};
    touchedTileIds.clear();
    tileReferences.clear();
    for (auto &bin : wireBins)
        bin.clear();
    triangles.clear();
    tileReferencesSorted = false;
    clipInput.clear();
    clipOutput.clear();
    clippedVertices.clear();
}
void Renderer::WorkerScratch::BeginCommand()
{
    // 항상 새 세대를 만든다. 이전 세대의 칸은 stamp가 달라 모두 무효가 된다.
    transformCacheValid = false;
    if (++commandStamp == 0)
    {
        for (auto &tag : vertexTags)
            tag.stamp = 0;
        commandStamp = 1;
    }
}
bool Renderer::CommandCacheKey::Matches(const MeshRenderCommand &command,
                                        const SRMath::mat4 &vp) const noexcept
{
    return mesh == command.sourceMesh &&
           rasterizeMode == command.rasterizeMode &&
           vertexCount == command.sourceMesh->vertices.size() &&
           vertices == command.sourceMesh->vertices.data() &&
           std::memcmp(&worldTransform, &command.worldTransform, sizeof(SRMath::mat4)) == 0 &&
           std::memcmp(&viewProjection, &vp, sizeof(SRMath::mat4)) == 0;
}

bool Renderer::WorkerScratch::BeginCachedCommand(const MeshRenderCommand &command,
                                                 const SRMath::mat4 &viewProjection)
{
    if (!command.sourceMesh)
    {
        transformCacheValid = false;
        throw std::invalid_argument("Vertex cache requires a source mesh");
    }

    if (transformCacheValid && transformCacheKey.Matches(command, viewProjection))
        return true;

    // Octree의 index 부분집합과 material은 정점 변환에 영향을 주지 않는다.
    // worker/명령 순서를 바꾸지 않고 같은 키에서만 세대와 두 행렬을 유지한다.
    BeginCommand();
    const SRMath::mat4 mvp = viewProjection * command.worldTransform;
    const auto inverseTransposeWorld =
        command.rasterizeMode == ERasterizeMode::Fill
            ? (command.normalMatrix ? *command.normalMatrix
                                    : SRMath::inverse_transpose(command.worldTransform).value_or(SRMath::mat4::identity()))
            : SRMath::mat4::identity();
    modelViewProjection = mvp;
    normalMatrix = inverseTransposeWorld;
    // 저장소 확보와 계산이 모두 성공한 뒤에만 키를 공개한다. 예외가 나면
    // BeginCommand에서 무효화한 상태이므로 다음 호출은 독립 resize부터 복구한다.
    transformCacheKey.mesh = command.sourceMesh;
    transformCacheKey.vertices = command.sourceMesh->vertices.data();
    transformCacheKey.vertexCount = command.sourceMesh->vertices.size();
    transformCacheKey.rasterizeMode = command.rasterizeMode;
    transformCacheKey.worldTransform = command.worldTransform;
    transformCacheKey.viewProjection = viewProjection;
    transformCacheValid = true;
    return false;
}
ShadedVertex Renderer::shadeVertex(const Vertex &vertex, const SRMath::mat4 &world,
                                   const SRMath::mat4 &modelViewProjection, const SRMath::mat4 &normalMatrix)
{
    ShadedVertex result;
    result.posClip = modelViewProjection * SRMath::vec4(vertex.position, 1.0f);
    result.posWorld = SRMath::vec3(world * SRMath::vec4(vertex.position, 1.0f));
    result.normalWorld = SRMath::normalize(SRMath::vec3(normalMatrix * SRMath::vec4(vertex.normal, 0.0f)));
    result.texcoord = vertex.texcoord;
    return result;
}
void Renderer::fillVertex(std::uint32_t index, const MeshRenderCommand &command,
                          const SRMath::mat4 &modelViewProjection, const SRMath::mat4 &normalMatrix,
                          WorkerScratch &scratch, int width, int height)
{
    const auto slot = index & (WorkerScratch::vertexCacheSize - 1);
    auto &cached = scratch.vertexCache[slot];
    auto &vertex = cached.shaded;
    if (command.rasterizeMode == ERasterizeMode::Wireframe)
    {
        // X-ray/숨은 선 모두 clip 좌표와 1/w만 사용한다. 법선 디버그는
        // 별도 명령이므로 world/normal/UV 변환을 여기서 반복하지 않는다.
        // mode도 캐시 키에 포함해 Fill과 섞이지 않으며 미사용 속성은 0이다.
        ShadedVertex wire{};
        wire.posClip = modelViewProjection * SRMath::vec4(command.sourceMesh->vertices[index].position, 1.0f);
        vertex = wire;
    }
    else
        vertex = shadeVertex(command.sourceMesh->vertices[index], command.worldTransform, modelViewProjection,
                             normalMatrix);

    auto &setup = cached.setup;
    const __m128 p = _mm_loadu_ps(&vertex.posClip.x);
    const __m128 diff = _mm_sub_ps(p, p);
    const int finiteMask = _mm_movemask_ps(_mm_cmpeq_ps(diff, diff));
    setup.finiteClip = (finiteMask == 0xF);
    setup.outcode = computeOutcode(vertex.posClip);

    const float oneOverW = 1.0f / vertex.posClip.w;
    const auto ndc = SRMath::vec3(vertex.posClip) * oneOverW;
    setup.ndc = ndc;

    auto &r = setup.raster;
    r.screenPos.x = (ndc.x + 1.f) * 0.5f * static_cast<float>(width);
    r.screenPos.y = (1.f - ndc.y) * 0.5f * static_cast<float>(height);
    r.oneOverW = oneOverW;
    r.normalWorldOverW = vertex.normalWorld * oneOverW;
    r.texcoordOverW = vertex.texcoord * oneOverW;
    r.worldPosOverW = vertex.posWorld * oneOverW;

    scratch.vertexTags[slot] = {scratch.commandStamp, index};
}
void Renderer::prepareFrame()
{
    // 모든 parallel_for가 끝난 프레임 경계에서만 풀과 bin을 비운다.
    m_tileReferenceCount = 0;
    m_activeTileWorkers.clear();
    for (auto &scratch : m_workerScratch)
        scratch.BeginFrame();
}
void Renderer::binCommands(const RenderQueue &queue, const FrameContext &frame)
{
    binCommands(queue.GetRenderCommands(), frame, 0);
}

std::uint64_t Renderer::binCommands(std::span<const MeshRenderCommand> commands, const FrameContext &frame,
                                    std::uint64_t baseTriangleIndex)
{
    // 안정된 제출 순서의 입력 삼각형마다 fan용 16개 ID를 예약한다.
    // clipping/culling 결과나 worker 실행 순서는 ID에 영향을 주지 않는다.
    // UINT64_MAX는 비어 있는 깊이 소유자 표시로 남겨 둔다.
    constexpr auto maximumTriangleCount = std::numeric_limits<std::uint64_t>::max() / 16;
    m_commandTriangleBases.resize(commands.size());
    std::uint64_t triangleCount = baseTriangleIndex;
    for (std::size_t index = 0; index < commands.size(); ++index)
    {
        const auto count = commands[index].indicesToDraw.size() / 3;
        if (count > maximumTriangleCount - triangleCount)
            throw std::overflow_error("Render primitive ID capacity exceeded");
        m_commandTriangleBases[index] = triangleCount;
        triangleCount += static_cast<std::uint64_t>(count);
    }
    const auto commandCount = commands.size();
    const auto lineChunkCount = m_debugLinePlan == DebugLinePlan::Parallel ? m_debugLineChunks.size() : 0;
    // 스레드마다 자기 분류가 끝나면(더 가져갈 명령이 없으면) 그 자리에서 자기 참조를 타일별로 정렬한다.
    // 예전에는 모두 끝날 때까지 기다린 뒤 정렬만 하는 병렬 단계를 한 번 더 열었다(합류 1회 + worker 깨우기).
    sr::jobs::ParallelFor<sr::jobs::Split::Fixed>(
        std::size_t{0}, commandCount + lineChunkCount, std::size_t{1},
        [&](std::size_t first, std::size_t last)
        {
            for (auto index = first; index != last; ++index)
                if (index < commandCount)
                    processCommand(commands[index], frame, m_workerScratch.Local(), m_commandTriangleBases[index]);
                else
                    prepareDebugLineChunk(index - commandCount);
        },
        [&]
        {
            if (auto *scratch = m_workerScratch.Find())
                if (!scratch->tileReferencesSorted)
                    scratch->SortTileReferences();
        });
    collectActiveTileWorkers();
    return triangleCount;
}

void Renderer::collectActiveTileWorkers()
{
    // parallel_for가 모두 끝난 뒤에만 고정한다. 실제 참조가 없는 worker는
    // 제외하며, 뒤의 prefix/scatter 단계는 이 목록만 사용한다.
    m_activeTileWorkers.clear();
    for (auto &scratch : m_workerScratch)
        if (!scratch.tileReferences.empty())
            m_activeTileWorkers.push_back(&scratch);
}
void Renderer::processCommand(const MeshRenderCommand &cmd, const FrameContext &frame, WorkerScratch &scratch,
                              std::uint64_t triangleBase)
{
    if (!cmd.sourceMesh || cmd.indicesToDraw.size() % 3 != 0 || cmd.indicesToDraw.empty())
        return;

    // Coarse Hi-Z Submesh Occlusion Culling:
    // 이전 프레임/전방 유닛 래스터 결과로 구축된 Coarse Hi-Z 버퍼를 검사하여
    // 서브메시의 AABB가 완전히 가려진 경우 정점 변환 및 타일 빈닝을 완전히 생략(Early Out)한다.
    const auto triangleCount = static_cast<std::uint64_t>(cmd.indicesToDraw.size() / 3);
    if (m_options.hiZ && m_hiZBuffer.IsValid() && cmd.rasterizeMode == ERasterizeMode::Fill)
    {
        const AABB &box = cmd.worldBounds.IsValid()
                              ? cmd.worldBounds
                              : (cmd.sourceMesh->localAABB.IsValid()
                                     ? cmd.sourceMesh->localAABB.Transform(cmd.worldTransform)
                                     : AABB{});
        if (box.IsValid())
        {
            if (m_options.collectStats)
                scratch.hiZTests += triangleCount;
            if (m_hiZBuffer.IsAABBOccluded(box, frame.viewProjection))
            {
                if (m_options.collectStats)
                    scratch.hiZRejected += triangleCount;
                return;
            }
        }
    }

    const Mesh *mesh = cmd.sourceMesh;
    scratch.tileReferencesSorted = false;
    scratch.BeginCachedCommand(cmd, frame.viewProjection);
    if (scratch.tileScatter.size() != m_tileStats.size()) [[unlikely]]
        scratch.tileScatter.resize(m_tileStats.size());
    const auto &mvp = scratch.modelViewProjection;
    const auto &inverseTransposeWorld = scratch.normalMatrix;
    const auto *const indicesData = cmd.indicesToDraw.data();
    const auto indexCount = cmd.indicesToDraw.size();
    std::array<WorkerScratch::CachedVertex, 3> conflict;
    const Vertex *const meshVertices = mesh->vertices.data();
    const std::size_t meshVertexCount = mesh->vertices.size();
    for (std::size_t i = 0; i < indexCount; i += 3)
    {
        // 정점 번호는 메시 전체에 흩어져 있어 miss 때 읽는 메시 정점이 L2 밖에 있기 쉽다.
        // 8삼각형 앞의 정점을 미리 당겨 둔다. 캐시 검사 뒤에만 당기면 그 분기가 더 비쌌다(채움 +2.9%).
        // prefetch는 값에 영향이 없다.
        constexpr std::size_t prefetchAhead = 3 * 8;
        if (i + prefetchAhead < indexCount)
            for (std::size_t corner = 0; corner < 3; ++corner)
            {
                const auto ahead = indicesData[i + prefetchAhead + corner];
                if (ahead < meshVertexCount)
                    _mm_prefetch(reinterpret_cast<const char *>(meshVertices + ahead), _MM_HINT_T0);
            }
        const uint32_t i0 = indicesData[i];
        const uint32_t i1 = indicesData[i + 1];
        const uint32_t i2 = indicesData[i + 2];

        // fillVertex 호출 뒤에는 컴파일러가 메시가 그대로라고 보지 못해 size()(48B 나눗셈)를 매번 다시
        // 계산했다. 이 명령 동안 메시 정점 배열은 바뀌지 않으므로 위에서 읽어 둔 값과 같다.
        if (i0 >= meshVertexCount || i1 >= meshVertexCount || i2 >= meshVertexCount)
            continue;

        // 서로 다른 두 정점이 같은 칸을 쓰면 나중에 채운 정점이 앞의 것을 덮는다. 그때만 복사본으로 처리한다.
        constexpr auto mask = WorkerScratch::vertexCacheSize - 1;
        const WorkerScratch::CachedVertex *c0, *c1, *c2;
        if (((i0 ^ i1) & mask) && ((i0 ^ i2) & mask) && ((i1 ^ i2) & mask)) [[likely]]
        {
            c0 = &cachedVertex(i0, cmd, mvp, inverseTransposeWorld, scratch, m_width, m_height);
            c1 = &cachedVertex(i1, cmd, mvp, inverseTransposeWorld, scratch, m_width, m_height);
            c2 = &cachedVertex(i2, cmd, mvp, inverseTransposeWorld, scratch, m_width, m_height);
        }
        else
        {
            // 같은 정점이 두 번 나오는 퇴화 삼각형도 이 경로로 온다(값은 같다).
            conflict[0] = cachedVertex(i0, cmd, mvp, inverseTransposeWorld, scratch, m_width, m_height);
            conflict[1] = cachedVertex(i1, cmd, mvp, inverseTransposeWorld, scratch, m_width, m_height);
            conflict[2] = cachedVertex(i2, cmd, mvp, inverseTransposeWorld, scratch, m_width, m_height);
            c0 = &conflict[0];
            c1 = &conflict[1];
            c2 = &conflict[2];
        }
        const auto &setup0 = c0->setup;
        const auto &setup1 = c1->setup;
        const auto &setup2 = c2->setup;

        if (!(setup0.finiteClip & setup1.finiteClip & setup2.finiteClip)) [[unlikely]]
            continue;

        const auto orOutcode = setup0.outcode | setup1.outcode | setup2.outcode;
        if (orOutcode == 0) [[likely]]
        {
            const auto &sv0 = c0->shaded;
            const auto &sv1 = c1->shaded;
            const auto &sv2 = c2->shaded;

            if (sv0.posClip.w <= 0.f || sv1.posClip.w <= 0.f || sv2.posClip.w <= 0.f) [[unlikely]]
                continue;

            const float area = (setup1.ndc.x - setup0.ndc.x) * (setup2.ndc.y - setup0.ndc.y) -
                               (setup1.ndc.y - setup0.ndc.y) * (setup2.ndc.x - setup0.ndc.x);
            if (!std::isfinite(area) || area >= 0.f)
                continue;

            const auto primitiveId = (triangleBase + static_cast<std::uint64_t>(i / 3)) * 16;
            if (Rasterizer::PrepareRasterInto(scratch.triangles.Slot(), setup0.raster, setup1.raster,
                                              setup2.raster, m_width, m_height, primitiveId, &cmd))
            {
                binPrepared(cmd, scratch.triangles.Commit(), frame, scratch);
            }
        }
        else
        {
            if ((setup0.outcode & setup1.outcode & setup2.outcode) != 0)
                continue;

            const auto &sv0 = c0->shaded;
            const auto &sv1 = c1->shaded;
            const auto &sv2 = c2->shaded;

            const auto finiteClip = [](const SRMath::vec4 &clip)
            {
                return std::isfinite(clip.x) && std::isfinite(clip.y) && std::isfinite(clip.z) &&
                       std::isfinite(clip.w);
            };

            scratch.clippedVertices.clear();
            clipTriangle(scratch.clippedVertices, sv0, sv1, sv2, scratch.clipInput, scratch.clipOutput);
            if (scratch.clippedVertices.size() < 3)
                continue;

            const std::size_t fanCount = scratch.clippedVertices.size() - 2;
            for (std::size_t fan = 0; fan < fanCount; ++fan)
            {
                const auto &a = scratch.clippedVertices[0];
                const auto &b = scratch.clippedVertices[fan + 1];
                const auto &c = scratch.clippedVertices[fan + 2];

                if (!finiteClip(a.posClip) || !finiteClip(b.posClip) || !finiteClip(c.posClip) ||
                    a.posClip.w <= 0.f || b.posClip.w <= 0.f || c.posClip.w <= 0.f)
                    continue;

                const auto aNdc = SRMath::vec3(a.posClip) / a.posClip.w;
                const auto bNdc = SRMath::vec3(b.posClip) / b.posClip.w;
                const auto cNdc = SRMath::vec3(c.posClip) / c.posClip.w;
                const float area = (bNdc.x - aNdc.x) * (cNdc.y - aNdc.y) - (bNdc.y - aNdc.y) * (cNdc.x - aNdc.x);
                if (!std::isfinite(area) || area >= 0.f)
                    continue;

                const auto primitiveId =
                    (triangleBase + static_cast<std::uint64_t>(i / 3)) * 16 + static_cast<std::uint64_t>(fan);
                binTriangle(cmd, a, b, c, frame, scratch, primitiveId);
            }
        }
    }
}
void Renderer::binTriangle(const MeshRenderCommand &cmd, const ShadedVertex &v0, const ShadedVertex &v1,
                           const ShadedVertex &v2, const FrameContext &frame, WorkerScratch &scratch,
                           std::uint64_t primitiveId)
{
    if (scratch.tileScatter.size() != m_tileStats.size()) [[unlikely]]
        scratch.tileScatter.resize(m_tileStats.size());
    const auto prepared = Rasterizer::Prepare(v0, v1, v2, m_width, m_height, primitiveId, &cmd);
    if (prepared)
        binPrepared(cmd, &scratch.triangles.emplace_back(*prepared), frame, scratch);
}
void Renderer::binPrepared(const MeshRenderCommand &cmd, TriangleRef *finalTri, const FrameContext &frame,
                           WorkerScratch &scratch)
{
    if (cmd.rasterizeMode == ERasterizeMode::Wireframe) [[unlikely]]
    {
        int firstBand = 0, lastBand = m_lineBandCount - 1;
        if (m_width <= 524288 && m_height <= 524288)
        {
            firstBand = std::clamp(finalTri->minY / m_lineBandHeight, 0, lastBand);
            lastBand = std::clamp(finalTri->maxY / m_lineBandHeight, firstBand, lastBand);
        }
        for (int band = firstBand; band <= lastBand; ++band)
            scratch.wireBins[band].push_back(finalTri);

        if (!m_options.hiddenWireframe)
            return;
    }

    const int minPixelX = std::max(0, finalTri->minX);
    const int maxPixelX = std::min(m_width - 1, finalTri->maxX);
    const int minPixelY = std::max(0, finalTri->minY);
    const int maxPixelY = std::min(m_height - 1, finalTri->maxY);
    if (minPixelX > maxPixelX || minPixelY > maxPixelY) [[unlikely]]
        return;

    // Fast path 1: Single tile (조밀한 메시의 60~70% 이상이 단일 16x16 타일에 속함)
    if (((minPixelX ^ maxPixelX) | (minPixelY ^ maxPixelY)) <= 15) [[likely]]
    {
        const auto tile = static_cast<std::size_t>(minPixelY >> 4) * frame.tilesX +
                          static_cast<std::size_t>(minPixelX >> 4);
        auto &state = scratch.tileScatter[tile];
        if (state.count++ == 0) [[unlikely]]
            scratch.touchedTileIds.push_back(tile);
        scratch.tileReferences.emplace_back(tile, finalTri);
        return;
    }

    const int minTileX = minPixelX >> 4;
    const int maxTileX = maxPixelX >> 4;
    const int minTileY = minPixelY >> 4;
    const int maxTileY = maxPixelY >> 4;

    const auto addTileRef = [&](std::size_t tile) SR_FORCEINLINE_LAMBDA
    {
        auto &state = scratch.tileScatter[tile];
        if (state.count++ == 0) [[unlikely]]
            scratch.touchedTileIds.push_back(tile);
        scratch.tileReferences.emplace_back(tile, finalTri);
    };

    const auto baseIdx = static_cast<std::size_t>(minTileY) * frame.tilesX;

    // Fast path 2: 2-tile horizontal span (가로 2개 타일 보수적 보장)
    if (minTileY == maxTileY && minTileX + 1 == maxTileX)
    {
        const auto t0 = baseIdx + static_cast<std::size_t>(minTileX);
        addTileRef(t0);
        addTileRef(t0 + 1);
        return;
    }

    // Fast path 3: 2-tile vertical span (세로 2개 타일 보수적 보장)
    if (minTileX == maxTileX && minTileY + 1 == maxTileY)
    {
        const auto t0 = baseIdx + static_cast<std::size_t>(minTileX);
        addTileRef(t0);
        addTileRef(t0 + frame.tilesX);
        return;
    }

    // Multi-tile triangle: SIMD conservative tile edge rejection
    const float ox0 = finalTri->dy12 >= 0.f ? 15.5f : 0.5f;
    const float oy0 = -finalTri->dx12 >= 0.f ? 15.5f : 0.5f;
    const float ox1 = finalTri->dy20 >= 0.f ? 15.5f : 0.5f;
    const float oy1 = -finalTri->dx20 >= 0.f ? 15.5f : 0.5f;
    const float ox2 = finalTri->dy01 >= 0.f ? 15.5f : 0.5f;
    const float oy2 = -finalTri->dx01 >= 0.f ? 15.5f : 0.5f;

    const float p1x = finalTri->rv1.screenPos.x;
    const float p1y = finalTri->rv1.screenPos.y;
    const float p2x = finalTri->rv2.screenPos.x;
    const float p2y = finalTri->rv2.screenPos.y;
    const float p0x = finalTri->rv0.screenPos.x;
    const float p0y = finalTri->rv0.screenPos.y;

    const float startX = static_cast<float>(minTileX << 4);
    const float startY = static_cast<float>(minTileY << 4);

    const __m128 eps = _mm_set1_ps(-0.001f);
    const __m128 ox = _mm_set_ps(0.f, ox2, ox1, ox0);
    const __m128 oy = _mm_set_ps(0.f, oy2, oy1, oy0);
    const __m128 px = _mm_set_ps(0.f, p0x, p2x, p1x);
    const __m128 py = _mm_set_ps(0.f, p0y, p2y, p1y);
    const __m128 dy = _mm_set_ps(0.f, finalTri->dy01, finalTri->dy20, finalTri->dy12);
    const __m128 dx = _mm_set_ps(0.f, finalTri->dx01, finalTri->dx20, finalTri->dx12);

    const __m128 startX_v = _mm_set1_ps(startX);
    const __m128 startY_v = _mm_set1_ps(startY);

    __m128 c_row = _mm_sub_ps(_mm_mul_ps(dy, _mm_sub_ps(_mm_add_ps(startX_v, ox), px)),
                               _mm_mul_ps(dx, _mm_sub_ps(_mm_add_ps(startY_v, oy), py)));

    const __m128 stepX = _mm_mul_ps(dy, _mm_set1_ps(16.f));
    const __m128 stepY = _mm_mul_ps(_mm_sub_ps(_mm_setzero_ps(), dx), _mm_set1_ps(16.f));

    for (int ty = minTileY; ty <= maxTileY; ++ty)
    {
        const auto rowBase = static_cast<std::size_t>(ty) * frame.tilesX;
        __m128 c = c_row;
        for (int tx = minTileX; tx <= maxTileX; ++tx)
        {
            const int mask = _mm_movemask_ps(_mm_cmplt_ps(c, eps));
            if ((mask & 7) == 0)
            {
                addTileRef(rowBase + static_cast<std::size_t>(tx));
            }
            c = _mm_add_ps(c, stepX);
        }
        c_row = _mm_add_ps(c_row, stepY);
    }
}

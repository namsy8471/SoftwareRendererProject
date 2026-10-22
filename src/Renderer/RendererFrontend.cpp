#include "Renderer/Renderer.h"
#include "Renderer/RenderQueue.h"
#include "Renderer/Rasterizer.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <tbb/parallel_for.h>
#include <xmmintrin.h>
#include <tbb/blocked_range.h>

Renderer::WorkerScratch::WorkerScratch()
{
    // 준비 구조가 커졌으므로 작은 초기 용량으로 시작하고 실제 high-water를 재사용한다.
    triangles.reserve(512);
    vertexCache.resize(vertexCacheSize);
    vertexTags.resize(vertexCacheSize);
}
void Renderer::WorkerScratch::BeginFrame()
{
    // 같은 주소의 정점을 프레임 사이에 수정하거나 자산을 재로드할 수 있다.
    // 주소/행렬이 같아도 이전 프레임의 정점 결과는 절대 재사용하지 않는다.
    transformCacheValid = false;
    // resize 전의 메타데이터로 사용한 tile만 초기화한다. 참조 배열을 풀보다
    // 먼저 비우므로 예외로 중단된 프레임도 다음 호출에서 안전하게 재사용한다.
    for (const auto tile : touchedTileIds)
        tileScatter[tile] = {};
    touchedTileIds.clear();
    tileReferences.clear();
    for (auto &bin : wireBins)
        bin.clear();
    triangles.clear();
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
bool Renderer::WorkerScratch::BeginCachedCommand(const MeshRenderCommand &command,
                                                 const SRMath::mat4 &viewProjection)
{
    if (!command.sourceMesh)
    {
        transformCacheValid = false;
        throw std::invalid_argument("Vertex cache requires a source mesh");
    }
    CommandCacheKey key;
    key.mesh = command.sourceMesh;
    key.vertices = command.sourceMesh->vertices.data();
    key.vertexCount = command.sourceMesh->vertices.size();
    key.rasterizeMode = command.rasterizeMode;
    for (std::size_t column = 0; column < 4; ++column)
        for (std::size_t row = 0; row < 4; ++row)
        {
            const auto index = column * 4 + row;
            key.worldBits[index] = std::bit_cast<std::uint32_t>(command.worldTransform.cols[column][row]);
            key.viewProjectionBits[index] = std::bit_cast<std::uint32_t>(viewProjection.cols[column][row]);
        }
    if (transformCacheValid && transformCacheKey == key)
        return true;

    // Octree의 index 부분집합과 material은 정점 변환에 영향을 주지 않는다.
    // worker/명령 순서를 바꾸지 않고 같은 키에서만 세대와 두 행렬을 유지한다.
    BeginCommand();
    const SRMath::mat4 mvp = viewProjection * command.worldTransform;
    const auto inverseTransposeWorld =
        command.rasterizeMode == ERasterizeMode::Fill
            ? SRMath::inverse_transpose(command.worldTransform).value_or(SRMath::mat4::identity())
            : SRMath::mat4::identity();
    modelViewProjection = mvp;
    normalMatrix = inverseTransposeWorld;
    // 저장소 확보와 계산이 모두 성공한 뒤에만 키를 공개한다. 예외가 나면
    // BeginCommand에서 무효화한 상태이므로 다음 호출은 독립 resize부터 복구한다.
    transformCacheKey = key;
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
    auto &vertex = scratch.vertexCache[slot].shaded;
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
    // processCommand와 Rasterizer::Prepare가 꼭짓점마다 하던 계산을 같은 식 그대로 옮겼다.
    auto &setup = scratch.vertexCache[slot].setup;
    setup.finiteClip = std::isfinite(vertex.posClip.x) && std::isfinite(vertex.posClip.y) &&
                       std::isfinite(vertex.posClip.z) && std::isfinite(vertex.posClip.w);
    setup.outcode = computeOutcode(vertex.posClip);
    setup.ndc = SRMath::vec3(vertex.posClip) / vertex.posClip.w;
    setup.raster = Rasterizer::ToRasterizerVertex(vertex, width, height);
    scratch.vertexTags[slot] = {scratch.commandStamp, index};
}
void Renderer::prepareFrame()
{
    // 모든 parallel_for가 끝난 프레임 경계에서만 풀과 bin을 비운다.
    m_tileReferences.clear();
    std::ranges::fill(m_tileOffsets, 0);
    m_activeTileWorkers.clear();
    for (auto &scratch : m_workerScratch)
        scratch.BeginFrame();
}
void Renderer::binCommands(const RenderQueue &queue, const FrameContext &frame)
{
    const auto commands = queue.GetRenderCommands();
    // 안정된 제출 순서의 입력 삼각형마다 fan용 16개 ID를 예약한다.
    // clipping/culling 결과나 worker 실행 순서는 ID에 영향을 주지 않는다.
    // UINT64_MAX는 비어 있는 깊이 소유자 표시로 남겨 둔다.
    constexpr auto maximumTriangleCount = std::numeric_limits<std::uint64_t>::max() / 16;
    m_commandTriangleBases.resize(commands.size());
    std::uint64_t triangleCount = 0;
    for (std::size_t index = 0; index < commands.size(); ++index)
    {
        const auto count = commands[index].indicesToDraw.size() / 3;
        if (count > maximumTriangleCount - triangleCount)
            throw std::overflow_error("Render primitive ID capacity exceeded");
        m_commandTriangleBases[index] = triangleCount;
        triangleCount += static_cast<std::uint64_t>(count);
    }
    tbb::parallel_for(
        tbb::blocked_range<std::size_t>(0, commands.size()),
        [&](const tbb::blocked_range<std::size_t> &range)
        {
            auto &scratch = m_workerScratch.local();
            for (auto index = range.begin(); index != range.end(); ++index)
                processCommand(commands[index], frame, scratch, m_commandTriangleBases[index]);
        },
        tbb::simple_partitioner());
    collectActiveTileWorkers();
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
    if (!cmd.sourceMesh || cmd.indicesToDraw.size() % 3 != 0)
        return;
    const Mesh *mesh = cmd.sourceMesh;
    scratch.BeginCachedCommand(cmd, frame.viewProjection);
    const auto &mvp = scratch.modelViewProjection;
    const auto &inverseTransposeWorld = scratch.normalMatrix;
    const auto indices = cmd.indicesToDraw;
    std::array<WorkerScratch::CachedVertex, 3> conflict;
    const Vertex *const meshVertices = mesh->vertices.data();
    const std::size_t meshVertexCount = mesh->vertices.size();
    for (size_t i = 0; i < indices.size(); i += 3)
    {
        // 정점 번호는 메시 전체에 흩어져 있어 miss 때 읽는 메시 정점이 L2 밖에 있기 쉽다.
        // 8삼각형 앞의 정점을 미리 당겨 둔다. 캐시 검사 뒤에만 당기면 그 분기가 더 비쌌다(채움 +2.9%).
        // prefetch는 값에 영향이 없다.
        constexpr std::size_t prefetchAhead = 3 * 8;
        if (i + prefetchAhead < indices.size())
            for (std::size_t corner = 0; corner < 3; ++corner)
            {
                const auto ahead = indices[i + prefetchAhead + corner];
                if (ahead < meshVertexCount)
                    _mm_prefetch(reinterpret_cast<const char *>(meshVertices + ahead), _MM_HINT_T0);
            }
        uint32_t i0 = indices[i];
        uint32_t i1 = indices[i + 1];
        uint32_t i2 = indices[i + 2];

        if (i0 >= mesh->vertices.size() || i1 >= mesh->vertices.size() || i2 >= mesh->vertices.size())
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
        const auto &sv0 = c0->shaded;
        const auto &sv1 = c1->shaded;
        const auto &sv2 = c2->shaded;
        const auto &setup0 = c0->setup;
        const auto &setup1 = c1->setup;
        const auto &setup2 = c2->setup;

        const auto finiteClip = [](const SRMath::vec4 &clip)
        {
            return std::isfinite(clip.x) && std::isfinite(clip.y) && std::isfinite(clip.z) &&
                   std::isfinite(clip.w);
        };
        if (!setup0.finiteClip || !setup1.finiteClip || !setup2.finiteClip)
            continue;

        const auto outcode0 = setup0.outcode;
        const auto outcode1 = setup1.outcode;
        const auto outcode2 = setup2.outcode;

        // 같은 clip 평면 밖에 있는 삼각형은 교차 계산 없이 기각할 수 있다.
        if ((outcode0 & outcode1 & outcode2) != 0)
        {
            continue;
        }

        const bool triviallyAccepted = (outcode0 | outcode1 | outcode2) == 0;
        if (!triviallyAccepted)
        {
            // 동차 좌표에서 자른 뒤에만 viewport로 변환한다.
            scratch.clippedVertices.clear();
            clipTriangle(scratch.clippedVertices, sv0, sv1, sv2, scratch.clipInput, scratch.clipOutput);
            if (scratch.clippedVertices.size() < 3)
                continue;
        }

        // 이미 내부인 삼각형은 큰 셰이딩 정점 3개를 clip 버퍼에 복사하지 않는다.
        // 검증된 캐시를 직접 참조하며, 실제 clipping 결과만 기존 fan 순서로 읽는다.
        const std::size_t fanCount = triviallyAccepted ? 1 : scratch.clippedVertices.size() - 2;
        for (std::size_t fan = 0; fan < fanCount; ++fan)
        {
            const auto &a = triviallyAccepted ? sv0 : scratch.clippedVertices[0];
            const auto &b = triviallyAccepted ? sv1 : scratch.clippedVertices[fan + 1];
            const auto &c = triviallyAccepted ? sv2 : scratch.clippedVertices[fan + 2];
            // near 평면을 가로지르는 원본 정점에는 음수/0 w가 있을 수 있다.
            // 동차 clipping 뒤의 양수 w fan에서만 원근 나눗셈과 뒷면 판정을 한다.
            if ((!triviallyAccepted &&
                 (!finiteClip(a.posClip) || !finiteClip(b.posClip) || !finiteClip(c.posClip))) ||
                a.posClip.w <= 0.f || b.posClip.w <= 0.f || c.posClip.w <= 0.f)
                continue;
            const auto aNdc = triviallyAccepted ? setup0.ndc : SRMath::vec3(a.posClip) / a.posClip.w;
            const auto bNdc = triviallyAccepted ? setup1.ndc : SRMath::vec3(b.posClip) / b.posClip.w;
            const auto cNdc = triviallyAccepted ? setup2.ndc : SRMath::vec3(c.posClip) / c.posClip.w;
            const float area = (bNdc.x - aNdc.x) * (cNdc.y - aNdc.y) - (bNdc.y - aNdc.y) * (cNdc.x - aNdc.x);
            // 기존 좌표계에서 앞면은 NDC의 CW(음수 면적)다.
            if (!std::isfinite(area) || area >= 0.f)
                continue;
            const auto primitiveId =
                (triangleBase + static_cast<std::uint64_t>(i / 3)) * 16 + static_cast<std::uint64_t>(fan);
            if (triviallyAccepted)
            {
                // 세 정점은 위에서 유한하고 w > 0임을 확인했다(Prepare의 첫 검사와 같다).
                // 풀의 다음 자리에 바로 준비하고, 성공했을 때만 확정한다(복사 없음).
                if (Rasterizer::PrepareRasterInto(scratch.triangles.Slot(), setup0.raster, setup1.raster,
                                                  setup2.raster, m_width, m_height, primitiveId, &cmd))
                    binPrepared(cmd, scratch.triangles.Commit(), frame, scratch);
            }
            else
                binTriangle(cmd, a, b, c, frame, scratch, primitiveId);
        }
    }
}
void Renderer::binTriangle(const MeshRenderCommand &cmd, const ShadedVertex &v0, const ShadedVertex &v1,
                           const ShadedVertex &v2, const FrameContext &frame, WorkerScratch &scratch,
                           std::uint64_t primitiveId)
{
    const auto prepared = Rasterizer::Prepare(v0, v1, v2, m_width, m_height, primitiveId, &cmd);
    if (prepared)
        binPrepared(cmd, &scratch.triangles.emplace_back(*prepared), frame, scratch);
}
void Renderer::binPrepared(const MeshRenderCommand &cmd, TriangleRef *finalTri, const FrameContext &frame,
                           WorkerScratch &scratch)
{
    // 숨은 선 모드에서는 같은 준비 결과를 depth-only 면 패스에도 전달한다.
    // finalTri는 worker 풀에 확정된 자리라 프레임 끝까지 주소가 유효하다.
    if (cmd.rasterizeMode == ERasterizeMode::Wireframe)
    {
        // 행 worker가 매번 전체 삼각형 풀을 훑지 않도록 겹치는 band만 기록한다.
        // ensureViewport에서 band 수/높이를 확정하며 이 벡터는 현재 worker만 쓴다.
        int firstBand = 0, lastBand = m_lineBandCount - 1;
        if (m_width <= 524288 && m_height <= 524288)
        {
            firstBand = std::clamp(finalTri->minY / m_lineBandHeight, 0, lastBand);
            lastBand = std::clamp(finalTri->maxY / m_lineBandHeight, firstBand, lastBand);
        }
        // 과대한 viewport에서는 DDA의 긴 누적 오차를 bbox로 단정하지 않고
        // 모든 band에 보수적으로 등록한다. 실제 쓰기 범위는 선 패스가 제한한다.
        for (int band = firstBand; band <= lastBand; ++band)
            scratch.wireBins[band].push_back(finalTri);
    }

    // X-ray 와이어는 행별 선 패스만 실행한다. 풀은 유지하되 타일 분배와
    // 면 깊이 계산은 생략한다. 혼합 장면의 Fill 명령은 정상적으로 bin에 남는다.
    if (cmd.rasterizeMode == ERasterizeMode::Wireframe && !m_options.hiddenWireframe)
        return;

    // 픽셀 AABB를 다시 투영하지 않고 래스터라이저와 같은 inclusive 경계를 쓴다.
    const int minPixelX = std::max(0, finalTri->minX);
    const int maxPixelX = std::min(m_width - 1, finalTri->maxX);
    const int minPixelY = std::max(0, finalTri->minY);
    const int maxPixelY = std::min(m_height - 1, finalTri->maxY);
    if (minPixelX > maxPixelX || minPixelY > maxPixelY)
        return;
    const int minTileX = minPixelX / tileSize;
    const int maxTileX = maxPixelX / tileSize;
    const int minTileY = minPixelY / tileSize;
    const int maxTileY = maxPixelY / tileSize;

    // 이전 프레임의 touched count는 BeginFrame에서 비워졌다. viewport 크기가
    // 바뀐 worker만 메타데이터 크기를 맞추고 단일 참조 배열의 용량은 재사용한다.
    // X-ray 조기 반환보다 뒤에 있어 순수 wire 프레임에는 새 메타데이터도 만들지 않는다.
    if (scratch.tileScatter.size() != m_tileStats.size())
        scratch.tileScatter.resize(m_tileStats.size());

    for (int ty = minTileY; ty <= maxTileY; ++ty)
    {
        const auto baseIdx = static_cast<std::size_t>(ty) * frame.tilesX;
        for (int tx = minTileX; tx <= maxTileX; ++tx)
        {
            const auto tile = baseIdx + static_cast<std::size_t>(tx);
            auto &state = scratch.tileScatter[tile];
            if (state.count == std::numeric_limits<std::size_t>::max())
                throw std::length_error("Worker tile reference count exceeded");
            // touched 등록 후 참조 삽입이 성공한 경우에만 count를 늘린다.
            // 어느 할당에서 실패해도 다음 BeginFrame이 남은 상태를 모두 지운다.
            if (state.count == 0)
                scratch.touchedTileIds.push_back(tile);
            scratch.tileReferences.push_back({tile, finalTri});
            ++state.count;
        }
    }
}

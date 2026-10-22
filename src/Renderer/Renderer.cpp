#include "Renderer/Renderer.h"
#include "Renderer/RenderQueue.h"
#include "Renderer/FXAA.h"
#include "Graphics/Light.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <stdexcept>
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

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
    // 두 할당 성공 후 viewport를 교체한다. offset의 마지막 원소는 전체 참조 수다.
    std::vector<std::size_t> offsets(tileCount + 1);
    std::vector<RenderStats> stats(tileCount);
    m_tileOffsets.swap(offsets);
    m_tileStats.swap(stats);
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

void Renderer::RenderScene(RenderTarget &target, const RenderQueue &queue, const ViewParams &view,
                           std::span<const DirectionalLight> lights)
{
    renderScene(target, queue, view, lights, false);
}

void Renderer::ClearAndRenderScene(RenderTarget &target, const RenderQueue &queue, const ViewParams &view,
                                   std::span<const DirectionalLight> lights)
{
    renderScene(target, queue, view, lights, true);
}

void Renderer::renderScene(RenderTarget &target, const RenderQueue &queue, const ViewParams &view,
                           std::span<const DirectionalLight> lights, bool clearTarget)
{
    m_stats = {};
    if (target.Empty())
        return;
    const auto started = FrameClock::now();
    ensureViewport(target.Width(), target.Height());
    const FrameContext frame{view.projection * view.view, view.position,
                             prepareLights(lights), (m_width + tileSize - 1) / tileSize,
                             (m_height + tileSize - 1) / tileSize};
    prepareFrame();
    if (m_options.collectStats)
        m_stats.prepareMs = elapsedMilliseconds(started);
    auto stage = FrameClock::now();
    binCommands(queue, frame);
    if (m_options.collectStats)
    {
        m_stats.binMs = elapsedMilliseconds(stage);
        for (const auto &scratch : m_workerScratch)
        {
            m_stats.preparedTriangles += scratch.triangles.size();
            m_stats.liveTriangleBytes += scratch.triangles.size() * sizeof(TriangleRef);
            m_stats.retainedTriangleBytes += scratch.triangles.capacity() * sizeof(TriangleRef);
            // 최종 scatter 복사본과 중복 합산하지 않는 논리적 triangle→tile 수다.
            m_stats.binReferences += scratch.tileReferences.size();
        }
    }
    const bool needsDepthPrepass =
        m_options.hiddenWireframe &&
        std::ranges::any_of(queue.GetRenderCommands(), [](const auto &command)
                            { return command.rasterizeMode == ERasterizeMode::Wireframe; });
    stage = FrameClock::now();
    renderTiles(target, frame, needsDepthPrepass, clearTarget);
    if (m_options.collectStats)
    {
        m_stats.rasterMs = elapsedMilliseconds(stage);
        for (const auto &stats : m_tileStats)
            m_stats.MergeCounters(stats);
        for (auto id : target.PrimitiveIds())
            m_stats.finalFilledPixels += id != RenderTarget::emptyOwner;
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

void Renderer::prepareTileReferences()
{
    m_tileReferences.clear();
    if (m_activeTileWorkers.empty())
    {
        // fill→X-ray 또는 빈 프레임 전환에서도 이전 offset/참조가 남지 않는다.
        std::ranges::fill(m_tileOffsets, 0);
        return;
    }

    // 타일 순서의 prefix 안에 worker 순서대로 서로 겹치지 않는 목적 구간을
    // 배정한다. 기존 private-bin 병합과 같은 worker별 삽입 순서를 보존한다.
    std::size_t total = 0;
    for (std::size_t tile = 0; tile < m_tileStats.size(); ++tile)
    {
        m_tileOffsets[tile] = total;
        for (auto *worker : m_activeTileWorkers)
        {
            auto &state = worker->tileScatter[tile];
            if (state.count > m_tileReferences.max_size() - total)
                throw std::length_error("Tile reference prefix capacity exceeded");
            state.cursor = total;
            total += state.count;
        }
    }
    m_tileOffsets.back() = total;
    // vector 용량을 프레임 간 재사용한다. 실패하면 scatter/래스터는 시작하지 않으며
    // 다음 prepareFrame이 private count/참조를 초기화한다.
    m_tileReferences.resize(total);

    // 각 작업은 한 worker의 cursor와 사전 배정된 목적 슬롯만 쓴다. 모든 count는
    // frontend push 성공 후 증가했으므로 매 참조가 정확히 한 슬롯을 채운다.
    tbb::parallel_for(tbb::blocked_range<std::size_t>(0, m_activeTileWorkers.size(), 1),
                      [&](const tbb::blocked_range<std::size_t> &range)
                      {
                          for (auto index = range.begin(); index != range.end(); ++index)
                          {
                              auto &worker = *m_activeTileWorkers[index];
                              for (const auto &reference : worker.tileReferences)
                                  m_tileReferences[worker.tileScatter[reference.tileIndex].cursor++] =
                                      reference.triangle;
                          }
                      });
}

void Renderer::renderTiles(RenderTarget &target, const FrameContext &frame, bool needsDepthPrepass,
                           bool clearTiles)
{
    // scatter join 뒤에만 각 타일의 span을 정렬/래스터한다.
    prepareTileReferences();
    if (m_tileReferences.empty())
    {
        // 빈 X-ray 프레임에는 타일 작업을 만들지 않되 이전 통계는 지운다.
        std::ranges::fill(m_tileStats, RenderStats{});
        if (clearTiles)
            target.Clear();
        return;
    }
    // auto_partitioner는 1080p 8160타일을 스레드 수에 맞춰 크게 나눈 뒤 훔칠 때만 쪼갠다. IronMan이 걸친
    // 중앙 타일은 다른 타일의 수십 배라 큰 조각 하나가 끝을 늦췄다(PC 24스레드 타일 단계 사용률 76%).
    // 4타일씩 고정해 처음부터 잘게 나눈다. 타일은 서로 독립이라 결과는 같다.
    constexpr std::size_t tilesPerTask = 4;
    tbb::parallel_for(
        tbb::blocked_range<std::size_t>(0, m_tileStats.size(), tilesPerTask),
        [&](const tbb::blocked_range<std::size_t> &range)
        {
            for (auto index = range.begin(); index != range.end(); ++index)
            {
                auto bin =
                    std::span<TriangleRef *>(m_tileReferences)
                        .subspan(m_tileOffsets[index], m_tileOffsets[index + 1] - m_tileOffsets[index]);
                auto &stats = m_tileStats[index];
                stats = {};
                const int x = static_cast<int>(index % frame.tilesX) * tileSize;
                const int y = static_cast<int>(index / frame.tilesX) * tileSize;
                // 타일은 화면을 겹침 없이 덮는다. 래스터 직전에 지워 값이 캐시에 남은 채로 쓴다.
                if (clearTiles)
                    target.ClearRect(x, y, std::min(x + tileSize, m_width), std::min(y + tileSize, m_height));
                if (bin.empty())
                    continue;
                if (m_options.frontToBack)
                    std::sort(bin.begin(), bin.end(),
                              [](const auto *a, const auto *b)
                              {
                                  if (a->maximumInverseW != b->maximumInverseW)
                                      return a->maximumInverseW > b->maximumInverseW;
                                  return a->primitiveId < b->primitiveId;
                              });
                Rasterizer rasterizer(target, frame, m_options.collectStats ? &stats : nullptr);
                rasterizer.BeginTile(
                    {x, y, std::min(x + tileSize, m_width), std::min(y + tileSize, m_height)}, m_options.hiZ);
                if (needsDepthPrepass)
                {
                    // 혼합 모드도 모든 면의 깊이를 먼저 완성해야 뒤쪽 채움 색이 남지 않는다.
                    for (const auto *triangle : bin)
                        rasterizer.DrawTriangle(*triangle, RasterPass::DepthOnly);
                    for (const auto *triangle : bin)
                        if (triangle->sourceCommand->rasterizeMode == ERasterizeMode::Fill)
                            rasterizer.DrawTriangle(*triangle, RasterPass::ShadeVisible);
                }
                else
                    for (const auto *triangle : bin)
                        if (triangle->sourceCommand->rasterizeMode == ERasterizeMode::Fill)
                            rasterizer.DrawTriangle(*triangle, RasterPass::Shaded);
            }
        },
        tbb::simple_partitioner());
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
        tbb::parallel_for(tbb::blocked_range<int>(0, m_lineBandCount, 1),
                          [&](const tbb::blocked_range<int> &range)
                          {
                              for (int band = range.begin(); band != range.end(); ++band)
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
    std::uint64_t bytes = sizeof(m_tileReferences) + m_tileReferences.capacity() * sizeof(TriangleRef *) +
                          sizeof(m_tileOffsets) + m_tileOffsets.capacity() * sizeof(std::size_t) +
                          sizeof(m_activeTileWorkers) +
                          m_activeTileWorkers.capacity() * sizeof(WorkerScratch *);
    for (const auto &scratch : m_workerScratch)
    {
        // 쉬고 있는 과거 worker의 보유 용량도 실제 메모리이므로 빠뜨리지 않는다.
        bytes += sizeof(scratch.tileReferences) + scratch.tileReferences.capacity() * sizeof(TileReference) +
                 sizeof(scratch.tileScatter) + scratch.tileScatter.capacity() * sizeof(TileScatterState) +
                 sizeof(scratch.touchedTileIds) + scratch.touchedTileIds.capacity() * sizeof(std::size_t) +
                 sizeof(scratch.wireBins);
        for (const auto &bin : scratch.wireBins)
            bytes += bin.capacity() * sizeof(TriangleRef *);
    }
    bytes += sizeof(m_debugLineRows) + m_debugLineRows.capacity() * sizeof(DebugLineRows) +
             m_debugLineRowWeights.capacity() * sizeof(std::uint64_t);
    for (const auto &counts : m_debugLineRowCounts)
        bytes += counts.capacity() * sizeof(std::uint32_t);
    m_stats.retainedBinBytes = bytes;
}

void Renderer::renderDepthTestedLines(RenderTarget &target, const RenderQueue &queue,
                                      const FrameContext &frame)
{
    // 명령 포인터와 bin은 프레임마다 비운다. 준비 슬롯은 큰 프레임에서
    // resize한 현재 범위 전체를 병렬 덮어쓰므로 중복된 전체 초기화는 하지 않는다.
    m_debugLineCommands.clear();
    m_debugLineChunks.clear();


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
        Rasterizer lines(target, frame, m_options.collectStats ? &m_stats : nullptr);
        for (const auto &command : commands)
            if (command.depthPolicy == DebugDepthPolicy::DepthTested)
                lines.DrawDebugPrimitive(command, m_currentLineAlgorithm);
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

    // 선이 걸친 행 구간. 화면 밖/기각 선은 first > last인 빈 구간이다.
    // 과대한 viewport/긴 DDA는 누적 오차를 bbox로 단정하지 않고 모든
    // band에 보낸다. 실제 쓰기는 원래 시작점으로 진행하며 행 범위로 제한한다.
    constexpr DebugLineRows noRows{std::numeric_limits<int>::max(), std::numeric_limits<int>::min()};
    constexpr DebugLineRows allRows{std::numeric_limits<int>::min(), std::numeric_limits<int>::max()};
    const auto rowRange = [&](const std::optional<Rasterizer::PreparedDebugLine> &prepared)
    {
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
    };
    for (auto &counts : m_debugLineRowCounts)
        counts.assign(static_cast<std::size_t>(m_height), 0u);

    // 큰 메시의 법선 명령 하나도 256개 선 단위로 분배한다. 각 chunk는
    // 고유한 원본 순번 슬롯만 쓰며 명령 MVP와 입력 정점은 읽기 전용이다.
    tbb::parallel_for(tbb::blocked_range<std::size_t>(0, m_debugLineChunks.size(), 1),
                      [&](const tbb::blocked_range<std::size_t> &range)
                      {
                          for (auto chunkIndex = range.begin(); chunkIndex != range.end(); ++chunkIndex)
                          {
                              const auto &chunk = m_debugLineChunks[chunkIndex];
                              const auto &command = m_debugLineCommands[chunk.commandIndex];
                              const auto &vertices = command.command->vertices;
                              auto &counts = m_debugLineRowCounts.local();
                              if (counts.size() != static_cast<std::size_t>(m_height))
                                  counts.assign(static_cast<std::size_t>(m_height), 0u);
                              const auto end = chunk.firstLine + chunk.lineCount;
                              for (auto line = chunk.firstLine; line < end; ++line)
                              {
                                  const auto index = command.firstLine + line;
                                  m_preparedDebugLines[index] = Rasterizer::PrepareDepthTestedLine(
                                      vertices[line * 2], vertices[line * 2 + 1], command.modelViewProjection,
                                      m_width, m_height);
                                  const auto rows = rowRange(m_preparedDebugLines[index]);
                                  m_debugLineRows[index] = rows;
                                  // 모든 band가 그리는 선은 경계 선택에 영향이 없어 세지 않는다.
                                  if (rows.first <= rows.last && rows.first >= 0)
                                      ++counts[static_cast<std::size_t>(rows.first)];
                              }
                          }
                      });

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
    std::array<int, debugLineBandCount + 1> bandStart{};
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

    std::array<RenderStats, maximumLineBands> bandStats{};
    tbb::parallel_for(
        tbb::blocked_range<int>(0, bands, 1),
        [&](const tbb::blocked_range<int> &range)
        {
            for (int band = range.begin(); band != range.end(); ++band)
            {
                const int minY = bandStart[band], maxY = bandStart[band + 1];
                Rasterizer lines(target, frame, m_options.collectStats ? &bandStats[band] : nullptr);
                lines.BeginLineRows(minY, maxY);
                for (std::size_t index = 0; index < m_debugLineRows.size(); ++index)
                {
                    const auto rows = m_debugLineRows[index];
                    if (rows.first < maxY && rows.last >= minY)
                        lines.DrawPreparedDebugLine(*m_preparedDebugLines[index], m_currentLineAlgorithm);
                }
            }
        });
    if (m_options.collectStats)
        for (int band = 0; band < bands; ++band)
            m_stats.MergeCounters(bandStats[band]);
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

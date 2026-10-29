#pragma once
#include <vector>
#include <array>
#include <atomic>
#include <bit>
#include <cstdint>
#include <span>
#include <optional>
#include <xmmintrin.h>
#include "Math/SRMath.h"
#include "Graphics/Mesh.h"
#include "Graphics/ViewParams.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/Tile.h"
#include "Renderer/RenderTypes.h"
#include "Renderer/RenderTarget.h"
#include "Renderer/Rasterizer.h"
#include "Renderer/ShadowMap.h"
#include "Graphics/IBL.h" 
#include "Utils/FixedCapacityVector.h"
#include "Utils/Jobs.h"
#include "Platform/Compiler.h"

class RenderQueue;
struct DirectionalLight;
struct PointLight;
struct SpotLight;
struct MeshRenderCommand;
struct DebugPrimitiveCommand;
enum class ERasterizeMode;

// 프레임 순서와 병렬 작업만 소유한다. 창과 대상 버퍼의 수명은 호출자가 관리한다.
class Renderer
{
  private:
    using ClipBuffer = sr::FixedCapacityVector<ShadedVertex, 12>;
    using FrameContext = RenderFrameContext;
    static constexpr int maximumLineBands = 64;
    // 법선 선 band 수. 경계를 선 밀도로 정하므로 더 늘리면 band마다 전체 선을 훑는 비용만 는다.
    static constexpr int debugLineBandCount = 32;
    int m_width = 0, m_height = 0;
    int m_lineBandCount = 0, m_lineBandHeight = 0;
    ELineAlgorithm m_currentLineAlgorithm = ELineAlgorithm::Bresenham;
    EAAAlgorithm m_currentAAAlgorithm = EAAAlgorithm::None;
    RenderOptions m_options;
    RenderStats m_stats;
    struct TileReference
    {
        std::size_t tileIndex = 0;
        TriangleRef *triangle = nullptr;
    };
    struct TileScatterState
    {
        std::size_t count = 0;
        std::size_t cursor = 0;
    };
    // 이번 프레임 모든 worker의 타일 참조 수. 0이면 타일 작업을 만들지 않는다.
    std::size_t m_tileReferenceCount = 0;
    // 타일 작업이 활성 worker들의 구간을 이어 붙이는 스레드별 작업 공간. capacity만 재사용한다.
    sr::jobs::PerWorker<std::vector<TriangleRef *>> m_tileGather;
    sr::jobs::PerWorker<TileBuffer> m_tileBuffers;
    std::vector<RenderStats> m_tileStats;
    // 타일 작업 한 조각이 맡는 연속 타일 수(행 순서).
    static constexpr std::size_t tilesPerTask = 4;
    // 묶음별 지난 프레임 래스터 시간(steady_clock 틱)과 그 누적 합(크기 묶음 수 + 1, 스레드 구간 경계용).
    std::vector<std::uint64_t> m_tileChunkCost;
    std::vector<std::uint64_t> m_tileChunkCostPrefix;
    std::vector<std::uint64_t> m_commandTriangleBases;
    std::vector<unsigned int> m_fxaaSnapshot;
    // 프레임마다 값을 갱신하고 capacity만 재사용한다. worker는 이 저장소를 읽기만 한다.
    std::vector<PreparedDirectionalLight> m_preparedLights;
    std::vector<PreparedPointLight> m_preparedPointLights;
    std::vector<PreparedSpotLight> m_preparedSpotLights;
    std::unique_ptr<ShadowMap> m_shadowMap;
    std::shared_ptr<sr::IBLMap> m_defaultIblMap;
    std::shared_ptr<sr::IBLMap> m_customIblMap;

    static constexpr std::size_t debugLineChunkSize = 256;
    struct DebugLineCommand
    {
        const DebugPrimitiveCommand *command;
        SRMath::mat4 modelViewProjection;
        std::size_t firstLine, lineCount;
    };
    struct DebugLineChunk
    {
        std::size_t commandIndex, firstLine, lineCount;
    };
    // 입력 line 순번이 슬롯 번호다. 각 band가 이 순서로 자기 band 선을 그려
    // 색이 다른 법선/디버그 선이 겹쳐도 기존 제출 순서를 유지한다.
    // 선이 걸친 화면 행의 포함 구간. 그리지 않을 선은 first > last다.
    struct DebugLineRows
    {
        int first, last;
    };
    std::vector<DebugLineCommand> m_debugLineCommands;
    std::vector<DebugLineChunk> m_debugLineChunks;
    std::vector<std::optional<Rasterizer::PreparedDebugLine>> m_preparedDebugLines;
    std::vector<DebugLineRows> m_debugLineRows;
    // 법선 선은 화면 중앙 몇 행에 몰린다. 행별 선 수로 band 경계를 정해 band 작업량을 맞춘다.
    sr::jobs::PerWorker<std::vector<std::uint32_t>> m_debugLineRowCounts;
    std::vector<std::uint64_t> m_debugLineRowWeights;
    // 깊이 검사 선을 이번 프레임에 어떻게 그리는지. Parallel이면 준비 chunk가 분류 작업에서 이미 돌았다.
    enum class DebugLinePlan
    {
        Serial,
        Parallel
    };
    DebugLinePlan m_debugLinePlan = DebugLinePlan::Serial;
    // 선 밀도로 정한 band 경계 [start[b], start[b+1]). 타일 작업 안에서 그렸으면 m_debugLinesDrawn이다.
    std::array<int, debugLineBandCount + 1> m_debugLineBandStart{};
    int m_debugLineBands = 0;
    bool m_debugLinesDrawn = false;
    // Renderer를 스택에 만드는 테스트가 있어 큰 배열은 힙에 둔다(MSVC C6262: 스택 16KB 초과).
    std::vector<RenderStats> m_debugLineBandStats = std::vector<RenderStats>(maximumLineBands);
    // 타일 행마다 아직 끝나지 않은 타일 조각 수. 0이 되면 그 행을 덮는 선 band의 대기 수를 줄인다.
    std::vector<std::atomic<std::uint32_t>> m_tileRowRemaining;

    struct CommandCacheKey
    {
        const Mesh *mesh = nullptr;
        const Vertex *vertices = nullptr;
        std::size_t vertexCount = 0;
        ERasterizeMode rasterizeMode = ERasterizeMode::Fill;
        SRMath::mat4 worldTransform{};
        SRMath::mat4 viewProjection{};

        [[nodiscard]] bool Matches(const MeshRenderCommand &command, const SRMath::mat4 &vp) const noexcept;
    };
    struct WorkerScratch
    {
        TrianglePool triangles;
        // worker만 삽입하고 frontend join 뒤 해당 band 작업들이 읽기만 한다.
        // 삼각형 풀의 주소는 안정적이며 프레임 초기화 때 벡터 용량을 재사용한다.
        std::array<std::vector<TriangleRef *>, maximumLineBands> wireBins;
        // 참조는 worker별 단일 배열에 삽입한다. count/cursor만 tile별로 두며
        // touched 목록으로 초기화한다. 순수 X-ray에는 이 메타데이터도 만들지 않는다.
        std::vector<TileReference> tileReferences;
        std::vector<TileScatterState> tileScatter;
        // tileReferences를 타일별로 모은 사본. 한 타일의 구간은 [cursor - count, cursor)다.
        std::vector<TriangleRef *> sortedTileReferences;
        std::vector<std::size_t> touchedTileIds;
        ClipBuffer clipInput, clipOutput, clippedVertices;
        // 정점 하나는 평균 여섯 삼각형이 공유한다. 정점 셰이딩과 clip 검사, NDC, 화면 정점(1/w, 원근 보정
        // 속성)을 꼭짓점마다가 아니라 정점마다 한 번만 계산해 둔다. 같은 함수에 같은 입력이라 값은 같다.
        struct VertexSetup
        {
            RasterizerVertex raster;
            SRMath::vec3 ndc;
            std::uint8_t outcode = 0;
            bool finiteClip = false;
        };
        struct CachedVertex
        {
            ShadedVertex shaded;
            VertexSetup setup;
        };
        struct VertexTag
        {
            std::uint64_t stamp = 0;
            std::uint32_t index = 0;
        };
        // 메시 크기 배열(IronMan 약 13만 정점 x 176B = 23MB를 worker마다) 대신 정점 번호 하위 비트로 칸을
        // 고르는 direct-mapped 캐시다. 명령 하나는 많아야 768개 정점을 쓰므로 4096칸(약 0.8MB)이면 충돌이
        // 드물고, worker의 L2에 남아 miss 때 DRAM 왕복 없이 쓴다. 칸은 (stamp, index)가 모두 같을 때만
        // 유효하다.
        static constexpr std::size_t vertexCacheSize = 4096;
        static_assert(std::has_single_bit(vertexCacheSize));
        std::vector<CachedVertex> vertexCache;
        std::vector<VertexTag> vertexTags;
        std::uint64_t commandStamp = 0;
        // 한 worker의 현재 프레임에서 마지막으로 처리한 키 하나만 재사용한다.
        // 포인터는 queue와 같은 수명 계약으로 빌리며 프레임 밖으로 재사용하지 않는다.
        CommandCacheKey transformCacheKey;
        bool transformCacheValid = false;
        SRMath::mat4 modelViewProjection = SRMath::mat4::identity();
        SRMath::mat4 normalMatrix = SRMath::mat4::identity();
        WorkerScratch();
        void BeginFrame();
        void BeginCommand();
        [[nodiscard]] bool IsCached(std::uint32_t index) const noexcept
        {
            const auto &tag = vertexTags[index & (vertexCacheSize - 1)];
            return tag.stamp == commandStamp && tag.index == index;
        }
        bool BeginCachedCommand(const MeshRenderCommand &, const SRMath::mat4 &viewProjection);
        void SortTileReferences();
        // sortedTileReferences가 지금의 tileReferences와 맞는지. 참조를 더하면 false가 된다.
        bool tileReferencesSorted = false;
    };
    sr::jobs::PerWorker<WorkerScratch> m_workerScratch;
    // frontend join 뒤 고정한 비소유 목록. scatter는 각 worker의 cursor만
    // 독점 갱신하며, 이 단계에서 ETS나 삼각형 풀을 늘리지 않는다.
    std::vector<WorkerScratch *> m_activeTileWorkers;

    void ensureViewport(int width, int height);
    std::span<const PreparedDirectionalLight> prepareLights(std::span<const DirectionalLight>);
    std::span<const PreparedPointLight> preparePointLights(std::span<const PointLight>);
    std::span<const PreparedSpotLight> prepareSpotLights(std::span<const SpotLight>);
    void prepareFrame();
    void binCommands(const RenderQueue &, const FrameContext &);
    void collectActiveTileWorkers();
    void prepareTileReferences();
    void gatherTileReferences(std::size_t tile, std::vector<TriangleRef *> &bin) const;
    void updateRetainedBinBytes();
    void processCommand(const MeshRenderCommand &, const FrameContext &, WorkerScratch &,
                        std::uint64_t triangleBase);
    void binTriangle(const MeshRenderCommand &, const ShadedVertex &, const ShadedVertex &,
                     const ShadedVertex &, const FrameContext &, WorkerScratch &,
                     std::uint64_t primitiveId = 0);
    SR_FORCEINLINE void binPrepared(const MeshRenderCommand &, TriangleRef *, const FrameContext &, WorkerScratch &);
    static ShadedVertex shadeVertex(const Vertex &, const SRMath::mat4 &, const SRMath::mat4 &,
                                    const SRMath::mat4 &);
    // 정점의 약 80%는 이미 이 명령에서 계산됐다. hit는 tag 비교 하나로 끝내도록 인라인하고,
    // 함수 호출(prologue/epilogue)은 miss(fillVertex)일 때만 한다.
    // 반환한 참조는 같은 칸을 쓰는 다른 정점을 채우기 전까지만 유효하다.
    static SR_FORCEINLINE const WorkerScratch::CachedVertex &cachedVertex(
        std::uint32_t index, const MeshRenderCommand &command, const SRMath::mat4 &modelViewProjection,
        const SRMath::mat4 &normalMatrix, WorkerScratch &scratch, int width, int height)
    {
        const auto slot = index & (WorkerScratch::vertexCacheSize - 1);
        const auto &tag = scratch.vertexTags[slot];
        if (tag.stamp != scratch.commandStamp || tag.index != index) [[unlikely]]
            fillVertex(index, command, modelViewProjection, normalMatrix, scratch, width, height);
        return scratch.vertexCache[slot];
    }
    static void fillVertex(std::uint32_t, const MeshRenderCommand &, const SRMath::mat4 &,
                           const SRMath::mat4 &, WorkerScratch &, int width, int height);
    [[nodiscard]] static SR_FORCEINLINE std::uint8_t computeOutcode(const SRMath::vec4 &posClip) noexcept { const __m128 p = _mm_loadu_ps(&posClip.x); const __m128 w = _mm_shuffle_ps(p, p, _MM_SHUFFLE(3, 3, 3, 3)); const __m128 negW = _mm_sub_ps(_mm_setzero_ps(), w); const int maskLt = _mm_movemask_ps(_mm_cmplt_ps(p, negW)) & 7; const int maskGt = _mm_movemask_ps(_mm_cmpgt_ps(p, w)) & 7; static constexpr std::uint8_t lut[8] = {0, 1, 4, 5, 16, 17, 20, 21}; return static_cast<std::uint8_t>(lut[maskLt] | (lut[maskGt] << 1)); }
    static ShadedVertex interpolate(const ShadedVertex &, const ShadedVertex &, float);
    static void clipPolygonAgainstPlane(ClipBuffer &, const ClipBuffer &, const __m128 &);
    static void clipTriangle(ClipBuffer &, const ShadedVertex &, const ShadedVertex &, const ShadedVertex &,
                             ClipBuffer &, ClipBuffer &);
    // drawLineBands면 깊이 검사 선 band도 같은 작업에서 그린다(band가 덮는 타일 행이 모두 끝난 뒤).
    void renderTiles(RenderTarget &, const FrameContext &, bool needsDepthPrepass, bool clearTiles = false,
                     bool drawLineBands = false);
    void renderLines(RenderTarget &, const RenderQueue &, const FrameContext &);
    void renderDepthTestedLines(RenderTarget &, const RenderQueue &, const FrameContext &);
    void planDepthTestedLines(const RenderQueue &, const FrameContext &);
    void prepareDebugLineChunk(std::size_t chunkIndex);
    DebugLineRows debugLineRowRange(const std::optional<Rasterizer::PreparedDebugLine> &) const noexcept;
    void computeDebugLineBands();
    void drawDebugLineBand(RenderTarget &, const FrameContext &, int band);
    void renderTileRange(RenderTarget &, const FrameContext &, std::size_t first, std::size_t last,
                         bool needsDepthPrepass, bool clearTiles, std::vector<TriangleRef *> &gathered,
                         TileBuffer &tileBuffer);
    void applyPostProcessing(RenderTarget &);
    friend struct RendererTestAccess;

  public:
    Renderer() = default;
    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;
    void SetLineAlgorithm(ELineAlgorithm algorithm) noexcept { m_currentLineAlgorithm = algorithm; }
    void SetAAAlgorithm(EAAAlgorithm algorithm) noexcept { m_currentAAAlgorithm = algorithm; }
    void SetOptions(RenderOptions options) noexcept { m_options = options; }
    void SetIBLMap(std::shared_ptr<sr::IBLMap> iblMap) noexcept { m_customIblMap = std::move(iblMap); }
    [[nodiscard]] const std::shared_ptr<sr::IBLMap> &GetIBLMap() const noexcept
    {
        return m_customIblMap ? m_customIblMap : m_defaultIblMap;
    }
    [[nodiscard]] RenderOptions GetOptions() const noexcept { return m_options; }
    [[nodiscard]] const RenderStats &GetStats() const noexcept { return m_stats; }

    // target.Clear → RenderScene → presenter.Present. 참조는 동기 호출 동안만 빌린다.
    // 같은 Renderer/target을 동시에 호출하지 않는다. 빈 target은 안전한 no-op이다.
    void RenderScene(RenderTarget &target, const RenderQueue &queue, const ViewParams &view,
                     std::span<const DirectionalLight> lights,
                     std::span<const PointLight> pointLights = {},
                     std::span<const SpotLight> spotLights = {});
    // target.Clear()를 따로 부르지 않고 각 타일 작업이 래스터 직전에 자기 타일을 지운다.
    // 결과는 Clear 후 RenderScene과 같다. 전체를 한 번 더 도는 병렬 단계와 그 동기화가 사라진다.
    void ClearAndRenderScene(RenderTarget &target, const RenderQueue &queue, const ViewParams &view,
                             std::span<const DirectionalLight> lights,
                             std::span<const PointLight> pointLights = {},
                             std::span<const SpotLight> spotLights = {});

  private:
    void renderScene(RenderTarget &, const RenderQueue &, const ViewParams &, std::span<const DirectionalLight>,
                     std::span<const PointLight>, std::span<const SpotLight>, bool clearTarget);
};

#pragma once
#include <vector>
#include <array>
#include <cstdint>
#include <span>
#include <optional>
#include <tbb/concurrent_vector.h>
#include <tbb/enumerable_thread_specific.h>
#include "Math/SRMath.h"
#include "Graphics/Mesh.h"
#include "Graphics/ViewParams.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/Tile.h"
#include "Renderer/RenderTypes.h"
#include "Renderer/RenderTarget.h"
#include "Renderer/Rasterizer.h"
#include "Utils/FixedCapacityVector.h"

class RenderQueue;
struct DirectionalLight;
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
        std::size_t tileIndex;
        TriangleRef *triangle;
    };
    struct TileScatterState
    {
        std::size_t count = 0;
        std::size_t cursor = 0;
    };
    // 모든 타일의 참조를 한 저장소에 모은다. offsets의 인접 두 값이 한 타일의
    // 반개구간이며, 타일마다 남는 과거 capacity를 누적하지 않는다.
    std::vector<TriangleRef *> m_tileReferences;
    std::vector<std::size_t> m_tileOffsets;
    std::vector<RenderStats> m_tileStats;
    std::vector<std::uint64_t> m_commandTriangleBases;
    std::vector<unsigned int> m_fxaaSnapshot;
    // 프레임마다 값을 갱신하고 capacity만 재사용한다. worker는 이 저장소를 읽기만 한다.
    std::vector<PreparedDirectionalLight> m_preparedLights;

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
    tbb::enumerable_thread_specific<std::vector<std::uint32_t>> m_debugLineRowCounts;
    std::vector<std::uint64_t> m_debugLineRowWeights;

    struct CommandCacheKey
    {
        const Mesh *mesh = nullptr;
        const Vertex *vertices = nullptr;
        std::size_t vertexCount = 0;
        // 행렬 객체의 padding이 아니라 16개 float 각각의 비트를 비교한다.
        std::array<std::uint32_t, 16> worldBits{}, viewProjectionBits{};
        ERasterizeMode rasterizeMode{};
        bool operator==(const CommandCacheKey &) const = default;
    };
    struct WorkerScratch
    {
        tbb::concurrent_vector<TriangleRef> triangles;
        // worker만 삽입하고 frontend join 뒤 해당 band 작업들이 읽기만 한다.
        // 삼각형 풀의 주소는 안정적이며 프레임 초기화 때 벡터 용량을 재사용한다.
        std::array<std::vector<TriangleRef *>, maximumLineBands> wireBins;
        // 참조는 worker별 단일 배열에 삽입한다. count/cursor만 tile별로 두며
        // touched 목록으로 초기화한다. 순수 X-ray에는 이 메타데이터도 만들지 않는다.
        std::vector<TileReference> tileReferences;
        std::vector<TileScatterState> tileScatter;
        std::vector<std::size_t> touchedTileIds;
        ClipBuffer clipInput, clipOutput, clippedVertices;
        std::vector<ShadedVertex> shadedVertices;
        std::vector<std::uint64_t> vertexStamps;
        std::uint64_t commandStamp = 0;
        // 한 worker의 현재 프레임에서 마지막으로 처리한 키 하나만 재사용한다.
        // 포인터는 queue와 같은 수명 계약으로 빌리며 프레임 밖으로 재사용하지 않는다.
        CommandCacheKey transformCacheKey;
        bool transformCacheValid = false;
        SRMath::mat4 modelViewProjection = SRMath::mat4::identity();
        SRMath::mat4 normalMatrix = SRMath::mat4::identity();
        WorkerScratch();
        void BeginFrame();
        void BeginCommand(std::size_t vertexCount);
        bool BeginCachedCommand(const MeshRenderCommand &, const SRMath::mat4 &viewProjection);
    };
    tbb::enumerable_thread_specific<WorkerScratch> m_workerScratch;
    // frontend join 뒤 고정한 비소유 목록. scatter는 각 worker의 cursor만
    // 독점 갱신하며, 이 단계에서 ETS나 삼각형 풀을 늘리지 않는다.
    std::vector<WorkerScratch *> m_activeTileWorkers;

    void ensureViewport(int width, int height);
    std::span<const PreparedDirectionalLight> prepareLights(std::span<const DirectionalLight>);
    void prepareFrame();
    void binCommands(const RenderQueue &, const FrameContext &);
    void collectActiveTileWorkers();
    void prepareTileReferences();
    void updateRetainedBinBytes();
    void processCommand(const MeshRenderCommand &, const FrameContext &, WorkerScratch &,
                        std::uint64_t triangleBase);
    void binTriangle(const MeshRenderCommand &, const ShadedVertex &, const ShadedVertex &,
                     const ShadedVertex &, const FrameContext &, WorkerScratch &,
                     std::uint64_t primitiveId = 0);
    static ShadedVertex shadeVertex(const Vertex &, const SRMath::mat4 &, const SRMath::mat4 &,
                                    const SRMath::mat4 &);
    static const ShadedVertex &cachedVertex(unsigned int, const MeshRenderCommand &, const SRMath::mat4 &,
                                            const SRMath::mat4 &, WorkerScratch &);
    static std::uint8_t computeOutcode(const SRMath::vec4 &) noexcept;
    static ShadedVertex interpolate(const ShadedVertex &, const ShadedVertex &, float);
    static void clipPolygonAgainstPlane(ClipBuffer &, const ClipBuffer &, const __m128 &);
    static void clipTriangle(ClipBuffer &, const ShadedVertex &, const ShadedVertex &, const ShadedVertex &,
                             ClipBuffer &, ClipBuffer &);
    void renderTiles(RenderTarget &, const FrameContext &, bool needsDepthPrepass);
    void renderLines(RenderTarget &, const RenderQueue &, const FrameContext &);
    void renderDepthTestedLines(RenderTarget &, const RenderQueue &, const FrameContext &);
    void applyPostProcessing(RenderTarget &);
    friend struct RendererTestAccess;

  public:
    Renderer() = default;
    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;
    void SetLineAlgorithm(ELineAlgorithm algorithm) noexcept { m_currentLineAlgorithm = algorithm; }
    void SetAAAlgorithm(EAAAlgorithm algorithm) noexcept { m_currentAAAlgorithm = algorithm; }
    void SetOptions(RenderOptions options) noexcept { m_options = options; }
    [[nodiscard]] RenderOptions GetOptions() const noexcept { return m_options; }
    [[nodiscard]] const RenderStats &GetStats() const noexcept { return m_stats; }

    // target.Clear → RenderScene → presenter.Present. 참조는 동기 호출 동안만 빌린다.
    // 같은 Renderer/target을 동시에 호출하지 않는다. 빈 target은 안전한 no-op이다.
    void RenderScene(RenderTarget &target, const RenderQueue &queue, const ViewParams &view,
                     std::span<const DirectionalLight> lights);
};

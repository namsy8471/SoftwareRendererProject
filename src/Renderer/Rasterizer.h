#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include "Renderer/RenderTarget.h"
#include "Renderer/RenderTypes.h"
#include "Renderer/Tile.h"

struct DebugPrimitiveCommand;
struct DebugVertex;
struct Material;
class Texture;

// 16x16 타일 하나의 색/깊이/ID 작업 버퍼(4KB). L1에 머무르며 worker마다 하나를 재사용한다.
// Rasterizer 안에 두면 스택에 Rasterizer를 두는 함수가 16KB를 넘어 /analyze C6262에 걸린다.
struct TileBuffer
{
    std::array<float, 256> depth;
    std::array<std::uint64_t, 256> ids;
    std::array<unsigned int, 256> color;
};

// 타깃은 빌려 쓰며 한 인스턴스는 한 타일 worker에서만 사용한다.
class Rasterizer
{
    friend struct RendererTestAccess;
    friend struct RasterizerTestAccess;

  public:
    struct SurfaceSample
    {
        SRMath::vec3 normalWorld;
        SRMath::vec2 texcoord;
        SRMath::vec3 positionWorld;
        SRMath::vec3 tangentWorld = {1.f, 0.f, 0.f};
        SRMath::vec3 bitangentWorld = {0.f, 1.f, 0.f};
        float lod = 0.f;
    };

    struct OwnerSample
    {
        float a, b, c, q;
    };

    // 동차 변환/클리핑은 한 번만 수행하고 행 worker가 불변 결과를 공유한다.
    // 색상은 시작 정점의 BGRX 값이며, 깊이는 기존 화면 선의 1/w 보간 규약이다.
    struct PreparedDebugLine
    {
        SRMath::vec2 screenA, screenB;
        float qa = 0.f, qb = 0.f;
        std::uint32_t color = 0;
    };

    Rasterizer(RenderTarget &target, const RenderFrameContext &frame, RenderStats *stats = nullptr)
        : m_target(target), m_frame(frame), m_stats(stats), m_lineMaxY(target.Height()),
          m_ownerScratch(ThreadOwnerScratch())
    {
        useTarget();
    }
    Rasterizer(const Rasterizer &) = delete;
    Rasterizer &operator=(const Rasterizer &) = delete;
    // 타일 버퍼에 남은 값을 타깃에 쓴다.
    ~Rasterizer() { EndTile(); }

    static RasterizerVertex ToRasterizerVertex(const ShadedVertex &, int width, int height);
    static std::optional<PreparedTriangle> Prepare(const ShadedVertex &, const ShadedVertex &,
                                                   const ShadedVertex &, int width, int height,
                                                   std::uint64_t primitiveId,
                                                   const MeshRenderCommand *command);
    // Prepare에서 화면 정점 변환 뒤의 부분. 이미 변환한 정점을 받는다(정점 공유 캐시용).
    // Into는 out에 바로 쓴다(약 370바이트의 0 초기화와 optional 복사를 생략). 성공하면 모든 필드를
    // 쓰고 true를 반환한다. false면 out의 내용은 정해지지 않는다.
    static bool PrepareRasterInto(PreparedTriangle &out, const RasterizerVertex &, const RasterizerVertex &,
                                  const RasterizerVertex &, int width, int height, std::uint64_t primitiveId,
                                  const MeshRenderCommand *command);
    static std::optional<PreparedTriangle> PrepareRaster(const RasterizerVertex &, const RasterizerVertex &,
                                                         const RasterizerVertex &, int width, int height,
                                                         std::uint64_t primitiveId,
                                                         const MeshRenderCommand *command);
    // buffer를 주면 16x16 이하 타일은 색/깊이/ID를 그 타일 버퍼에서 그리고 EndTile에서 타깃에 한 번 쓴다.
    // cleared면 타깃을 읽지 않고 Clear와 같은 값으로 시작한다(타깃의 그 영역은 EndTile이 덮는다).
    // buffer가 없으면 예전처럼 타깃에 바로 그린다(cleared는 무시하므로 호출자가 먼저 지운다).
    // ID 면이 없는 타깃에서 두 번에 나눠 그리는 타일은 앞 패스의 픽셀 주인 ID를 idsIn/idsOut(타일 버퍼와 같은
    // 16칸 행 배치, 256개)으로 넘긴다. idsIn이 있으면 cleared가 아닐 때 ID를 0으로 꾸미지 않고 여기서 읽고,
    // idsOut이 있으면 EndTile이 ID를 여기에 쓴다. 같은 깊이에서 작은 ID가 이기는 규칙이 패스 경계에서도 그대로다.
    void BeginTile(TileBounds bounds, bool hiZ, bool cleared = false, TileBuffer *buffer = nullptr,
                   const std::uint64_t *idsIn = nullptr, std::uint64_t *idsOut = nullptr);
    void EndTile();
    // 타일 버퍼로 그리는 중이면 4x4 블록 16개의 최소 깊이(1/w, 가장 먼 값)를 out[by * 4 + bx]에 쓰고 true다.
    // 빈 픽셀은 emptyDepth(float 최솟값)라 그 블록은 무엇도 가리지 않는다. 타일 밖 블록도 emptyDepth다.
    bool ComputeBlockMinimums(std::array<float, 16> &out) const noexcept;
    // 삼각형의 어떤 픽셀 깊이(보간 1/w)도 넘지 않는 상한. 보간 오차를 바깥쪽으로 넓힌다.
    // subnormal/overflow처럼 오차 가정이 깨지면 false다(그때는 가려졌다고 보지 않는다).
    static bool ConservativeDepthUpper(const PreparedTriangle &, float &upper) noexcept;
    // 선의 픽셀 위상을 바꾸지 않고 담당 행만 그린다. Bresenham은 원래
    // 정수 오차 상태를 복원해 건너뛰며 DDA는 순차 누적을 유지한다. 서로 겹치지 않는
    // [minY, maxY)를 맡긴 worker끼리는 색상 버퍼를 안전하게 공유할 수 있다.
    void BeginLineRows(int minY, int maxY);
    void DrawTriangle(const PreparedTriangle &, RasterPass);
    void DrawTriangleAlphaMask(const PreparedTriangle &, RasterPass pass = RasterPass::DepthOwner);
    void DrawTriangleAlphaBlend(const PreparedTriangle &);
    // DepthOwner 패스로 타일의 모든 삼각형을 그린 뒤 부른다. 기록된 픽셀마다 그 삼각형으로 한 번 셰이딩한다.
    void ShadeOwners();
    void DrawWireTriangle(const PreparedTriangle &, ELineAlgorithm, bool depthTest = true);
    void DrawDebugPrimitive(const DebugPrimitiveCommand &, ELineAlgorithm);
    static std::optional<PreparedDebugLine> PrepareDepthTestedLine(const DebugVertex &, const DebugVertex &,
                                                                   const SRMath::mat4 &modelViewProjection,
                                                                   int width, int height);
    void DrawPreparedDebugLine(const PreparedDebugLine &, ELineAlgorithm);

    static SurfaceSample InterpolateSurface(const RasterizerVertex &, const RasterizerVertex &,
                                            const RasterizerVertex &, float, float, float, float);
    static float ComputeTriangleLod(const PreparedTriangle &) noexcept;
    static void ComputeTriangleTangentFrame(const PreparedTriangle &, SRMath::vec3 &tangentWorld,
                                            SRMath::vec3 &bitangentWorld) noexcept;
    static SRMath::vec3 ShadeSurface(const SurfaceSample &, const Material &, const RenderFrameContext &);
    // 픽셀 4개를 lane 하나씩 맡아 SSE로 한 번에 셰이딩한다. 각 lane은 ShadeSurface와 같은 float 연산을
    // 같은 순서로 하므로 결과가 비트 단위로 같다(dot은 _mm_dp_ps(0x71)의 (x+y)+(z+0) 순서를 따른다).
    static void ShadeSurface4(const SurfaceSample (&)[4], const Material *const (&)[4],
                              const RenderFrameContext &, SRMath::vec3 (&)[4]);
    static void ShadeSurface8(const SurfaceSample (&)[8], const Material *const (&)[8],
                              const RenderFrameContext &, SRMath::vec3 (&)[8]);
    static SRMath::Color4 SampleTexture(const Texture &tex, float u, float v, float lod,
                                        const RenderFrameContext &frame);

  private:
    RenderTarget &m_target;
    const RenderFrameContext &m_frame;
    RenderStats *m_stats;
    int m_lineMinY = 0, m_lineMaxY;
    TileBounds m_bounds{};
    bool m_hiZ = false;
    std::array<float, 16> m_blockMinimum{};
    std::array<bool, 16> m_blockFull{};
    float m_tileMinimum = 0.f;
    bool m_tileFull = false;
    // DepthOwner 패스가 픽셀마다 마지막으로 색을 쓸 삼각형(재질이 있고 깊이 검사를 통과한 것)과
    // 그때 계산한 barycentric a, b, c와 보간 1/w(q)를 남긴다. 16x16 타일 기준이다.
    // ShadeOwners는 같은 값을 다시 계산하지 않고 이것을 읽는다. 유효한 칸은 m_ownerMask 비트로만 판단하므로
    // 배열은 지우지 않는다(타일마다 6 KB 0 초기화를 피하려고 스레드별 저장소를 빌려 쓴다).
    struct OwnerScratch
    {
        std::array<const PreparedTriangle *, 256> triangles;
        std::array<OwnerSample, 256> samples;
    };
    // 호출 스레드의 저장소. 한 스레드에서 Rasterizer의 DepthOwner→ShadeOwners 구간이 겹치지 않아야 한다
    // (타일 worker는 타일 하나를 끝낸 뒤 다음 타일을 시작한다).
    static OwnerScratch &ThreadOwnerScratch() noexcept;
    // ShadeOwners의 4픽셀 묶음. 보간(InterpolateSurface)과 ShadeSurface4를 같은 번역 단위에서 inline한다.
    static void ShadeOwnerQuad(const PreparedTriangle *const (&)[4], const OwnerSample *const (&)[4],
                               const RenderFrameContext &, SRMath::vec3 (&)[4]);
    static void ShadeOwner8(const PreparedTriangle *const (&)[8], const OwnerSample *const (&)[8],
                            const RenderFrameContext &, SRMath::vec3 (&)[8]);
    OwnerScratch &m_ownerScratch;
    std::array<std::uint64_t, 4> m_ownerMask{};
    // 픽셀 (x, y)는 base[(y - m_originY) * stride + (x - m_originX)]다. 타일 버퍼거나 타깃 전체다.
    float *m_depthBase = nullptr;
    std::uint64_t *m_idBase = nullptr;
    unsigned int *m_colorBase = nullptr;
    std::size_t m_depthStride = 0, m_colorStride = 0;
    int m_originX = 0, m_originY = 0;
    bool m_local = false;
    TileBuffer *m_tile = nullptr;
    std::uint64_t *m_idsOut = nullptr;

    // 패스 선택은 삼각형당 한 번만 한다. 픽셀 루프에는 depth-only의 조명 분기를 남기지 않는다.
    template <bool Instrumented, RasterPass Pass> void DrawTriangleImpl(const PreparedTriangle &);
    template <bool Instrumented, RasterPass Pass> void DrawTriangleAlphaMaskImpl(const PreparedTriangle &);
    template <bool Instrumented> void DrawTriangleAlphaBlendImpl(const PreparedTriangle &);
    bool IsOccluded(const PreparedTriangle &);
    void RefreshHiZ();
    void useTarget();
    void DrawScreenLine(SRMath::vec2 a, SRMath::vec2 b, float qa, float qb, std::uint32_t bgrx,
                        ELineAlgorithm, bool depthTest, float slopeX = 0.f, float slopeY = 0.f);
    static bool ClipLine(SRMath::vec4 &a, SRMath::vec4 &b);
};

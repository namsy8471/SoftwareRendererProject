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
    }

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
    void BeginTile(TileBounds bounds, bool hiZ);
    // 선의 픽셀 위상을 바꾸지 않고 담당 행만 그린다. Bresenham은 원래
    // 정수 오차 상태를 복원해 건너뛰며 DDA는 순차 누적을 유지한다. 서로 겹치지 않는
    // [minY, maxY)를 맡긴 worker끼리는 색상 버퍼를 안전하게 공유할 수 있다.
    void BeginLineRows(int minY, int maxY);
    void DrawTriangle(const PreparedTriangle &, RasterPass);
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
    static SRMath::vec3 ShadeSurface(const SurfaceSample &, const Material &, const RenderFrameContext &);
    // 픽셀 4개를 lane 하나씩 맡아 SSE로 한 번에 셰이딩한다. 각 lane은 ShadeSurface와 같은 float 연산을
    // 같은 순서로 하므로 결과가 비트 단위로 같다(dot은 _mm_dp_ps(0x71)의 (x+y)+(z+0) 순서를 따른다).
    static void ShadeSurface4(const SurfaceSample (&)[4], const Material *const (&)[4],
                              const RenderFrameContext &, SRMath::vec3 (&)[4]);

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
    struct OwnerSample
    {
        float a, b, c, q;
    };
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
    OwnerScratch &m_ownerScratch;
    std::array<std::uint64_t, 4> m_ownerMask{};

    // 패스 선택은 삼각형당 한 번만 한다. 픽셀 루프에는 depth-only의 조명 분기를 남기지 않는다.
    template <bool Instrumented, RasterPass Pass> void DrawTriangleImpl(const PreparedTriangle &);
    bool IsOccluded(const PreparedTriangle &);
    void RefreshHiZ();
    void DrawScreenLine(SRMath::vec2 a, SRMath::vec2 b, float qa, float qb, std::uint32_t bgrx,
                        ELineAlgorithm, bool depthTest, float slopeX = 0.f, float slopeY = 0.f);
    static bool ClipLine(SRMath::vec4 &a, SRMath::vec4 &b);
};

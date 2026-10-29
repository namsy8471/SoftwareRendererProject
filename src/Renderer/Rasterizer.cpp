#include "Math/SIMD.h"
#include "Renderer/Rasterizer.h"
#include "Renderer/RenderCommand.h"
#include "Graphics/Material.h"
#include "Graphics/Texture.h"
#include "Platform/Compiler.h"
#include "Renderer/Tile.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <smmintrin.h>
#include <limits>
#include <utility>

namespace
{
constexpr float floatEpsilon = std::numeric_limits<float>::epsilon();
constexpr std::uint64_t noPrimitive = std::numeric_limits<std::uint64_t>::max();

bool finiteClip(const SRMath::vec4 &p)
{
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && std::isfinite(p.w);
}

std::uint32_t packColor(const SRMath::vec3 &c)
{
    // 채널마다 "isfinite(v) ? uint32(clamp(v, 0, 1) * 255) : 0"을 세 채널 한 번에 계산한다.
    // 유한한 v에서 max(min(v, 1), 0)은 std::clamp와 값이 같다(-0만 다르지만 * 255 후 절삭하면 둘 다 0).
    // NaN/inf 채널은 |v| < inf 마스크로 0이 된다. 모든 2^32 float에서 기존 식과 같음을 확인했다.
    const __m128 finite = _mm_cmplt_ps(_mm_andnot_ps(_mm_set1_ps(-0.f), c.m128),
                                       _mm_set1_ps(std::numeric_limits<float>::infinity()));
    const __m128 clamped = _mm_max_ps(_mm_min_ps(c.m128, _mm_set1_ps(1.f)), _mm_setzero_ps());
    const __m128i channels =
        _mm_and_si128(_mm_cvttps_epi32(_mm_mul_ps(clamped, _mm_set1_ps(255.f))), _mm_castps_si128(finite));
    // [r, g, b, w] → [b, g, r, w]로 바꿔 바이트로 줄이면 하위 3바이트가 b | g << 8 | r << 16이다.
    const __m128i bgr = _mm_shuffle_epi32(channels, _MM_SHUFFLE(3, 0, 1, 2));
    const __m128i bytes = _mm_packus_epi16(_mm_packus_epi32(bgr, bgr), _mm_setzero_si128());
    return static_cast<std::uint32_t>(_mm_cvtsi128_si32(bytes)) & 0x00FFFFFFu;
}

// 큰 화면에서도 고정소수점 signed overflow를 만들지 않는다. 2^60 여유는
// 시작 edge와 16픽셀 증분의 합에도 충분하며 기존 int32 안전 범위 결과는 같다.
bool toFixed(float input, std::int64_t &result)
{
    const float scaled = input * 256.f;
    // 2^60은 float와 double 모두 정확히 표현되므로 float 비교 하나가 기존
    // "!isfinite || |double| >= 2^60"과 같다. NaN은 비교가 거짓이라 거부되고 inf는 2^60 이상이다.
    if (!(std::abs(scaled) < 0x1p60f))
        return false;
    result = static_cast<std::int64_t>(scaled);
    return true;
}

struct BresenhamRows
{
    int x, y, lastX, lastY;
    std::int64_t error;
};

// 끝점을 band 경계로 다시 자르면 Bresenham의 tie/오차 위상이 달라진다.
// 대신 원래 선의 major-axis step k에서 minor 이동량 floor((k*minor+major/2)/major)를
// 역산한다. 반환 구간의 첫 좌표와 error는 원래 루프를 k번 실행한 상태와 같다.
// 호출자는 Y bbox와 band가 겹침을 먼저 확인한다. 한 행 선은 나눗셈 없이 통과한다.
BresenhamRows restrictBresenhamRows(int startX, int startY, int endX, int endY, int minY, int maxY)
{
    const int dx = std::abs(endX - startX), dy = std::abs(endY - startY);
    BresenhamRows result{startX, startY, endX, endY, static_cast<std::int64_t>(dx) - dy};
    if (std::min(startY, endY) >= minY && std::max(startY, endY) < maxY)
        return result;

    const int sx = startX < endX ? 1 : -1, sy = startY < endY ? 1 : -1;
    // band가 아주 큰 surface의 끝까지 뻗어도 끝점 범위로 먼저 제한해 차를 구한다.
    const int firstY = sy > 0 ? std::max(minY, startY) - startY : startY - std::min(maxY - 1, startY);
    const int lastY = sy > 0 ? std::min(maxY - 1, endY) - startY : startY - std::max(minY, endY);
    std::int64_t firstStep = firstY, lastStep = lastY;
    if (dx > dy)
    {
        // Y가 minor 축이면 해당 행에 처음 도달하는 major step을 올림 나눗셈으로 구한다.
        // firstY==0 / lastY==dy는 시작/끝점을 포함하며 음수 나눗셈이 필요 없다.
        const auto firstStepAtY = [dx, dy](int y)
        { return (static_cast<std::int64_t>(y) * dx - dx / 2 + dy - 1) / dy; };
        firstStep = firstY == 0 ? 0 : firstStepAtY(firstY);
        lastStep = lastY == dy ? dx : firstStepAtY(lastY + 1) - 1;
    }
    const auto advancesAt = [dx, dy](std::int64_t step)
    {
        return dx >= dy ? std::pair{step, (step * dy + dx / 2) / dx}
                        : std::pair{(step * dx + dy / 2) / dy, step};
    };
    const auto [firstXAdvance, firstYAdvance] = advancesAt(firstStep);
    const auto [lastXAdvance, lastYAdvance] = advancesAt(lastStep);
    result.x += sx * static_cast<int>(firstXAdvance);
    result.y += sy * static_cast<int>(firstYAdvance);
    result.lastX = startX + sx * static_cast<int>(lastXAdvance);
    result.lastY = startY + sy * static_cast<int>(lastYAdvance);
    // 허용 끝점 범위에서도 중간 곱과 2*error는 int32를 넘을 수 있다.
    result.error += -firstXAdvance * dy + firstYAdvance * dx;
    return result;
}

// MSVC /O2에서도 중첩 lambda가 픽셀마다 별도 호출되어 프로파일 병목이 되었다.
// 이름 있는 호출 연산자에 forceinline을 적용하되 원래 수식/분기 순서는 유지한다.
// 뷰와 계수는 값으로 보관하며 빌린 target 저장소는 동기 선분 순회 동안 유효하다.
template <bool DepthTest, bool Sloped, bool VaryingDepth, bool Instrumented> struct LinePixelWriter
{
    PixelSurfaceView surface;
    std::span<float> depth;
    RenderStats *stats;
    // 원래 식의 float→double 승격 결과를 미리 보관한다. 값과 연산 순서는 같다.
    double ax, ay;
    double vx, vy, lengthSquared, deltaQ;
    double qa;
    float slopeX, slopeY;
    std::size_t depthStride;
    std::uint32_t color;
    int minY, maxY;

    SR_FORCEINLINE void operator()(int px, int py) const
    {
        if (static_cast<unsigned int>(px) >= static_cast<unsigned int>(surface.width) || py < minY ||
            py >= maxY)
            return;
        if constexpr (DepthTest && !Instrumented)
        {
            // 선은 깊이/owner를 바꾸지 않는다. 색이 이미 같으면 깊이 판정이
            // 통과해도 기각돼도 결과가 같아 q/bias 계산을 생략할 수 있다.
            // 계측 중에는 기각 횟수까지 보존하도록 원래 판정을 수행한다.
            if (surface.pixels[static_cast<std::size_t>(py) * surface.stridePixels + px] == color)
                return;
        }
        if constexpr (DepthTest)
        {
            double amount = 0.;
            if constexpr (Sloped || VaryingDepth)
                if (lengthSquared > 0.)
                {
                    // std::clamp는 const&를 반환해 MSVC가 픽셀마다 세 값을 스택에 쓰고 주소를
                    // cmov로 골라 다시 읽었다(/FAs). MSVC clamp와 같은 비교 순서의 값 선택이며
                    // NaN은 그대로 통과한다.
                    const double t = ((px + 0.5 - ax) * vx + (py + 0.5 - ay) * vy) / lengthSquared;
                    amount = 1. < t ? 1. : (t < 0. ? 0. : t);
                }
            double q = qa;
            if constexpr (VaryingDepth)
                q = qa + deltaQ * amount;
            double slopeBias = 0.;
            if constexpr (Sloped)
            {
                const double lineX = ax + vx * amount, lineY = ay + vy * amount;
                slopeBias = std::abs(slopeX * (px + 0.5 - lineX)) + std::abs(slopeY * (py + 0.5 - lineY));
            }
            const double bias =
                slopeBias + 8. * floatEpsilon *
                                std::max(std::abs(q), static_cast<double>(std::numeric_limits<float>::min()));
            const auto index = static_cast<std::size_t>(py) * depthStride + px;
            if (!std::isfinite(q) || !(q > 0.) || q + bias < depth[index])
            {
                if constexpr (Instrumented)
                    ++stats->lineDepthRejected;
                return;
            }
        }
        surface.pixels[static_cast<std::size_t>(py) * surface.stridePixels + px] = color;
    }
};
} // namespace

RasterizerVertex Rasterizer::ToRasterizerVertex(const ShadedVertex &v, int width, int height)
{
    const float q = 1.f / v.posClip.w;
    const SRMath::vec3 ndc = SRMath::vec3(v.posClip) * q;
    RasterizerVertex result;
    result.screenPos.x = (ndc.x + 1.f) * 0.5f * width;
    result.screenPos.y = (1.f - ndc.y) * 0.5f * height;
    result.oneOverW = q;
    result.normalWorldOverW = v.normalWorld * q;
    result.texcoordOverW = v.texcoord * q;
    result.worldPosOverW = v.posWorld * q;
    return result;
}

std::optional<PreparedTriangle> Rasterizer::Prepare(const ShadedVertex &a, const ShadedVertex &b,
                                                    const ShadedVertex &c, int width, int height,
                                                    std::uint64_t primitiveId,
                                                    const MeshRenderCommand *command)
{
    if (width <= 0 || height <= 0 || primitiveId == noPrimitive)
        return std::nullopt;
    for (const auto *v : {&a, &b, &c})
        if (!finiteClip(v->posClip) || !(v->posClip.w > 0.f))
            return std::nullopt;
    return PrepareRaster(ToRasterizerVertex(a, width, height), ToRasterizerVertex(b, width, height),
                         ToRasterizerVertex(c, width, height), width, height, primitiveId, command);
}

bool Rasterizer::PrepareRasterInto(PreparedTriangle &t, const RasterizerVertex &a, const RasterizerVertex &b,
                                   const RasterizerVertex &c, int width, int height,
                                   std::uint64_t primitiveId, const MeshRenderCommand *command)
{
    if (width <= 0 || height <= 0 || primitiveId == noPrimitive)
        return false;

    // Fast rejection: validate vertex screen coordinates and positive finite 1/w before copy
    const auto isValidVertex = [](const RasterizerVertex &v) noexcept
    {
        return (std::abs(v.screenPos.x) < 0x1p31f) & (std::abs(v.screenPos.y) < 0x1p31f) &
               (v.oneOverW > 0.f && v.oneOverW < std::numeric_limits<float>::infinity());
    };
    if (!isValidVertex(a) || !isValidVertex(b) || !isValidVertex(c))
        return false;

    t.sourceCommand = command;
    t.primitiveId = primitiveId;
    t.rv0 = a;
    t.rv1 = b;
    t.rv2 = c;

    // Vectorized min/max bounds calculation across 3 vertices using SSE
    const __m128 p0 = a.screenPos.m128;
    const __m128 p1 = b.screenPos.m128;
    const __m128 p2 = c.screenPos.m128;

    const __m128 minXY = _mm_min_ps(_mm_min_ps(p0, p1), p2);
    const __m128 maxXY = _mm_max_ps(_mm_max_ps(p0, p1), p2);

    const __m128i minInt = _mm_max_epi32(_mm_cvttps_epi32(minXY), _mm_setzero_si128());
    const __m128i maxLimit = _mm_set_epi32(0, 0, height - 1, width - 1);
    const __m128i maxInt = _mm_min_epi32(_mm_cvttps_epi32(maxXY), maxLimit);

    // Reject if minX > maxX || minY > maxY
    const __m128i cmpGt = _mm_cmpgt_epi32(minInt, maxInt);
    if ((_mm_movemask_ps(_mm_castsi128_ps(cmpGt)) & 3) != 0)
        return false;

    // Store minX, minY, maxX, maxY in one contiguous 16-byte store
    const __m128i bounds = _mm_unpacklo_epi64(minInt, maxInt);
    _mm_storeu_si128(reinterpret_cast<__m128i *>(&t.minX), bounds);

    // Vectorized edge deltas
    const __m128 p01 = _mm_movelh_ps(p0, p1);
    const __m128 p12 = _mm_movelh_ps(p1, p2);
    const __m128 d01_12 = _mm_sub_ps(p01, p12); // [dx01, dy01, dx12, dy12]
    const __m128 d20 = _mm_sub_ps(p2, p0);      // [dx20, dy20, 0, 0]

    _mm_storeu_ps(&t.dx01, d01_12);
    _mm_store_sd(reinterpret_cast<double *>(&t.dx20), _mm_castps_pd(d20));

    // Streamlined fixed-point edge delta conversions:
    // Vertices verified |coord| < 2^31, so deltas are < 2^32, scaled by 256 is < 2^40,
    // guaranteed to fit into int64 without overflow.
    const __m128 scale256 = _mm_set1_ps(256.0f);
    const __m128 scaled_01_12 = _mm_mul_ps(d01_12, scale256);
    const __m128 scaled_20 = _mm_mul_ps(d20, scale256);

    t.dx01Fixed = _mm_cvttss_si64(scaled_01_12);
    t.dy01Fixed = _mm_cvttss_si64(_mm_shuffle_ps(scaled_01_12, scaled_01_12, _MM_SHUFFLE(1, 1, 1, 1)));
    t.dx12Fixed = _mm_cvttss_si64(_mm_movehl_ps(scaled_01_12, scaled_01_12));
    t.dy12Fixed = _mm_cvttss_si64(_mm_shuffle_ps(scaled_01_12, scaled_01_12, _MM_SHUFFLE(3, 3, 3, 3)));
    t.dx20Fixed = _mm_cvttss_si64(scaled_20);
    t.dy20Fixed = _mm_cvttss_si64(_mm_shuffle_ps(scaled_20, scaled_20, _MM_SHUFFLE(1, 1, 1, 1)));

    const __m128 w0 = _mm_set_ss(a.oneOverW);
    const __m128 w1 = _mm_set_ss(b.oneOverW);
    const __m128 w2 = _mm_set_ss(c.oneOverW);
    t.maximumInverseW = _mm_cvtss_f32(_mm_max_ss(_mm_max_ss(w0, w1), w2));
    return true;
}

std::optional<PreparedTriangle> Rasterizer::PrepareRaster(const RasterizerVertex &a,
                                                          const RasterizerVertex &b,
                                                          const RasterizerVertex &c, int width, int height,
                                                          std::uint64_t primitiveId,
                                                          const MeshRenderCommand *command)
{
    PreparedTriangle t;
    if (!PrepareRasterInto(t, a, b, c, width, height, primitiveId, command))
        return std::nullopt;
    return t;
}

void Rasterizer::useTarget()
{
    const auto surface = m_target.Surface();
    m_depthBase = m_target.Depth().data();
    // ID 면이 없는 타깃이면 nullptr다. 그런 타깃의 삼각형은 타일 버퍼로만 그린다(DrawTriangle).
    m_idBase = m_target.HasPrimitiveIds() ? m_target.PrimitiveIds().data() : nullptr;
    m_colorBase = surface.pixels.data();
    m_depthStride = static_cast<std::size_t>(m_target.Width());
    m_colorStride = surface.stridePixels;
    m_originX = m_originY = 0;
    m_local = false;
}

void Rasterizer::BeginTile(TileBounds bounds, bool hiZ, bool cleared, TileBuffer *buffer)
{
    EndTile();
    bounds.minX = std::clamp(bounds.minX, 0, m_target.Width());
    bounds.minY = std::clamp(bounds.minY, 0, m_target.Height());
    bounds.maxX = std::clamp(bounds.maxX, bounds.minX, m_target.Width());
    bounds.maxY = std::clamp(bounds.maxY, bounds.minY, m_target.Height());
    m_bounds = bounds;
    const int width = bounds.maxX - bounds.minX, height = bounds.maxY - bounds.minY;
    if (buffer && !m_target.Empty() && width > 0 && height > 0 && width <= 16 && height <= 16)
    {
        m_tile = buffer;
        // 타일 버퍼는 L1에 있다. 타깃은 EndTile에서 행마다 한 번만 쓴다.
        if (cleared)
        {
            m_tile->depth.fill(RenderTarget::emptyDepth);
            m_tile->ids.fill(RenderTarget::emptyOwner);
            m_tile->color.fill(0u);
        }
        else
        {
            const auto surface = m_target.Surface();
            const bool ids = m_target.HasPrimitiveIds();
            for (int y = 0; y < height; ++y)
            {
                const auto row = static_cast<std::size_t>(bounds.minY + y);
                const auto first = row * m_target.Width() + bounds.minX;
                std::copy_n(m_target.Depth().data() + first, width, m_tile->depth.data() + y * 16);
                if (ids)
                    std::copy_n(m_target.PrimitiveIds().data() + first, width, m_tile->ids.data() + y * 16);
                else
                    // ID 면이 없으면 이미 그려진 픽셀의 주인은 가장 작은 ID(0)로 본다. 같은 깊이면 먼저 그린
                    // 쪽이 남는다.
                    for (int x = 0; x < width; ++x)
                        m_tile->ids[y * 16 + x] = m_tile->depth[y * 16 + x] == RenderTarget::emptyDepth
                                                      ? RenderTarget::emptyOwner
                                                      : 0;
                std::copy_n(surface.pixels.data() + row * surface.stridePixels + bounds.minX, width,
                            m_tile->color.data() + y * 16);
            }
        }
        m_depthBase = m_tile->depth.data();
        m_idBase = m_tile->ids.data();
        m_colorBase = m_tile->color.data();
        m_depthStride = m_colorStride = 16;
        m_originX = bounds.minX;
        m_originY = bounds.minY;
        m_local = true;
    }
    // 4x4 요약 16개는 최대 16x16 타일만 표현한다.
    m_hiZ = hiZ && !m_target.Empty() && bounds.maxX > bounds.minX && bounds.maxY > bounds.minY &&
            bounds.maxX - bounds.minX <= 16 && bounds.maxY - bounds.minY <= 16;
    m_tileFull = false;
    m_blockFull.fill(false);
    m_ownerMask.fill(0);
    if (m_hiZ)
        RefreshHiZ();
}

void Rasterizer::EndTile()
{
    if (!m_local || !m_tile)
        return;
    const auto surface = m_target.Surface();
    const int width = m_bounds.maxX - m_bounds.minX;
    const bool ids = m_target.HasPrimitiveIds();
    for (int y = 0; y < m_bounds.maxY - m_bounds.minY; ++y)
    {
        const auto row = static_cast<std::size_t>(m_bounds.minY + y);
        const auto first = row * m_target.Width() + m_bounds.minX;
        std::copy_n(m_tile->depth.data() + y * 16, width, m_target.Depth().data() + first);
        if (ids)
            std::copy_n(m_tile->ids.data() + y * 16, width, m_target.PrimitiveIds().data() + first);
        std::copy_n(m_tile->color.data() + y * 16, width,
                    surface.pixels.data() + row * surface.stridePixels + m_bounds.minX);
    }
    useTarget();
}

void Rasterizer::RefreshHiZ()
{
    if (m_tileFull)
        return;
    m_tileFull = true;
    m_tileMinimum = std::numeric_limits<float>::infinity();
    for (int by = 0; by < 4; ++by)
        for (int bx = 0; bx < 4; ++bx)
        {
            const int block = by * 4 + bx;
            bool full = true;
            float minimum = std::numeric_limits<float>::infinity();
            const int x0 = m_bounds.minX + bx * 4;
            const int y0 = m_bounds.minY + by * 4;
            const int x1 = std::min(x0 + 4, m_bounds.maxX);
            const int y1 = std::min(y0 + 4, m_bounds.maxY);
            for (int y = y0; y < y1; ++y)
                for (int x = x0; x < x1; ++x)
                {
                    const auto index =
                        static_cast<std::size_t>(y - m_originY) * m_depthStride + (x - m_originX);
                    const float q = m_depthBase[index];
                    if (m_idBase[index] == noPrimitive || !(q > 0.f) || !std::isfinite(q))
                        full = false;
                    minimum = std::min(minimum, q);
                }
            m_blockFull[block] = full;
            m_blockMinimum[block] = minimum;
            m_tileFull &= full;
            m_tileMinimum = std::min(m_tileMinimum, minimum);
        }
}

bool Rasterizer::IsOccluded(const PreparedTriangle &t)
{
    if (m_stats)
        ++m_stats->hiZTests;
    // 양의 유한 가중치의 합/나눗셈/곱/덧셈 오차를 바깥쪽으로 확장한다.
    // subnormal/overflow는 상대 오차 가정을 만족하지 않아 기각하지 않는다.
    if (!std::isnormal(t.rv0.oneOverW) || !std::isnormal(t.rv1.oneOverW) || !std::isnormal(t.rv2.oneOverW))
        return false;
    const float upper = std::nextafter(t.maximumInverseW * (1.f + 32.f * floatEpsilon),
                                       std::numeric_limits<float>::infinity());
    if (!std::isfinite(upper))
        return false;
    bool hidden = m_tileFull && upper < m_tileMinimum;
    if (!hidden)
    {
        const int x0 = std::max(t.minX, m_bounds.minX);
        const int y0 = std::max(t.minY, m_bounds.minY);
        const int x1 = std::min(t.maxX, m_bounds.maxX - 1);
        const int y1 = std::min(t.maxY, m_bounds.maxY - 1);
        if (x0 > x1 || y0 > y1)
            return false;
        hidden = true;
        for (int by = (y0 - m_bounds.minY) / 4; by <= (y1 - m_bounds.minY) / 4; ++by)
            for (int bx = (x0 - m_bounds.minX) / 4; bx <= (x1 - m_bounds.minX) / 4; ++bx)
            {
                const int block = by * 4 + bx;
                if (!m_blockFull[block] || !(upper < m_blockMinimum[block]))
                    hidden = false;
            }
    }
    if (hidden && m_stats)
        ++m_stats->hiZRejected;
    return hidden;
}

void Rasterizer::DrawTriangleAlphaMask(const PreparedTriangle &t, RasterPass pass)
{
    if (m_target.Empty() || !m_idBase)
        return;
    if (m_hiZ && pass != RasterPass::ShadeVisible && IsOccluded(t))
        return;
    const auto draw = [&]<RasterPass Pass>()
    {
        if (m_stats)
            DrawTriangleAlphaMaskImpl<true, Pass>(t);
        else
            DrawTriangleAlphaMaskImpl<false, Pass>(t);
    };
    switch (pass)
    {
    case RasterPass::Shaded:
        draw.template operator()<RasterPass::Shaded>();
        break;
    case RasterPass::DepthOnly:
        draw.template operator()<RasterPass::DepthOnly>();
        break;
    case RasterPass::ShadeVisible:
        draw.template operator()<RasterPass::ShadeVisible>();
        break;
    case RasterPass::DepthOwner:
        draw.template operator()<RasterPass::DepthOwner>();
        break;
    }
    if (m_hiZ && pass != RasterPass::ShadeVisible)
        RefreshHiZ();
}

void Rasterizer::DrawTriangleAlphaBlend(const PreparedTriangle &t)
{
    if (m_target.Empty() || !m_idBase)
        return;
    if (m_hiZ && IsOccluded(t))
        return;
    if (m_stats)
        DrawTriangleAlphaBlendImpl<true>(t);
    else
        DrawTriangleAlphaBlendImpl<false>(t);
}

template <bool Instrumented, RasterPass Pass>
void Rasterizer::DrawTriangleAlphaMaskImpl(const PreparedTriangle &t)
{
    const int x0 = std::max(t.minX, m_bounds.minX);
    const int y0 = std::max(t.minY, m_bounds.minY);
    const int x1 = std::min(t.maxX, m_bounds.maxX - 1);
    const int y1 = std::min(t.maxY, m_bounds.maxY - 1);
    if (x0 > x1 || y0 > y1)
        return;
    const auto &p0 = t.rv0.screenPos;
    const auto &p1 = t.rv1.screenPos;
    const auto &p2 = t.rv2.screenPos;
    std::int64_t r0, r1, r2;
    if (!toFixed(t.dy12 * (x0 + 0.5f - p1.x) - t.dx12 * (y0 + 0.5f - p1.y), r0) ||
        !toFixed(t.dy20 * (x0 + 0.5f - p2.x) - t.dx20 * (y0 + 0.5f - p2.y), r1) ||
        !toFixed(t.dy01 * (x0 + 0.5f - p0.x) - t.dx01 * (y0 + 0.5f - p0.y), r2))
        return;
    const auto safeEdge = [&](std::int64_t r, std::int64_t dx, std::int64_t dy)
    {
        const double budget = std::abs(static_cast<double>(r)) +
                              std::abs(static_cast<double>(dx)) * (y1 - y0 + 1.) +
                              std::abs(static_cast<double>(dy)) * (x1 - x0 + 1.);
        return budget < 0x1p62;
    };
    const auto magnitude = [](std::int64_t v) { return static_cast<std::uint64_t>(v < 0 ? -v : v); };
    const bool smallEdges = (magnitude(r0) | magnitude(r1) | magnitude(r2) | magnitude(t.dx12Fixed) |
                             magnitude(t.dy12Fixed) | magnitude(t.dx20Fixed) | magnitude(t.dy20Fixed) |
                             magnitude(t.dx01Fixed) | magnitude(t.dy01Fixed)) < (std::uint64_t{1} << 56) &&
                            (x1 - x0) + (y1 - y0) < 60;
    if (!smallEdges && (!safeEdge(r0, t.dx12Fixed, t.dy12Fixed) || !safeEdge(r1, t.dx20Fixed, t.dy20Fixed) ||
                        !safeEdge(r2, t.dx01Fixed, t.dy01Fixed)))
        return;

    float *const depth = m_depthBase;
    std::uint64_t *const ids = m_idBase;
    unsigned int *const colors = m_colorBase;
    const Material *const mat =
        (t.sourceCommand && t.sourceCommand->material) ? t.sourceCommand->material : nullptr;
    const float opacity = mat ? mat->opacity : 1.0f;
    const float cutoff = mat ? mat->alphaCutoff : 0.5f;
    const Texture *const diffTex = (mat && mat->diffuseTexture) ? mat->diffuseTexture.get() : nullptr;
    const float lod = (diffTex && m_frame.mipmapping) ? ComputeTriangleLod(t) : 0.f;

    const auto shadePixel = [&](int x, int y, std::int64_t w0, std::int64_t w1, std::int64_t w2)
                                SR_FORCEINLINE_LAMBDA
    {
        const float f0 = static_cast<float>(w0) / 256.f;
        const float f1 = static_cast<float>(w1) / 256.f;
        const float f2 = static_cast<float>(w2) / 256.f;
        const float total = f0 + f1 + f2;
        if (std::isfinite(total) && total >= 1e-5f)
        {
            if constexpr (Instrumented)
                ++m_stats->coveragePassed;
            const float inverse = 1.f / total;
            const float a = f0 * inverse, b = f1 * inverse, c = f2 * inverse;
            float q = t.rv0.oneOverW * a;
            q += t.rv1.oneOverW * b;
            q += t.rv2.oneOverW * c;
            const auto index = static_cast<std::size_t>(y - m_originY) * m_depthStride + (x - m_originX);
            const bool finite = std::isfinite(q) && q > 0.f;
            const bool visible = Pass == RasterPass::ShadeVisible
                                     ? ids[index] == t.primitiveId && depth[index] == q
                                     : q > depth[index] || (q == depth[index] && t.primitiveId < ids[index]);
            if (finite && visible)
            {
                if constexpr (Pass != RasterPass::ShadeVisible)
                {
                    float alpha = opacity;
                    if (diffTex)
                    {
                        const float invQ = 1.0f / q;
                        const float u = (t.rv0.texcoordOverW.x * a + t.rv1.texcoordOverW.x * b +
                                         t.rv2.texcoordOverW.x * c) *
                                        invQ;
                        const float v = (t.rv0.texcoordOverW.y * a + t.rv1.texcoordOverW.y * b +
                                         t.rv2.texcoordOverW.y * c) *
                                        invQ;
                        const auto texColor = SampleTexture(*diffTex, u, v, lod, m_frame);
                        alpha *= texColor.a;
                    }
                    if (alpha < cutoff)
                    {
                        if constexpr (Instrumented)
                            ++m_stats->depthRejected;
                        return;
                    }
                    depth[index] = q;
                    ids[index] = t.primitiveId;
                }
                if constexpr (Pass == RasterPass::DepthOwner)
                {
                    if (mat)
                    {
                        const auto cell =
                            static_cast<std::size_t>(y - m_bounds.minY) * 16 + (x - m_bounds.minX);
                        m_ownerScratch.triangles[cell] = &t;
                        m_ownerScratch.samples[cell] = {a, b, c, q};
                        m_ownerMask[cell >> 6] |= std::uint64_t{1} << (cell & 63);
                    }
                }
                else if constexpr (Pass != RasterPass::DepthOnly)
                {
                    if (mat)
                    {
                        auto sample = InterpolateSurface(t.rv0, t.rv1, t.rv2, a, b, c, 1.f / q);
                        if (mat->normalTexture)
                            ComputeTriangleTangentFrame(t, sample.tangentWorld, sample.bitangentWorld);
                        if (m_frame.mipmapping)
                            sample.lod = lod;
                        colors[static_cast<std::size_t>(y - m_originY) * m_colorStride + (x - m_originX)] =
                            packColor(ShadeSurface(sample, *mat, m_frame));
                        if constexpr (Instrumented)
                            ++m_stats->shadedPixels;
                    }
                }
            }
            else if constexpr (Instrumented)
                ++m_stats->depthRejected;
        }
    };

    const std::int64_t maxOffset0 = t.dy12Fixed >= 0 ? t.dy12Fixed * 3 : 0;
    const std::int64_t maxOffset1 = t.dy20Fixed >= 0 ? t.dy20Fixed * 3 : 0;
    const std::int64_t maxOffset2 = t.dy01Fixed >= 0 ? t.dy01Fixed * 3 : 0;
    const std::int64_t dy12_4 = t.dy12Fixed * 4;
    const std::int64_t dy20_4 = t.dy20Fixed * 4;
    const std::int64_t dy01_4 = t.dy01Fixed * 4;

    for (int y = y0; y <= y1; ++y)
    {
        auto w0 = r0, w1 = r1, w2 = r2;
        int x = x0;
        while (x + 3 <= x1)
        {
            if (((w0 + maxOffset0) | (w1 + maxOffset1) | (w2 + maxOffset2)) < 0)
            {
                w0 += dy12_4;
                w1 += dy20_4;
                w2 += dy01_4;
                x += 4;
            }
            else
            {
                break;
            }
        }
        while (x <= x1 && (w0 | w1 | w2) < 0)
        {
            w0 += t.dy12Fixed;
            w1 += t.dy20Fixed;
            w2 += t.dy01Fixed;
            ++x;
        }
        for (; x <= x1 && (w0 | w1 | w2) >= 0; ++x)
        {
            shadePixel(x, y, w0, w1, w2);
            w0 += t.dy12Fixed;
            w1 += t.dy20Fixed;
            w2 += t.dy01Fixed;
        }
        r0 -= t.dx12Fixed;
        r1 -= t.dx20Fixed;
        r2 -= t.dx01Fixed;
    }
}

template <bool Instrumented> void Rasterizer::DrawTriangleAlphaBlendImpl(const PreparedTriangle &t)
{
    const int x0 = std::max(t.minX, m_bounds.minX);
    const int y0 = std::max(t.minY, m_bounds.minY);
    const int x1 = std::min(t.maxX, m_bounds.maxX - 1);
    const int y1 = std::min(t.maxY, m_bounds.maxY - 1);
    if (x0 > x1 || y0 > y1)
        return;
    const auto &p0 = t.rv0.screenPos;
    const auto &p1 = t.rv1.screenPos;
    const auto &p2 = t.rv2.screenPos;
    std::int64_t r0, r1, r2;
    if (!toFixed(t.dy12 * (x0 + 0.5f - p1.x) - t.dx12 * (y0 + 0.5f - p1.y), r0) ||
        !toFixed(t.dy20 * (x0 + 0.5f - p2.x) - t.dx20 * (y0 + 0.5f - p2.y), r1) ||
        !toFixed(t.dy01 * (x0 + 0.5f - p0.x) - t.dx01 * (y0 + 0.5f - p0.y), r2))
        return;
    const auto safeEdge = [&](std::int64_t r, std::int64_t dx, std::int64_t dy)
    {
        const double budget = std::abs(static_cast<double>(r)) +
                              std::abs(static_cast<double>(dx)) * (y1 - y0 + 1.) +
                              std::abs(static_cast<double>(dy)) * (x1 - x0 + 1.);
        return budget < 0x1p62;
    };
    const auto magnitude = [](std::int64_t v) { return static_cast<std::uint64_t>(v < 0 ? -v : v); };
    const bool smallEdges = (magnitude(r0) | magnitude(r1) | magnitude(r2) | magnitude(t.dx12Fixed) |
                             magnitude(t.dy12Fixed) | magnitude(t.dx20Fixed) | magnitude(t.dy20Fixed) |
                             magnitude(t.dx01Fixed) | magnitude(t.dy01Fixed)) < (std::uint64_t{1} << 56) &&
                            (x1 - x0) + (y1 - y0) < 60;
    if (!smallEdges && (!safeEdge(r0, t.dx12Fixed, t.dy12Fixed) || !safeEdge(r1, t.dx20Fixed, t.dy20Fixed) ||
                        !safeEdge(r2, t.dx01Fixed, t.dy01Fixed)))
        return;

    const float *const depth = m_depthBase;
    unsigned int *const colors = m_colorBase;
    const Material *const mat =
        (t.sourceCommand && t.sourceCommand->material) ? t.sourceCommand->material : nullptr;
    if (!mat)
        return;

    const float lod = (mat->diffuseTexture && m_frame.mipmapping) ? ComputeTriangleLod(t) : 0.f;
    SRMath::vec3 tangentWorld{1.f, 0.f, 0.f};
    SRMath::vec3 bitangentWorld{0.f, 1.f, 0.f};
    if (mat->normalTexture)
        ComputeTriangleTangentFrame(t, tangentWorld, bitangentWorld);

    const auto shadePixel = [&](int x, int y, std::int64_t w0, std::int64_t w1, std::int64_t w2)
                                SR_FORCEINLINE_LAMBDA
    {
        const float f0 = static_cast<float>(w0) / 256.f;
        const float f1 = static_cast<float>(w1) / 256.f;
        const float f2 = static_cast<float>(w2) / 256.f;
        const float total = f0 + f1 + f2;
        if (std::isfinite(total) && total >= 1e-5f)
        {
            if constexpr (Instrumented)
                ++m_stats->coveragePassed;
            const float inverse = 1.f / total;
            const float a = f0 * inverse, b = f1 * inverse, c = f2 * inverse;
            float q = t.rv0.oneOverW * a;
            q += t.rv1.oneOverW * b;
            q += t.rv2.oneOverW * c;
            const auto index = static_cast<std::size_t>(y - m_originY) * m_depthStride + (x - m_originX);
            const bool finite = std::isfinite(q) && q > 0.f;
            // Depth test: read-only, NO depth write!
            const bool visible = q > depth[index];
            if (finite && visible)
            {
                const float invQ = 1.0f / q;
                SurfaceSample sample = InterpolateSurface(t.rv0, t.rv1, t.rv2, a, b, c, invQ);
                sample.lod = lod;
                sample.tangentWorld = tangentWorld;
                sample.bitangentWorld = bitangentWorld;

                float alpha = mat->opacity;
                if (mat->diffuseTexture)
                {
                    const auto texColor = SampleTexture(*mat->diffuseTexture, sample.texcoord.x,
                                                        sample.texcoord.y, lod, m_frame);
                    alpha *= texColor.a;
                }
                alpha = std::clamp(alpha, 0.0f, 1.0f);
                const int alpha255 = static_cast<int>(std::round(alpha * 255.0f));
                if (alpha255 > 0)
                {
                    const SRMath::vec3 srcColor = ShadeSurface(sample, *mat, m_frame);
                    const std::uint32_t srcPacked = packColor(srcColor);
                    const auto colorIndex =
                        static_cast<std::size_t>(y - m_originY) * m_colorStride + (x - m_originX);
                    const std::uint32_t dstPacked = colors[colorIndex];

                    const int srcB = static_cast<int>((srcPacked >> 0) & 0xFF);
                    const int srcG = static_cast<int>((srcPacked >> 8) & 0xFF);
                    const int srcR = static_cast<int>((srcPacked >> 16) & 0xFF);

                    const int dstB = static_cast<int>((dstPacked >> 0) & 0xFF);
                    const int dstG = static_cast<int>((dstPacked >> 8) & 0xFF);
                    const int dstR = static_cast<int>((dstPacked >> 16) & 0xFF);

                    const int invA = 255 - alpha255;
                    const int outB = (srcB * alpha255 + dstB * invA + 127) / 255;
                    const int outG = (srcG * alpha255 + dstG * invA + 127) / 255;
                    const int outR = (srcR * alpha255 + dstR * invA + 127) / 255;

                    colors[colorIndex] = static_cast<std::uint32_t>(outB | (outG << 8) | (outR << 16));
                    if constexpr (Instrumented)
                        ++m_stats->shadedPixels;
                }
            }
            else if constexpr (Instrumented)
                ++m_stats->depthRejected;
        }
    };

    const std::int64_t maxOffset0 = t.dy12Fixed >= 0 ? t.dy12Fixed * 3 : 0;
    const std::int64_t maxOffset1 = t.dy20Fixed >= 0 ? t.dy20Fixed * 3 : 0;
    const std::int64_t maxOffset2 = t.dy01Fixed >= 0 ? t.dy01Fixed * 3 : 0;
    const std::int64_t dy12_4 = t.dy12Fixed * 4;
    const std::int64_t dy20_4 = t.dy20Fixed * 4;
    const std::int64_t dy01_4 = t.dy01Fixed * 4;

    for (int y = y0; y <= y1; ++y)
    {
        auto w0 = r0, w1 = r1, w2 = r2;
        int x = x0;
        while (x + 3 <= x1)
        {
            if (((w0 + maxOffset0) | (w1 + maxOffset1) | (w2 + maxOffset2)) < 0)
            {
                w0 += dy12_4;
                w1 += dy20_4;
                w2 += dy01_4;
                x += 4;
            }
            else
            {
                break;
            }
        }
        while (x <= x1 && (w0 | w1 | w2) < 0)
        {
            w0 += t.dy12Fixed;
            w1 += t.dy20Fixed;
            w2 += t.dy01Fixed;
            ++x;
        }
        for (; x <= x1 && (w0 | w1 | w2) >= 0; ++x)
        {
            shadePixel(x, y, w0, w1, w2);
            w0 += t.dy12Fixed;
            w1 += t.dy20Fixed;
            w2 += t.dy01Fixed;
        }
        r0 -= t.dx12Fixed;
        r1 -= t.dx20Fixed;
        r2 -= t.dx01Fixed;
    }
}

void Rasterizer::DrawTriangle(const PreparedTriangle &t, RasterPass pass)
{
    if (m_target.Empty() || !m_idBase)
        return;
    if (m_hiZ && pass != RasterPass::ShadeVisible && IsOccluded(t))
        return;
    const auto draw = [&]<RasterPass Pass>()
    {
        if (m_stats)
            DrawTriangleImpl<true, Pass>(t);
        else
            DrawTriangleImpl<false, Pass>(t);
    };
    switch (pass)
    {
    case RasterPass::Shaded:
        draw.template operator()<RasterPass::Shaded>();
        break;
    case RasterPass::DepthOnly:
        draw.template operator()<RasterPass::DepthOnly>();
        break;
    case RasterPass::ShadeVisible:
        draw.template operator()<RasterPass::ShadeVisible>();
        break;
    case RasterPass::DepthOwner:
        draw.template operator()<RasterPass::DepthOwner>();
        break;
    }
    if (m_hiZ && pass != RasterPass::ShadeVisible)
        RefreshHiZ();
}

template <bool Instrumented, RasterPass Pass> void Rasterizer::DrawTriangleImpl(const PreparedTriangle &t)
{
    const int x0 = std::max(t.minX, m_bounds.minX);
    const int y0 = std::max(t.minY, m_bounds.minY);
    const int x1 = std::min(t.maxX, m_bounds.maxX - 1);
    const int y1 = std::min(t.maxY, m_bounds.maxY - 1);
    if (x0 > x1 || y0 > y1)
        return;
    const auto &p0 = t.rv0.screenPos;
    const auto &p1 = t.rv1.screenPos;
    const auto &p2 = t.rv2.screenPos;
    std::int64_t r0, r1, r2;
    // 시작점은 타일마다 기존 float 연산 순서로 계산해 절삭 결과를 보존한다.
    if (!toFixed(t.dy12 * (x0 + 0.5f - p1.x) - t.dx12 * (y0 + 0.5f - p1.y), r0) ||
        !toFixed(t.dy20 * (x0 + 0.5f - p2.x) - t.dx20 * (y0 + 0.5f - p2.y), r1) ||
        !toFixed(t.dy01 * (x0 + 0.5f - p0.x) - t.dx01 * (y0 + 0.5f - p0.y), r2))
        return;
    // 호출자가 넓은 bounds를 넘겨도 int64 누적 범위를 먼저 검사한다.
    const auto safeEdge = [&](std::int64_t r, std::int64_t dx, std::int64_t dy)
    {
        const double budget = std::abs(static_cast<double>(r)) +
                              std::abs(static_cast<double>(dx)) * (y1 - y0 + 1.) +
                              std::abs(static_cast<double>(dy)) * (x1 - x0 + 1.);
        return budget < 0x1p62;
    };
    // 보통은 모든 값이 2^56 미만이고 bbox가 작아 budget이 63 * 2^56 < 2^62로 막힌다.
    // 이때는 double 9번 곱셈/덧셈 없이 정수 OR 한 번으로 같은 판정(통과)을 낸다.
    // toFixed가 |값| < 2^60을 보장하므로 부호 반전은 넘치지 않는다.
    const auto magnitude = [](std::int64_t v) { return static_cast<std::uint64_t>(v < 0 ? -v : v); };
    const bool smallEdges = (magnitude(r0) | magnitude(r1) | magnitude(r2) | magnitude(t.dx12Fixed) |
                             magnitude(t.dy12Fixed) | magnitude(t.dx20Fixed) | magnitude(t.dy20Fixed) |
                             magnitude(t.dx01Fixed) | magnitude(t.dy01Fixed)) < (std::uint64_t{1} << 56) &&
                            (x1 - x0) + (y1 - y0) < 60;
    if (!smallEdges && (!safeEdge(r0, t.dx12Fixed, t.dy12Fixed) || !safeEdge(r1, t.dx20Fixed, t.dy20Fixed) ||
                        !safeEdge(r2, t.dx01Fixed, t.dy01Fixed)))
        return;
    const bool hasMaterial = (t.sourceCommand != nullptr && t.sourceCommand->material != nullptr);
    const int originX = m_originX;
    const std::ptrdiff_t depthStride = static_cast<std::ptrdiff_t>(m_depthStride);
    const std::ptrdiff_t colorStride = static_cast<std::ptrdiff_t>(m_colorStride);
    float *depthRow = m_depthBase + static_cast<std::ptrdiff_t>(y0 - m_originY) * depthStride;
    std::uint64_t *idRow = m_idBase + static_cast<std::ptrdiff_t>(y0 - m_originY) * depthStride;
    [[maybe_unused]] unsigned int *colorRow = nullptr;
    if constexpr (Pass != RasterPass::DepthOnly && Pass != RasterPass::DepthOwner)
    {
        colorRow = m_colorBase + static_cast<std::ptrdiff_t>(y0 - m_originY) * colorStride;
    }
    [[maybe_unused]] std::size_t cellRow = 0;
    if constexpr (Pass == RasterPass::DepthOwner)
    {
        cellRow = static_cast<std::size_t>(y0 - m_bounds.minY) * 16;
    }
    const std::int64_t dy12 = t.dy12Fixed;
    const std::int64_t dy20 = t.dy20Fixed;
    const std::int64_t dy01 = t.dy01Fixed;

    const std::int64_t dy12_3 = dy12 * 3;
    const std::int64_t dy20_3 = dy20 * 3;
    const std::int64_t dy01_3 = dy01 * 3;

    const std::int64_t dy12_4 = dy12 * 4;
    const std::int64_t dy20_4 = dy20 * 4;
    const std::int64_t dy01_4 = dy01 * 4;

    const std::int64_t maxOffset0 = dy12 >= 0 ? dy12_3 : 0;
    const std::int64_t maxOffset1 = dy20 >= 0 ? dy20_3 : 0;
    const std::int64_t maxOffset2 = dy01 >= 0 ? dy01_3 : 0;

    const std::int64_t minOffset0 = dy12 >= 0 ? 0 : dy12_3;
    const std::int64_t minOffset1 = dy20 >= 0 ? 0 : dy20_3;
    const std::int64_t minOffset2 = dy01 >= 0 ? 0 : dy01_3;

    const __m128 v0_w = _mm_set1_ps(t.rv0.oneOverW);
    const __m128 v1_w = _mm_set1_ps(t.rv1.oneOverW);
    const __m128 v2_w = _mm_set1_ps(t.rv2.oneOverW);
    const __m128 inv256 = _mm_set1_ps(1.f / 256.f);
    const __m128 one_f = _mm_set1_ps(1.f);
    const __m128 covEps = _mm_set1_ps(1e-5f);
    const __m128i idVec = _mm_set1_epi64x(static_cast<std::int64_t>(t.primitiveId));
    const __m128i triVec = _mm_set1_epi64x(reinterpret_cast<std::intptr_t>(&t));

    const auto shadePixelScalar = [&](int px, [[maybe_unused]] int py, std::int64_t pw0, std::int64_t pw1,
                                      std::int64_t pw2) SR_FORCEINLINE_LAMBDA
    {
        const int localX = px - originX;
        const float f0 = static_cast<float>(pw0) / 256.f;
        const float f1 = static_cast<float>(pw1) / 256.f;
        const float f2 = static_cast<float>(pw2) / 256.f;
        const float total = f0 + f1 + f2;
        if (std::isfinite(total) && total >= 1e-5f)
        {
            if constexpr (Instrumented)
                ++m_stats->coveragePassed;
            const float inverse = 1.f / total;
            const float a = f0 * inverse, b = f1 * inverse, c = f2 * inverse;
            float q = t.rv0.oneOverW * a;
            q += t.rv1.oneOverW * b;
            q += t.rv2.oneOverW * c;
            const bool finite = std::isfinite(q) && q > 0.f;
            const bool visible =
                Pass == RasterPass::ShadeVisible
                    ? idRow[localX] == t.primitiveId && depthRow[localX] == q
                    : q > depthRow[localX] || (q == depthRow[localX] && t.primitiveId < idRow[localX]);
            if (finite && visible)
            {
                if constexpr (Pass != RasterPass::ShadeVisible)
                {
                    depthRow[localX] = q;
                    idRow[localX] = t.primitiveId;
                }
                if constexpr (Pass == RasterPass::DepthOwner)
                {
                    if (hasMaterial)
                    {
                        const std::size_t cell = cellRow + static_cast<std::size_t>(px - m_bounds.minX);
                        m_ownerScratch.triangles[cell] = &t;
                        m_ownerScratch.samples[cell] = {a, b, c, q};
                        m_ownerMask[cell >> 6] |= std::uint64_t{1} << (cell & 63);
                    }
                }
                else if constexpr (Pass != RasterPass::DepthOnly)
                {
                    if (hasMaterial)
                    {
                        auto sample = InterpolateSurface(t.rv0, t.rv1, t.rv2, a, b, c, 1.f / q);
                        if (t.sourceCommand->material->normalTexture)
                            ComputeTriangleTangentFrame(t, sample.tangentWorld, sample.bitangentWorld);
                        if (m_frame.mipmapping)
                            sample.lod = ComputeTriangleLod(t);
                        colorRow[localX] =
                            packColor(ShadeSurface(sample, *t.sourceCommand->material, m_frame));
                        if constexpr (Instrumented)
                            ++m_stats->shadedPixels;
                    }
                }
            }
            else if constexpr (Instrumented)
                ++m_stats->depthRejected;
        }
    };

    for (int y = y0; y <= y1; ++y)
    {
        auto w0 = r0, w1 = r1, w2 = r2;
        int x = x0;

        // 1. Skip leading blocks of 4 pixels that are guaranteed outside
        while (x + 3 <= x1)
        {
            if (((w0 + maxOffset0) | (w1 + maxOffset1) | (w2 + maxOffset2)) < 0)
            {
                w0 += dy12_4;
                w1 += dy20_4;
                w2 += dy01_4;
                x += 4;
            }
            else
            {
                break;
            }
        }

        // 2. Step scalar pixel-by-pixel until first inside pixel
        while (x <= x1 && (w0 | w1 | w2) < 0)
        {
            w0 += dy12;
            w1 += dy20;
            w2 += dy01;
            ++x;
        }

        // 3. Process blocks of 4 pixels that are fully inside
        while (x + 3 <= x1)
        {
            if (((w0 + minOffset0) | (w1 + minOffset1) | (w2 + minOffset2)) >= 0)
            {
                if constexpr (Pass == RasterPass::DepthOwner)
                {
                    const int localX = x - originX;
                    const auto w0_1 = w0 + dy12;
                    const auto w0_2 = w0 + dy12 * 2;
                    const auto w0_3 = w0 + dy12_3;

                    const auto w1_1 = w1 + dy20;
                    const auto w1_2 = w1 + dy20 * 2;
                    const auto w1_3 = w1 + dy20_3;

                    const auto w2_1 = w2 + dy01;
                    const auto w2_2 = w2 + dy01 * 2;
                    const auto w2_3 = w2 + dy01_3;

                    const __m128 f0 =
                        _mm_mul_ps(_mm_set_ps(static_cast<float>(w0_3), static_cast<float>(w0_2),
                                              static_cast<float>(w0_1), static_cast<float>(w0)),
                                   inv256);
                    const __m128 f1 =
                        _mm_mul_ps(_mm_set_ps(static_cast<float>(w1_3), static_cast<float>(w1_2),
                                              static_cast<float>(w1_1), static_cast<float>(w1)),
                                   inv256);
                    const __m128 f2 =
                        _mm_mul_ps(_mm_set_ps(static_cast<float>(w2_3), static_cast<float>(w2_2),
                                              static_cast<float>(w2_1), static_cast<float>(w2)),
                                   inv256);

                    const __m128 total = _mm_add_ps(_mm_add_ps(f0, f1), f2);
                    const __m128 covMask = _mm_cmpge_ps(total, covEps);
                    if (_mm_movemask_ps(covMask) == 0x0F)
                    {
                        if constexpr (Instrumented)
                            m_stats->coveragePassed += 4;

                        const __m128 invTotal = _mm_div_ps(one_f, total);
                        __m128 a = _mm_mul_ps(f0, invTotal);
                        __m128 b = _mm_mul_ps(f1, invTotal);
                        __m128 c = _mm_mul_ps(f2, invTotal);

                        const __m128 q = _mm_add_ps(_mm_add_ps(_mm_mul_ps(v0_w, a), _mm_mul_ps(v1_w, b)),
                                                    _mm_mul_ps(v2_w, c));
                        const __m128 curDepth = _mm_loadu_ps(&depthRow[localX]);

                        const __m128 cmpGt = _mm_cmpgt_ps(q, curDepth);
                        const int passMask = _mm_movemask_ps(cmpGt);

                        if (passMask == 0x0F)
                        {
                            _mm_storeu_ps(&depthRow[localX], q);
                            _mm_storeu_si128(reinterpret_cast<__m128i *>(&idRow[localX]), idVec);
                            _mm_storeu_si128(reinterpret_cast<__m128i *>(&idRow[localX + 2]), idVec);

                            if (hasMaterial)
                            {
                                const std::size_t cell =
                                    cellRow + static_cast<std::size_t>(x - m_bounds.minX);
                                _mm_storeu_si128(
                                    reinterpret_cast<__m128i *>(&m_ownerScratch.triangles[cell + 0]), triVec);
                                _mm_storeu_si128(
                                    reinterpret_cast<__m128i *>(&m_ownerScratch.triangles[cell + 2]), triVec);

                                __m128 s0 = a, s1 = b, s2 = c, s3 = q;
                                _MM_TRANSPOSE4_PS(s0, s1, s2, s3);
                                _mm_storeu_ps(reinterpret_cast<float *>(&m_ownerScratch.samples[cell + 0]),
                                              s0);
                                _mm_storeu_ps(reinterpret_cast<float *>(&m_ownerScratch.samples[cell + 1]),
                                              s1);
                                _mm_storeu_ps(reinterpret_cast<float *>(&m_ownerScratch.samples[cell + 2]),
                                              s2);
                                _mm_storeu_ps(reinterpret_cast<float *>(&m_ownerScratch.samples[cell + 3]),
                                              s3);

                                if ((cell & 63) <= 60)
                                {
                                    m_ownerMask[cell >> 6] |= std::uint64_t{0x0F} << (cell & 63);
                                }
                                else
                                {
                                    m_ownerMask[(cell + 0) >> 6] |= std::uint64_t{1} << ((cell + 0) & 63);
                                    m_ownerMask[(cell + 1) >> 6] |= std::uint64_t{1} << ((cell + 1) & 63);
                                    m_ownerMask[(cell + 2) >> 6] |= std::uint64_t{1} << ((cell + 2) & 63);
                                    m_ownerMask[(cell + 3) >> 6] |= std::uint64_t{1} << ((cell + 3) & 63);
                                }
                            }
                        }
                        else
                        {
                            alignas(16) float a_arr[4], b_arr[4], c_arr[4], q_arr[4];
                            _mm_store_ps(a_arr, a);
                            _mm_store_ps(b_arr, b);
                            _mm_store_ps(c_arr, c);
                            _mm_store_ps(q_arr, q);

                            for (int i = 0; i < 4; ++i)
                            {
                                const float qi = q_arr[i];
                                const int lx = localX + i;
                                const bool finite = std::isfinite(qi) && qi > 0.f;
                                const bool visible =
                                    finite &&
                                    (qi > depthRow[lx] || (qi == depthRow[lx] && t.primitiveId < idRow[lx]));
                                if (visible)
                                {
                                    depthRow[lx] = qi;
                                    idRow[lx] = t.primitiveId;
                                    if (hasMaterial)
                                    {
                                        const std::size_t cell_i =
                                            cellRow + static_cast<std::size_t>(x + i - m_bounds.minX);
                                        m_ownerScratch.triangles[cell_i] = &t;
                                        m_ownerScratch.samples[cell_i] = {a_arr[i], b_arr[i], c_arr[i], qi};
                                        m_ownerMask[cell_i >> 6] |= std::uint64_t{1} << (cell_i & 63);
                                    }
                                }
                                else if constexpr (Instrumented)
                                {
                                    ++m_stats->depthRejected;
                                }
                            }
                        }
                    }
                    else
                    {
                        shadePixelScalar(x + 0, y, w0, w1, w2);
                        shadePixelScalar(x + 1, y, w0_1, w1_1, w2_1);
                        shadePixelScalar(x + 2, y, w0_2, w1_2, w2_2);
                        shadePixelScalar(x + 3, y, w0_3, w1_3, w2_3);
                    }
                }
                else if constexpr (Pass == RasterPass::DepthOnly)
                {
                    const int localX = x - originX;
                    const auto w0_1 = w0 + dy12;
                    const auto w0_2 = w0 + dy12 * 2;
                    const auto w0_3 = w0 + dy12_3;

                    const auto w1_1 = w1 + dy20;
                    const auto w1_2 = w1 + dy20 * 2;
                    const auto w1_3 = w1 + dy20_3;

                    const auto w2_1 = w2 + dy01;
                    const auto w2_2 = w2 + dy01 * 2;
                    const auto w2_3 = w2 + dy01_3;

                    const __m128 f0 =
                        _mm_mul_ps(_mm_set_ps(static_cast<float>(w0_3), static_cast<float>(w0_2),
                                              static_cast<float>(w0_1), static_cast<float>(w0)),
                                   inv256);
                    const __m128 f1 =
                        _mm_mul_ps(_mm_set_ps(static_cast<float>(w1_3), static_cast<float>(w1_2),
                                              static_cast<float>(w1_1), static_cast<float>(w1)),
                                   inv256);
                    const __m128 f2 =
                        _mm_mul_ps(_mm_set_ps(static_cast<float>(w2_3), static_cast<float>(w2_2),
                                              static_cast<float>(w2_1), static_cast<float>(w2)),
                                   inv256);

                    const __m128 total = _mm_add_ps(_mm_add_ps(f0, f1), f2);
                    const __m128 covMask = _mm_cmpge_ps(total, covEps);
                    if (_mm_movemask_ps(covMask) == 0x0F)
                    {
                        if constexpr (Instrumented)
                            m_stats->coveragePassed += 4;

                        const __m128 invTotal = _mm_div_ps(one_f, total);
                        const __m128 a = _mm_mul_ps(f0, invTotal);
                        const __m128 b = _mm_mul_ps(f1, invTotal);
                        const __m128 c = _mm_mul_ps(f2, invTotal);

                        const __m128 q = _mm_add_ps(_mm_add_ps(_mm_mul_ps(v0_w, a), _mm_mul_ps(v1_w, b)),
                                                    _mm_mul_ps(v2_w, c));
                        const __m128 curDepth = _mm_loadu_ps(&depthRow[localX]);

                        const __m128 cmpGt = _mm_cmpgt_ps(q, curDepth);
                        const int passMask = _mm_movemask_ps(cmpGt);

                        if (passMask == 0x0F)
                        {
                            _mm_storeu_ps(&depthRow[localX], q);
                            _mm_storeu_si128(reinterpret_cast<__m128i *>(&idRow[localX]), idVec);
                            _mm_storeu_si128(reinterpret_cast<__m128i *>(&idRow[localX + 2]), idVec);
                        }
                        else
                        {
                            alignas(16) float q_arr[4];
                            _mm_store_ps(q_arr, q);

                            for (int i = 0; i < 4; ++i)
                            {
                                const float qi = q_arr[i];
                                const int lx = localX + i;
                                const bool finite = std::isfinite(qi) && qi > 0.f;
                                const bool visible =
                                    finite &&
                                    (qi > depthRow[lx] || (qi == depthRow[lx] && t.primitiveId < idRow[lx]));
                                if (visible)
                                {
                                    depthRow[lx] = qi;
                                    idRow[lx] = t.primitiveId;
                                }
                                else if constexpr (Instrumented)
                                {
                                    ++m_stats->depthRejected;
                                }
                            }
                        }
                    }
                    else
                    {
                        shadePixelScalar(x + 0, y, w0, w1, w2);
                        shadePixelScalar(x + 1, y, w0_1, w1_1, w2_1);
                        shadePixelScalar(x + 2, y, w0_2, w1_2, w2_2);
                        shadePixelScalar(x + 3, y, w0_3, w1_3, w2_3);
                    }
                }
                else
                {
                    shadePixelScalar(x + 0, y, w0, w1, w2);
                    shadePixelScalar(x + 1, y, w0 + dy12, w1 + dy20, w2 + dy01);
                    shadePixelScalar(x + 2, y, w0 + dy12 * 2, w1 + dy20 * 2, w2 + dy01 * 2);
                    shadePixelScalar(x + 3, y, w0 + dy12_3, w1 + dy20_3, w2 + dy01_3);
                }

                w0 += dy12_4;
                w1 += dy20_4;
                w2 += dy01_4;
                x += 4;
            }
            else
            {
                break;
            }
        }

        // 4. Scalar tail for remaining inside pixels (0 to 3 pixels)
        for (; x <= x1 && (w0 | w1 | w2) >= 0; ++x)
        {
            shadePixelScalar(x, y, w0, w1, w2);
            w0 += dy12;
            w1 += dy20;
            w2 += dy01;
        }

        r0 -= t.dx12Fixed;
        r1 -= t.dx20Fixed;
        r2 -= t.dx01Fixed;
        depthRow += depthStride;
        idRow += depthStride;
        if constexpr (Pass != RasterPass::DepthOnly && Pass != RasterPass::DepthOwner)
            colorRow += colorStride;
        if constexpr (Pass == RasterPass::DepthOwner)
            cellRow += 16;
    }
}

Rasterizer::OwnerScratch &Rasterizer::ThreadOwnerScratch() noexcept
{
    thread_local OwnerScratch scratch;
    return scratch;
}

void Rasterizer::ShadeOwners()
{
    static_assert(tileSize == 16);
    // a, b, c, q는 DrawTriangleImpl<DepthOwner>가 이 픽셀을 마지막으로 통과시킬 때 계산한 값 그대로다.
    // 이전처럼 edge 시작값(toFixed)과 정수 edge 값, 1/total을 다시 계산하지 않는다. 같은 값이라 색이 같다.
    // 픽셀 8개씩(AVX2 지원 시) 또는 4개씩(SSE) SIMD lane에 나눠 셰이딩하고, 남은 픽셀은 4개 묶음 또는
    // 스칼라로 한다.
    const auto store = [&](std::size_t cell, const SRMath::vec3 &color) SR_FORCEINLINE_LAMBDA
    {
        const int x = m_bounds.minX + static_cast<int>(cell & 15);
        const int y = m_bounds.minY + static_cast<int>(cell >> 4);
        m_colorBase[static_cast<std::size_t>(y - m_originY) * m_colorStride + (x - m_originX)] =
            packColor(color);
    };
    const bool useAvx2 = SRMath::SIMD::avx2_available();
    const int targetBatch = useAvx2 ? 8 : 4;
    std::size_t pending[8] = {};
    int count = 0;
    std::size_t shaded = 0;
    for (std::size_t word = 0; word < m_ownerMask.size(); ++word)
    {
        std::uint64_t bits = m_ownerMask[word];
        m_ownerMask[word] = 0;
        while (bits)
        {
            pending[count++] = word * 64 + static_cast<std::size_t>(std::countr_zero(bits));
            bits &= bits - 1;
            if (count < targetBatch)
                continue;
            count = 0;
            if (targetBatch == 8)
            {
                const PreparedTriangle *const triangles[8] = {
                    m_ownerScratch.triangles[pending[0]], m_ownerScratch.triangles[pending[1]],
                    m_ownerScratch.triangles[pending[2]], m_ownerScratch.triangles[pending[3]],
                    m_ownerScratch.triangles[pending[4]], m_ownerScratch.triangles[pending[5]],
                    m_ownerScratch.triangles[pending[6]], m_ownerScratch.triangles[pending[7]]};
                const OwnerSample *const samples[8] = {
                    &m_ownerScratch.samples[pending[0]], &m_ownerScratch.samples[pending[1]],
                    &m_ownerScratch.samples[pending[2]], &m_ownerScratch.samples[pending[3]],
                    &m_ownerScratch.samples[pending[4]], &m_ownerScratch.samples[pending[5]],
                    &m_ownerScratch.samples[pending[6]], &m_ownerScratch.samples[pending[7]]};
                SRMath::vec3 colors[8];
                ShadeOwner8(triangles, samples, m_frame, colors);
                for (int i = 0; i < 8; ++i)
                    store(pending[i], colors[i]);
                shaded += 8;
            }
            else
            {
                const PreparedTriangle *const triangles[4] = {
                    m_ownerScratch.triangles[pending[0]], m_ownerScratch.triangles[pending[1]],
                    m_ownerScratch.triangles[pending[2]], m_ownerScratch.triangles[pending[3]]};
                const OwnerSample *const samples[4] = {
                    &m_ownerScratch.samples[pending[0]], &m_ownerScratch.samples[pending[1]],
                    &m_ownerScratch.samples[pending[2]], &m_ownerScratch.samples[pending[3]]};
                SRMath::vec3 colors[4];
                ShadeOwnerQuad(triangles, samples, m_frame, colors);
                for (int i = 0; i < 4; ++i)
                    store(pending[i], colors[i]);
                shaded += 4;
            }
        }
    }
    int offset = 0;
    if (count >= 4)
    {
        const PreparedTriangle *const triangles[4] = {
            m_ownerScratch.triangles[pending[0]], m_ownerScratch.triangles[pending[1]],
            m_ownerScratch.triangles[pending[2]], m_ownerScratch.triangles[pending[3]]};
        const OwnerSample *const samples[4] = {
            &m_ownerScratch.samples[pending[0]], &m_ownerScratch.samples[pending[1]],
            &m_ownerScratch.samples[pending[2]], &m_ownerScratch.samples[pending[3]]};
        SRMath::vec3 colors[4];
        ShadeOwnerQuad(triangles, samples, m_frame, colors);
        for (int i = 0; i < 4; ++i)
            store(pending[i], colors[i]);
        shaded += 4;
        offset = 4;
    }
    for (int i = offset; i < count; ++i)
    {
        const PreparedTriangle *t = m_ownerScratch.triangles[pending[i]];
        const OwnerSample &s = m_ownerScratch.samples[pending[i]];
        auto sample = InterpolateSurface(t->rv0, t->rv1, t->rv2, s.a, s.b, s.c, 1.f / s.q);
        if (t->sourceCommand->material->normalTexture)
            ComputeTriangleTangentFrame(*t, sample.tangentWorld, sample.bitangentWorld);
        if (m_frame.mipmapping)
            sample.lod = ComputeTriangleLod(*t);
        store(pending[i], ShadeSurface(sample, *t->sourceCommand->material, m_frame));
        ++shaded;
    }
    if (m_stats)
        m_stats->shadedPixels += shaded;
}

bool Rasterizer::ClipLine(SRMath::vec4 &a, SRMath::vec4 &b)
{
    if (!finiteClip(a) || !finiteClip(b))
        return false;
    // 대부분의 법선은 양 끝점이 절두체 안이다. 실제 교차가 없으면
    // 평면별 거리/분기 여섯 번을 생략한다. 거대한 w는 기존 덧셈 overflow
    // 판정을 유지하도록 느린 경로로 보낸다.
    constexpr float safeW = std::numeric_limits<float>::max() * 0.5f;
    if (a.w > 0.f && b.w > 0.f && a.w <= safeW && b.w <= safeW)
    {
        const auto sign = _mm_set1_ps(-0.f);
        const auto insideA = _mm_cmple_ps(_mm_andnot_ps(sign, a.m128), _mm_set1_ps(a.w));
        const auto insideB = _mm_cmple_ps(_mm_andnot_ps(sign, b.m128), _mm_set1_ps(b.w));
        if ((_mm_movemask_ps(_mm_and_ps(insideA, insideB)) & 7) == 7)
            return true;
    }
    // 삼각형과 같은 near, far, left, right, bottom, top 평면 순서.
    for (const int plane : {4, 5, 0, 1, 2, 3})
    {
        const auto distance = [plane](const SRMath::vec4 &p)
        {
            const float axis = plane < 2 ? p.x : (plane < 4 ? p.y : p.z);
            return p.w + ((plane % 2) != 0 ? -axis : axis);
        };
        const float da = distance(a), db = distance(b);
        if (!std::isfinite(da) || !std::isfinite(db) || (da < 0.f && db < 0.f))
            return false;
        if ((da < 0.f) != (db < 0.f))
        {
            const float amount = da / (da - db);
            SRMath::vec4 p;
            p.m128 = _mm_add_ps(a.m128, _mm_mul_ps(_mm_sub_ps(b.m128, a.m128), _mm_set1_ps(amount)));
            if (da < 0.f)
                a = p;
            else
                b = p;
        }
    }
    return finiteClip(a) && finiteClip(b) && a.w > 0.f && b.w > 0.f;
}

void Rasterizer::DrawScreenLine(SRMath::vec2 a, SRMath::vec2 b, float qa, float qb, std::uint32_t color,
                                ELineAlgorithm algorithm, bool depthTest, float slopeX, float slopeY)
{
    if (m_target.Empty() || m_lineMinY >= m_lineMaxY || !std::isfinite(a.x) || !std::isfinite(a.y) ||
        !std::isfinite(b.x) || !std::isfinite(b.y))
        return;
    // 동차 clipping을 거치지 않는 설명용 오버레이도 과대한 정수 변환은 막는다.
    constexpr double integerLimit = static_cast<double>(std::numeric_limits<int>::max()) / 4.;
    if (std::abs(static_cast<double>(a.x)) > integerLimit ||
        std::abs(static_cast<double>(a.y)) > integerLimit ||
        std::abs(static_cast<double>(b.x)) > integerLimit ||
        std::abs(static_cast<double>(b.y)) > integerLimit)
        return;
    const int startX = static_cast<int>(a.x), startY = static_cast<int>(a.y);
    const int endX = static_cast<int>(b.x), endY = static_cast<int>(b.y);
    // DDA/Bresenham 모두 두 정수 끝점의 Y 범위를 벗어나지 않는다.
    // 끝점을 band 경계로 다시 자르면 반올림/오차 누적이 달라진다.
    // Bresenham은 아래에서 원래 위상을 복원하며, DDA는 원래 시작점부터 누적한다.
    // 수억 step의 DDA는 double 누적 오차가 끝점의 0.5픽셀을 넘을 수 있다.
    // 큰 overlay 입력은 조기 기각만 생략하고 원래 루프를 따른다.
    const bool boundedDda = std::max(std::abs(endX - startX), std::abs(endY - startY)) <= (1 << 20);
    if ((algorithm != ELineAlgorithm::DDA || boundedDda) &&
        (std::max(startY, endY) < m_lineMinY || std::min(startY, endY) >= m_lineMaxY))
        return;
    const double vx = static_cast<double>(b.x) - a.x, vy = static_cast<double>(b.y) - a.y;
    const double lengthSquared = vx * vx + vy * vy;
    const double deltaQ = static_cast<double>(qb) - qa;
    const auto surface = m_target.Surface();
    const auto depth = m_target.Depth();
    const auto depthStride = static_cast<std::size_t>(surface.width);

    // 깊이/기울기/계측 여부는 선분 전체에서 불변이다. 한 번만 분기해
    // overlay에는 깊이 계산을, 법선에는 불필요한 slope 투영을 넣지 않는다.
    // q 보간에 필요한 나눗셈은 역수 곱으로 바꾸지 않아 경계 결과를 보존한다.
    const auto draw = [&]<bool DepthTest, bool Sloped, bool VaryingDepth, bool Instrumented>()
    {
        const LinePixelWriter<DepthTest, Sloped, VaryingDepth, Instrumented> plot{.surface = surface,
                                                                                  .depth = depth,
                                                                                  .stats = m_stats,
                                                                                  .ax = a.x,
                                                                                  .ay = a.y,
                                                                                  .vx = vx,
                                                                                  .vy = vy,
                                                                                  .lengthSquared =
                                                                                      lengthSquared,
                                                                                  .deltaQ = deltaQ,
                                                                                  .qa = qa,
                                                                                  .slopeX = slopeX,
                                                                                  .slopeY = slopeY,
                                                                                  .depthStride = depthStride,
                                                                                  .color = color,
                                                                                  .minY = m_lineMinY,
                                                                                  .maxY = m_lineMaxY};
        if (algorithm == ELineAlgorithm::DDA)
        {
            const int dx = endX - startX, dy = endY - startY;
            const int steps = std::max(std::abs(dx), std::abs(dy));
            if (steps == 0)
            {
                plot(startX, startY);
                return;
            }
            const double incrementX = dx / static_cast<double>(steps),
                         incrementY = dy / static_cast<double>(steps);
            double currentX = startX, currentY = startY;
            for (int i = 0; i <= steps; ++i)
            {
                plot(static_cast<int>(std::round(currentX)), static_cast<int>(std::round(currentY)));
                currentX += incrementX;
                currentY += incrementY;
            }
        }
        else
        {
            const auto rows = restrictBresenhamRows(startX, startY, endX, endY, m_lineMinY, m_lineMaxY);
            int x = rows.x, y = rows.y;
            const int dx = std::abs(endX - startX), sx = startX < endX ? 1 : -1;
            const int dy = -std::abs(endY - startY), sy = startY < endY ? 1 : -1;
            // endpoint guard만으로는 2*error의 int32 overflow를 막지 못한다.
            // 재개 상태와 후속 누적 모두 int64로 유지한다.
            auto error = rows.error;
            while (true)
            {
                plot(x, y);
                if (x == rows.lastX && y == rows.lastY)
                    break;
                const auto twiceError = 2 * error;
                if (twiceError >= dy)
                {
                    error += dy;
                    x += sx;
                }
                if (twiceError <= dx)
                {
                    error += dx;
                    y += sy;
                }
            }
        }
    };
    const auto drawDepth = [&]<bool Sloped, bool VaryingDepth>()
    {
        if (m_stats)
            draw.template operator()<true, Sloped, VaryingDepth, true>();
        else
            draw.template operator()<true, Sloped, VaryingDepth, false>();
    };
    if (!depthTest)
        draw.template operator()<false, false, false, false>();
    else if (slopeX != 0.f || slopeY != 0.f)
    {
        if (qa == qb)
            drawDepth.template operator()<true, false>();
        else
            drawDepth.template operator()<true, true>();
    }
    else
    {
        if (qa == qb)
            drawDepth.template operator()<false, false>();
        else
            drawDepth.template operator()<false, true>();
    }
}

void Rasterizer::BeginLineRows(int minY, int maxY)
{
    EndTile();
    m_lineMinY = std::clamp(minY, 0, m_target.Height());
    m_lineMaxY = std::clamp(maxY, m_lineMinY, m_target.Height());
}

void Rasterizer::DrawWireTriangle(const PreparedTriangle &t, ELineAlgorithm algorithm, bool depthTest)
{
    const auto &a = t.rv0;
    const auto &b = t.rv1;
    const auto &c = t.rv2;
    if (!depthTest)
    {
        // 기존 X-ray 경로에는 깊이/bias가 필요 없다. 면 기울기 계산도 건너뛴다.
        DrawScreenLine(a.screenPos, b.screenPos, a.oneOverW, b.oneOverW, 0xFFFFFFu, algorithm, false);
        DrawScreenLine(b.screenPos, c.screenPos, b.oneOverW, c.oneOverW, 0xFFFFFFu, algorithm, false);
        DrawScreenLine(c.screenPos, a.screenPos, c.oneOverW, a.oneOverW, 0xFFFFFFu, algorithm, false);
        return;
    }
    const double dx1 = b.screenPos.x - a.screenPos.x, dy1 = b.screenPos.y - a.screenPos.y;
    const double dx2 = c.screenPos.x - a.screenPos.x, dy2 = c.screenPos.y - a.screenPos.y;
    const double determinant = dx1 * dy2 - dy1 * dx2;
    float sx = 0.f, sy = 0.f;
    if (std::isfinite(determinant) && std::abs(determinant) > 1e-12)
    {
        const double q1 = b.oneOverW - a.oneOverW, q2 = c.oneOverW - a.oneOverW;
        sx = static_cast<float>((q1 * dy2 - q2 * dy1) / determinant);
        sy = static_cast<float>((dx1 * q2 - dx2 * q1) / determinant);
        if (!std::isfinite(sx) || !std::isfinite(sy))
            sx = sy = 0.f;
    }
    DrawScreenLine(a.screenPos, b.screenPos, a.oneOverW, b.oneOverW, 0xFFFFFFu, algorithm, true, sx, sy);
    DrawScreenLine(b.screenPos, c.screenPos, b.oneOverW, c.oneOverW, 0xFFFFFFu, algorithm, true, sx, sy);
    DrawScreenLine(c.screenPos, a.screenPos, c.oneOverW, a.oneOverW, 0xFFFFFFu, algorithm, true, sx, sy);
}

std::optional<Rasterizer::PreparedDebugLine> Rasterizer::PrepareDepthTestedLine(
    const DebugVertex &start, const DebugVertex &end, const SRMath::mat4 &modelViewProjection, int width,
    int height)
{
    if (width <= 0 || height <= 0)
        return std::nullopt;
    auto a = modelViewProjection * start.position;
    auto b = modelViewProjection * end.position;
    if (!ClipLine(a, b))
        return std::nullopt;
    const float qa = 1.f / a.w, qb = 1.f / b.w;
    const float ax = a.x * qa, ay = a.y * qa;
    const float bx = b.x * qb, by = b.y * qb;
    const SRMath::vec2 screenA{(ax + 1.f) * 0.5f * width, (1.f - ay) * 0.5f * height};
    const SRMath::vec2 screenB{(bx + 1.f) * 0.5f * width, (1.f - by) * 0.5f * height};
    // band 분배 전에 정수 끝점을 구하므로 DrawScreenLine의 동일한 fail-closed
    // 좌표 검사를 여기서 먼저 한다. 수식/연산 순서는 순차 depth 선 경로와 같다.
    constexpr double integerLimit = static_cast<double>(std::numeric_limits<int>::max()) / 4.;
    if (!std::isfinite(screenA.x) || !std::isfinite(screenA.y) || !std::isfinite(screenB.x) ||
        !std::isfinite(screenB.y) || std::abs(static_cast<double>(screenA.x)) > integerLimit ||
        std::abs(static_cast<double>(screenA.y)) > integerLimit ||
        std::abs(static_cast<double>(screenB.x)) > integerLimit ||
        std::abs(static_cast<double>(screenB.y)) > integerLimit)
        return std::nullopt;
    return PreparedDebugLine{screenA, screenB, qa, qb, packColor(SRMath::vec3(start.color))};
}

void Rasterizer::DrawPreparedDebugLine(const PreparedDebugLine &line, ELineAlgorithm algorithm)
{
    // 원본 끝점으로 진행한다. band 경계에서 재클리핑하거나 보간을 재시작하지 않는다.
    DrawScreenLine(line.screenA, line.screenB, line.qa, line.qb, line.color, algorithm, true);
}

void Rasterizer::DrawDebugPrimitive(const DebugPrimitiveCommand &command, ELineAlgorithm algorithm)
{
    if (command.type != DebugPrimitiveType::Line || m_target.Empty())
        return;
    const SRMath::mat4 mvp = m_frame.viewProjection * command.worldTransform;
    const int width = m_target.Width(), height = m_target.Height();
    const auto draw = [&]<bool DepthTest>()
    {
        for (std::size_t i = 0; i + 1 < command.vertices.size(); i += 2)
        {
            auto a = mvp * command.vertices[i].position;
            auto b = mvp * command.vertices[i + 1].position;
            if constexpr (DepthTest)
            {
                if (!ClipLine(a, b))
                    continue;
            }
            else if (!finiteClip(a) || !finiteClip(b) || a.w <= 0.1f || b.w <= 0.1f)
                continue;
            float qa = 1.f, qb = 1.f;
            float ax, ay, bx, by;
            if constexpr (DepthTest)
            {
                qa = 1.f / a.w;
                qb = 1.f / b.w;
                ax = a.x * qa;
                ay = a.y * qa;
                bx = b.x * qb;
                by = b.y * qb;
            }
            else
            {
                // 설명용 overlay의 x/w 순서는 그대로이며 사용하지 않는
                // 역깊이는 만들지 않는다. clipping/정수 끝점 의미도 유지한다.
                ax = a.x / a.w;
                ay = a.y / a.w;
                bx = b.x / b.w;
                by = b.y / b.w;
            }
            const SRMath::vec2 screenA{(ax + 1.f) * 0.5f * width, (1.f - ay) * 0.5f * height};
            const SRMath::vec2 screenB{(bx + 1.f) * 0.5f * width, (1.f - by) * 0.5f * height};
            // 법선에는 원본 면 기울기가 없어 작은 상대 epsilon만 허용한다.
            // 급경사 표면의 시작점을 과도하게 앞으로 끌어내지 않는 보수적 정책이다.
            DrawScreenLine(screenA, screenB, qa, qb, packColor(SRMath::vec3(command.vertices[i].color)),
                           algorithm, DepthTest);
        }
    };
    if (command.depthPolicy == DebugDepthPolicy::DepthTested)
        draw.template operator()<true>();
    else
        draw.template operator()<false>();
}

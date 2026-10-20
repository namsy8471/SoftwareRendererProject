#include "Renderer/Rasterizer.h"
#include "Renderer/RenderCommand.h"
#include "Graphics/Material.h"
#include "Platform/Compiler.h"
#include <algorithm>
#include <cmath>
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
    const auto channel = [](float value) -> std::uint32_t
    { return std::isfinite(value) ? static_cast<std::uint32_t>(std::clamp(value, 0.f, 1.f) * 255.f) : 0u; };
    return (channel(c.x) << 16) | (channel(c.y) << 8) | channel(c.z);
}

// 큰 화면에서도 고정소수점 signed overflow를 만들지 않는다. 2^60 여유는
// 시작 edge와 16픽셀 증분의 합에도 충분하며 기존 int32 안전 범위 결과는 같다.
bool toFixed(float input, std::int64_t &result)
{
    const float scaled = input * 256.f;
    if (!std::isfinite(scaled) || std::abs(static_cast<double>(scaled)) >= 0x1p60)
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
    SRMath::vec2 a;
    double vx, vy, lengthSquared, deltaQ;
    float qa, slopeX, slopeY;
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
                amount =
                    lengthSquared > 0.
                        ? std::clamp(((px + 0.5 - a.x) * vx + (py + 0.5 - a.y) * vy) / lengthSquared, 0., 1.)
                        : 0.;
            double q = qa;
            if constexpr (VaryingDepth)
                q = qa + deltaQ * amount;
            double slopeBias = 0.;
            if constexpr (Sloped)
            {
                const double lineX = a.x + vx * amount, lineY = a.y + vy * amount;
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
    PreparedTriangle t{};
    t.sourceCommand = command;
    t.primitiveId = primitiveId;
    t.rv0 = ToRasterizerVertex(a, width, height);
    t.rv1 = ToRasterizerVertex(b, width, height);
    t.rv2 = ToRasterizerVertex(c, width, height);
    for (const auto *v : {&t.rv0, &t.rv1, &t.rv2})
        if (!std::isfinite(v->screenPos.x) || !std::isfinite(v->screenPos.y) || !std::isfinite(v->oneOverW) ||
            !(v->oneOverW > 0.f) ||
            std::abs(static_cast<double>(v->screenPos.x)) >= std::numeric_limits<int>::max() ||
            std::abs(static_cast<double>(v->screenPos.y)) >= std::numeric_limits<int>::max())
            return std::nullopt;
    const auto &p0 = t.rv0.screenPos;
    const auto &p1 = t.rv1.screenPos;
    const auto &p2 = t.rv2.screenPos;
    // 세 값은 위에서 유한성을 검사했다. 고정 3항 비교로 표현해 작은
    // initializer_list에도 발생하던 범용 min/max 순회 호출을 피한다.
    t.minX = std::max(0, static_cast<int>(std::min(p0.x, std::min(p1.x, p2.x))));
    t.minY = std::max(0, static_cast<int>(std::min(p0.y, std::min(p1.y, p2.y))));
    t.maxX = std::min(width - 1, static_cast<int>(std::max(p0.x, std::max(p1.x, p2.x))));
    t.maxY = std::min(height - 1, static_cast<int>(std::max(p0.y, std::max(p1.y, p2.y))));
    if (t.minX > t.maxX || t.minY > t.maxY)
        return std::nullopt;
    t.dx01 = p0.x - p1.x;
    t.dy01 = p0.y - p1.y;
    t.dx12 = p1.x - p2.x;
    t.dy12 = p1.y - p2.y;
    t.dx20 = p2.x - p0.x;
    t.dy20 = p2.y - p0.y;
    if (!toFixed(t.dx01, t.dx01Fixed) || !toFixed(t.dy01, t.dy01Fixed) || !toFixed(t.dx12, t.dx12Fixed) ||
        !toFixed(t.dy12, t.dy12Fixed) || !toFixed(t.dx20, t.dx20Fixed) || !toFixed(t.dy20, t.dy20Fixed))
        return std::nullopt;
    t.maximumInverseW = std::max(t.rv0.oneOverW, std::max(t.rv1.oneOverW, t.rv2.oneOverW));
    return t;
}

void Rasterizer::BeginTile(TileBounds bounds, bool hiZ)
{
    bounds.minX = std::clamp(bounds.minX, 0, m_target.Width());
    bounds.minY = std::clamp(bounds.minY, 0, m_target.Height());
    bounds.maxX = std::clamp(bounds.maxX, bounds.minX, m_target.Width());
    bounds.maxY = std::clamp(bounds.maxY, bounds.minY, m_target.Height());
    m_bounds = bounds;
    // 4x4 요약 16개는 최대 16x16 타일만 표현한다.
    m_hiZ = hiZ && !m_target.Empty() && bounds.maxX > bounds.minX && bounds.maxY > bounds.minY &&
            bounds.maxX - bounds.minX <= 16 && bounds.maxY - bounds.minY <= 16;
    m_tileFull = false;
    m_blockFull.fill(false);
    if (m_hiZ)
        RefreshHiZ();
}

void Rasterizer::RefreshHiZ()
{
    const auto depth = m_target.Depth();
    const auto ids = m_target.PrimitiveIds();
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
                    const auto index = static_cast<std::size_t>(y) * m_target.Width() + x;
                    const float q = depth[index];
                    if (ids[index] == noPrimitive || !(q > 0.f) || !std::isfinite(q))
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

void Rasterizer::DrawTriangle(const PreparedTriangle &t, RasterPass pass)
{
    if (m_target.Empty())
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
    if (!safeEdge(r0, t.dx12Fixed, t.dy12Fixed) || !safeEdge(r1, t.dx20Fixed, t.dy20Fixed) ||
        !safeEdge(r2, t.dx01Fixed, t.dy01Fixed))
        return;
    auto depth = m_target.Depth();
    auto ids = m_target.PrimitiveIds();
    auto surface = m_target.Surface();
    for (int y = y0; y <= y1; ++y)
    {
        auto w0 = r0, w1 = r1, w2 = r2;
        for (int x = x0; x <= x1; ++x)
        {
            if ((w0 | w1 | w2) >= 0)
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
                    const auto index = static_cast<std::size_t>(y) * m_target.Width() + x;
                    const bool finite = std::isfinite(q) && q > 0.f;
                    const bool visible =
                        Pass == RasterPass::ShadeVisible
                            ? ids[index] == t.primitiveId && depth[index] == q
                            : q > depth[index] || (q == depth[index] && t.primitiveId < ids[index]);
                    if (finite && visible)
                    {
                        if constexpr (Pass != RasterPass::ShadeVisible)
                        {
                            depth[index] = q;
                            ids[index] = t.primitiveId;
                        }
                        if constexpr (Pass != RasterPass::DepthOnly)
                            if (t.sourceCommand && t.sourceCommand->material)
                            {
                                const auto sample = InterpolateSurface(t.rv0, t.rv1, t.rv2, a, b, c, 1.f / q);
                                surface.pixels[static_cast<std::size_t>(y) * surface.stridePixels + x] =
                                    packColor(ShadeSurface(sample, *t.sourceCommand->material, m_frame));
                                if constexpr (Instrumented)
                                    ++m_stats->shadedPixels;
                            }
                    }
                    else if constexpr (Instrumented)
                        ++m_stats->depthRejected;
                }
            }
            // 퇴화/zero-weight 픽셀도 반드시 edge를 진행한다.
            w0 += t.dy12Fixed;
            w1 += t.dy20Fixed;
            w2 += t.dy01Fixed;
        }
        r0 -= t.dx12Fixed;
        r1 -= t.dx20Fixed;
        r2 -= t.dx01Fixed;
    }
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
                                                                                  .a = a,
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

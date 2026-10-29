#pragma once
#include "Math/SRMath.h"
#include "Platform/Compiler.h"
#include <array>
#include <immintrin.h>
#include <limits>

struct AABB
{
    SRMath::vec3 min{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                     std::numeric_limits<float>::max()};
    SRMath::vec3 max{std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(),
                     std::numeric_limits<float>::lowest()};

    // 현재 경계를 다른 경계/점까지 확장한다. 상태를 변경하므로 반환값은 없다.
    void Encapsulate(const AABB &other) noexcept;
    void Encapsulate(const SRMath::vec3 &point) noexcept;

    [[nodiscard]] bool Intersects(const AABB &other) const noexcept;
    [[nodiscard]] bool Contains(const AABB &other) const noexcept;

    // 값 반환형의 top-level const는 이동을 막을 뿐 호출자 보호에 도움이 없어
    [[nodiscard]] SRMath::vec3 Center() const noexcept;
    [[nodiscard]] std::array<SRMath::vec3, 8> Corners() const noexcept;
    [[nodiscard]] bool IsValid() const noexcept;

    [[nodiscard]] AABB Transform(const SRMath::mat4 &transform) const noexcept;
};

// Arvo's algorithm for transforming an AABB by an affine 4x4 matrix using SSE.
// 행렬의 4개 열(column)을 보관하고, 임의의 AABB에 대해
// 18개 내외의 SSE 벡터 명령으로 min/max bound를 정확하게 변환한다.
struct AABBTransformer
{
    __m128 c0, c1, c2, c3;

    SR_FORCEINLINE explicit AABBTransformer(const SRMath::mat4 &transform) noexcept
    {
        const __m128 mask = _mm_castsi128_ps(_mm_set_epi32(0, ~0, ~0, ~0));
        c0 = _mm_and_ps(transform.cols[0].m128, mask);
        c1 = _mm_and_ps(transform.cols[1].m128, mask);
        c2 = _mm_and_ps(transform.cols[2].m128, mask);
        c3 = _mm_and_ps(transform.cols[3].m128, mask);
    }

    [[nodiscard]] SR_FORCEINLINE AABB Transform(const AABB &box) const noexcept
    {
        const __m128 xa = _mm_mul_ps(c0, _mm_set1_ps(box.min.x));
        const __m128 xb = _mm_mul_ps(c0, _mm_set1_ps(box.max.x));
        const __m128 x_lo = _mm_min_ps(xa, xb);
        const __m128 x_hi = _mm_max_ps(xa, xb);

        const __m128 ya = _mm_mul_ps(c1, _mm_set1_ps(box.min.y));
        const __m128 yb = _mm_mul_ps(c1, _mm_set1_ps(box.max.y));
        const __m128 y_lo = _mm_min_ps(ya, yb);
        const __m128 y_hi = _mm_max_ps(ya, yb);

        const __m128 za = _mm_mul_ps(c2, _mm_set1_ps(box.min.z));
        const __m128 zb = _mm_mul_ps(c2, _mm_set1_ps(box.max.z));
        const __m128 z_lo = _mm_min_ps(za, zb);
        const __m128 z_hi = _mm_max_ps(za, zb);

        const __m128 res_min = _mm_add_ps(c3, _mm_add_ps(x_lo, _mm_add_ps(y_lo, z_lo)));
        const __m128 res_max = _mm_add_ps(c3, _mm_add_ps(x_hi, _mm_add_ps(y_hi, z_hi)));

        alignas(16) float minFloats[4];
        alignas(16) float maxFloats[4];
        _mm_store_ps(minFloats, res_min);
        _mm_store_ps(maxFloats, res_max);

        AABB out;
        out.min = SRMath::vec3(minFloats[0], minFloats[1], minFloats[2]);
        out.max = SRMath::vec3(maxFloats[0], maxFloats[1], maxFloats[2]);
        return out;
    }
};

using AABBTransformerAVX2 = AABBTransformer;

SR_FORCEINLINE AABB AABB::Transform(const SRMath::mat4 &transform) const noexcept
{
    const AABBTransformer transformer(transform);
    return transformer.Transform(*this);
}

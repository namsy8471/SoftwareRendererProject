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

// AVX2 8-Corner 단일 배치 변환기
// 행렬의 4개 열(column)을 브로드캐스트하여 보관하고, 여러 AABB에 대해
// 8개 꼭짓점의 아핀 변환 및 min/max reduction을 벡터 레지스터 내에서 일괄 수행한다.
struct AABBTransformerAVX2
{
    __m256 c0_x, c1_x, c2_x, c3_x;
    __m256 c0_y, c1_y, c2_y, c3_y;
    __m256 c0_z, c1_z, c2_z, c3_z;

    SR_FORCEINLINE explicit AABBTransformerAVX2(const SRMath::mat4 &transform) noexcept
        : c0_x(_mm256_set1_ps(transform[0][0])),
          c1_x(_mm256_set1_ps(transform[1][0])),
          c2_x(_mm256_set1_ps(transform[2][0])),
          c3_x(_mm256_set1_ps(transform[3][0])),
          c0_y(_mm256_set1_ps(transform[0][1])),
          c1_y(_mm256_set1_ps(transform[1][1])),
          c2_y(_mm256_set1_ps(transform[2][1])),
          c3_y(_mm256_set1_ps(transform[3][1])),
          c0_z(_mm256_set1_ps(transform[0][2])),
          c1_z(_mm256_set1_ps(transform[1][2])),
          c2_z(_mm256_set1_ps(transform[2][2])),
          c3_z(_mm256_set1_ps(transform[3][2]))
    {
    }

    [[nodiscard]] SR_FORCEINLINE AABB Transform(const AABB &box) const noexcept
    {
        // 8개 꼭짓점의 x, y, z 좌표를 AVX 8-lane으로 패킹
        const __m256 x8 = _mm256_set_ps(box.max.x, box.min.x, box.max.x, box.min.x, box.max.x, box.min.x, box.max.x, box.min.x);
        const __m256 y8 = _mm256_set_ps(box.max.y, box.max.y, box.min.y, box.min.y, box.max.y, box.max.y, box.min.y, box.min.y);
        const __m256 z8 = _mm256_set_ps(box.max.z, box.max.z, box.max.z, box.max.z, box.min.z, box.min.z, box.min.z, box.min.z);

        __m256 x_out = _mm256_fmadd_ps(c0_x, x8, c3_x);
        x_out = _mm256_fmadd_ps(c1_x, y8, x_out);
        x_out = _mm256_fmadd_ps(c2_x, z8, x_out);

        __m256 y_out = _mm256_fmadd_ps(c0_y, x8, c3_y);
        y_out = _mm256_fmadd_ps(c1_y, y8, y_out);
        y_out = _mm256_fmadd_ps(c2_y, z8, y_out);

        __m256 z_out = _mm256_fmadd_ps(c0_z, x8, c3_z);
        z_out = _mm256_fmadd_ps(c1_z, y8, z_out);
        z_out = _mm256_fmadd_ps(c2_z, z8, z_out);

        // 256-bit -> 128-bit reduction (8 -> 4)
        const __m128 x_lo = _mm256_castps256_ps128(x_out);
        const __m128 x_hi = _mm256_extractf128_ps(x_out, 1);
        const __m128 x_min4 = _mm_min_ps(x_lo, x_hi);
        const __m128 x_max4 = _mm_max_ps(x_lo, x_hi);

        const __m128 y_lo = _mm256_castps256_ps128(y_out);
        const __m128 y_hi = _mm256_extractf128_ps(y_out, 1);
        const __m128 y_min4 = _mm_min_ps(y_lo, y_hi);
        const __m128 y_max4 = _mm_max_ps(y_lo, y_hi);

        const __m128 z_lo = _mm256_castps256_ps128(z_out);
        const __m128 z_hi = _mm256_extractf128_ps(z_out, 1);
        const __m128 z_min4 = _mm_min_ps(z_lo, z_hi);
        const __m128 z_max4 = _mm_max_ps(z_lo, z_hi);

        // 128-bit reduction (4 -> 2)
        const __m128 x_min2 = _mm_min_ps(x_min4, _mm_movehl_ps(x_min4, x_min4));
        const __m128 x_max2 = _mm_max_ps(x_max4, _mm_movehl_ps(x_max4, x_max4));

        const __m128 y_min2 = _mm_min_ps(y_min4, _mm_movehl_ps(y_min4, y_min4));
        const __m128 y_max2 = _mm_max_ps(y_max4, _mm_movehl_ps(y_max4, y_max4));

        const __m128 z_min2 = _mm_min_ps(z_min4, _mm_movehl_ps(z_min4, z_min4));
        const __m128 z_max2 = _mm_max_ps(z_max4, _mm_movehl_ps(z_max4, z_max4));

        // 128-bit reduction (2 -> 1)
        const __m128 x_min1 = _mm_min_ss(x_min2, _mm_shuffle_ps(x_min2, x_min2, 1));
        const __m128 x_max1 = _mm_max_ss(x_max2, _mm_shuffle_ps(x_max2, x_max2, 1));

        const __m128 y_min1 = _mm_min_ss(y_min2, _mm_shuffle_ps(y_min2, y_min2, 1));
        const __m128 y_max1 = _mm_max_ss(y_max2, _mm_shuffle_ps(y_max2, y_max2, 1));

        const __m128 z_min1 = _mm_min_ss(z_min2, _mm_shuffle_ps(z_min2, z_min2, 1));
        const __m128 z_max1 = _mm_max_ss(z_max2, _mm_shuffle_ps(z_max2, z_max2, 1));

        AABB newAABB;
        newAABB.min = SRMath::vec3(_mm_cvtss_f32(x_min1), _mm_cvtss_f32(y_min1), _mm_cvtss_f32(z_min1));
        newAABB.max = SRMath::vec3(_mm_cvtss_f32(x_max1), _mm_cvtss_f32(y_max1), _mm_cvtss_f32(z_max1));
        return newAABB;
    }
};

SR_FORCEINLINE AABB AABB::Transform(const SRMath::mat4 &transform) const noexcept
{
    const AABBTransformerAVX2 transformer(transform);
    return transformer.Transform(*this);
}

// 이 파일만 AVX(/arch:AVX, -mavx)로 컴파일한다. 호출은 Skinning.cpp가 CPUID 확인 뒤에만 한다.
#include "Animation/Skinning.h"

#include <immintrin.h>

namespace sr::anim::detail
{
void SkinVerticesAVX(std::span<const Vertex> in, std::span<const SkinInfluence> influences,
                     std::span<const SRMath::mat4> jointMatrices, std::span<Vertex> out, AABB &bounds) noexcept
{
    // 행렬 두 열을 256-bit 레지스터 하나에 담는다: c01 = [열0 | 열1], c23 = [열2 | 열3].
    // 섞기 16번(SSE) → 8번, 위치·법선 변환 각 4번 → 2번 곱으로 줄고, lo/hi 반을 더해 결과를 얻는다.
    __m128 boundsMin = bounds.min.m128, boundsMax = bounds.max.m128;
    const __m128 xyzMask = _mm_castsi128_ps(_mm_set_epi32(0, -1, -1, -1));
    for (std::size_t i = 0; i < in.size(); ++i)
    {
        const SkinInfluence &influence = influences[i];
        __m256 c01 = _mm256_setzero_ps(), c23 = _mm256_setzero_ps();
        for (std::size_t k = 0; k < 4; ++k)
        {
            // 가중치 0 슬롯은 건너뛴다(대부분 정점은 관절 1~2개). 두 구현이 같은 슬롯을 건너뛰어야 비트가 같다.
            if (influence.weights[k] == 0.f)
                continue;
            const float *m = jointMatrices[influence.joints[k]].data;
            const __m256 w = _mm256_set1_ps(influence.weights[k]);
            c01 = _mm256_add_ps(c01, _mm256_mul_ps(_mm256_loadu_ps(m), w));
            c23 = _mm256_add_ps(c23, _mm256_mul_ps(_mm256_loadu_ps(m + 8), w));
        }
        const Vertex &source = in[i];
        // [x x x x | y y y y], [z z z z | 1 1 1 1]
        const __m256 positionXY =
            _mm256_mul_ps(c01, _mm256_set_m128(_mm_set1_ps(source.position.y), _mm_set1_ps(source.position.x)));
        const __m256 positionZW =
            _mm256_mul_ps(c23, _mm256_set_m128(_mm_set1_ps(1.f), _mm_set1_ps(source.position.z)));
        const __m256 normalXY =
            _mm256_mul_ps(c01, _mm256_set_m128(_mm_set1_ps(source.normal.y), _mm_set1_ps(source.normal.x)));
        const __m256 normalZW =
            _mm256_mul_ps(c23, _mm256_set_m128(_mm_setzero_ps(), _mm_set1_ps(source.normal.z)));
        // 스칼라 구현과 같은 묶음: (c0·x + c1·y) + (c2·z + c3·1)
        const __m128 position = _mm_and_ps(
            _mm_add_ps(_mm_add_ps(_mm256_castps256_ps128(positionXY), _mm256_extractf128_ps(positionXY, 1)),
                       _mm_add_ps(_mm256_castps256_ps128(positionZW), _mm256_extractf128_ps(positionZW, 1))),
            xyzMask);
        const __m128 normal = _mm_and_ps(
            _mm_add_ps(_mm_add_ps(_mm256_castps256_ps128(normalXY), _mm256_extractf128_ps(normalXY, 1)),
                       _mm_add_ps(_mm256_castps256_ps128(normalZW), _mm256_extractf128_ps(normalZW, 1))),
            xyzMask);
        out[i].position.m128 = position;
        out[i].normal.m128 = normal;
        boundsMin = _mm_min_ps(boundsMin, position);
        boundsMax = _mm_max_ps(boundsMax, position);
    }
    bounds.min.m128 = _mm_and_ps(boundsMin, xyzMask);
    bounds.max.m128 = _mm_and_ps(boundsMax, xyzMask);
}
} // namespace sr::anim::detail

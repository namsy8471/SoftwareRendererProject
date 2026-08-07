#include "Math/SIMD.h"

#include <immintrin.h>

namespace SRMath::SIMD
{
    // 두 vec4를 __m256의 하위/상위 128-bit lane에 배치해 같은 행렬을 한 번에
    // 적용한다. AVX2 정수 명령은 사용하지 않으므로 /arch:AVX면 충분하다.
    [[nodiscard]] TransformPair transform_pair_avx(const mat4& matrix,
                                                    const vec4& first,
                                                    const vec4& second) noexcept
    {
        const __m256 x = _mm256_set_m128(_mm_set1_ps(second.x), _mm_set1_ps(first.x));
        const __m256 y = _mm256_set_m128(_mm_set1_ps(second.y), _mm_set1_ps(first.y));
        const __m256 z = _mm256_set_m128(_mm_set1_ps(second.z), _mm_set1_ps(first.z));
        const __m256 w = _mm256_set_m128(_mm_set1_ps(second.w), _mm_set1_ps(first.w));

        const __m256 column0 = _mm256_set_m128(matrix.cols[0].m128, matrix.cols[0].m128);
        const __m256 column1 = _mm256_set_m128(matrix.cols[1].m128, matrix.cols[1].m128);
        const __m256 column2 = _mm256_set_m128(matrix.cols[2].m128, matrix.cols[2].m128);
        const __m256 column3 = _mm256_set_m128(matrix.cols[3].m128, matrix.cols[3].m128);

        __m256 transformed = _mm256_mul_ps(column0, x);
        transformed = _mm256_add_ps(transformed, _mm256_mul_ps(column1, y));
        transformed = _mm256_add_ps(transformed, _mm256_mul_ps(column2, z));
        transformed = _mm256_add_ps(transformed, _mm256_mul_ps(column3, w));

        // 결과를 메모리에 저장했다 다시 vec4로 읽는 대신 128-bit lane을
        // 레지스터에서 직접 분리한다. AVX가 두 vec4에 유리한 이유를 살리고
        // 불필요한 32-byte stack round-trip을 없앤다.
        const __m128 firstResult = _mm256_castps256_ps128(transformed);
        const __m128 secondResult = _mm256_extractf128_ps(transformed, 1);
        return {
            vec4{ firstResult },
            vec4{ secondResult }
        };
    }
}

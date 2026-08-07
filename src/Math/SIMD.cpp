#include "Math/SIMD.h"

#include <array>
#include <intrin.h>

namespace SRMath::SIMD
{
    namespace
    {
        // Baseline implementation: x64의 기본 보장인 SSE를 이용해 vec4를
        // 하나씩 계산한다. AVX를 지원하지 않는 CPU에서도 항상 실행 가능하다.
        [[nodiscard]] TransformPair transform_pair_sse(const mat4& matrix,
                                                       const vec4& first,
                                                       const vec4& second) noexcept
        {
            return { matrix * first, matrix * second };
        }

        // C++ 표준 라이브러리에는 아직 x86 ISA 탐지 API가 없으므로 MSVC의
        // CPUID intrinsic을 플랫폼 경계에서만 사용한다.
        [[nodiscard]] bool detect_avx() noexcept
        {
            std::array<int, 4> registers{};
            __cpuid(registers.data(), 0);
            const int maximum_leaf = registers[0];
            if (maximum_leaf < 1)
            {
                return false;
            }

            __cpuidex(registers.data(), 1, 0);
            constexpr int osxsave_bit = 1 << 27;
            constexpr int avx_bit = 1 << 28;
            if ((registers[2] & (osxsave_bit | avx_bit)) != (osxsave_bit | avx_bit))
            {
                return false;
            }

            // XCR0 bit 1(XMM)과 bit 2(YMM)가 모두 켜져 있어야 Windows가
            // 컨텍스트 전환 시 256-bit 레지스터 상태를 보존한다.
            if ((_xgetbv(0) & 0x6) != 0x6)
            {
                return false;
            }

            return true;
        }

    }

    // C++11의 raw 함수 포인터 문법을 읽기 쉬운 using 별칭으로 표현한다.
    // 상태를 가진 펑터가 아니라 동일 시그니처의 SSE/AVX 자유 함수 주소다.
    using TransformPairFunction = TransformPair (*)(const mat4&, const vec4&, const vec4&) noexcept;

    [[nodiscard]] TransformPairFunction select_transform_pair() noexcept;

    [[nodiscard]] bool avx_available() noexcept
    {
        static const bool available = detect_avx();
        return available;
    }

    [[nodiscard]] TransformPair transform_pair(const mat4& matrix,
                                               const vec4& first,
                                               const vec4& second) noexcept
    {
        // 함수 지역 static 초기화는 C++11부터 thread-safe하다. CPUID 검사는
        // 프로세스에서 한 번만 수행되고 핫 패스에는 간접 호출만 남는다.
        static const TransformPairFunction implementation = select_transform_pair();
        return implementation(matrix, first, second);
    }

    // AVX 구현은 SIMD_AVX.cpp만 /arch:AVX로 컴파일한다. 같은 번역 단위에
    // 두면 컴파일러가 baseline 함수에도 AVX를 자동 생성할 수 있기 때문이다.
    [[nodiscard]] TransformPair transform_pair_avx(const mat4&, const vec4&, const vec4&) noexcept;

    [[nodiscard]] TransformPairFunction select_transform_pair() noexcept
    {
        return avx_available() ? transform_pair_avx : transform_pair_sse;
    }
}

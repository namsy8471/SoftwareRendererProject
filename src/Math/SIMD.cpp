#include "Math/SIMD.h"

#include <array>
#include <cstdint>
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#endif

namespace SRMath::SIMD
{
namespace
{
// Baseline implementation: x64의 기본 보장인 SSE를 이용해 vec4를
// 하나씩 계산한다. AVX를 지원하지 않는 CPU에서도 항상 실행 가능하다.
[[nodiscard]] TransformPair transform_pair_sse(const mat4 &matrix, const vec4 &first,
                                               const vec4 &second) noexcept
{
    return {matrix * first, matrix * second};
}

// C++ 표준 라이브러리에는 아직 x86 ISA 탐지 API가 없으므로 컴파일러별
// CPUID/XGETBV intrinsic을 이 두 함수 안에만 둔다.
void cpuid(std::array<int, 4> &registers, int leaf, int subleaf) noexcept
{
#if defined(_MSC_VER)
    __cpuidex(registers.data(), leaf, subleaf);
#else
    unsigned int a = 0, b = 0, c = 0, d = 0;
    __cpuid_count(static_cast<unsigned int>(leaf), static_cast<unsigned int>(subleaf), a, b, c, d);
    registers = {static_cast<int>(a), static_cast<int>(b), static_cast<int>(c), static_cast<int>(d)};
#endif
}

[[nodiscard]] std::uint64_t xgetbv0() noexcept
{
#if defined(_MSC_VER)
    return _xgetbv(0);
#else
    std::uint32_t low = 0, high = 0;
    __asm__ volatile("xgetbv" : "=a"(low), "=d"(high) : "c"(0));
    return (static_cast<std::uint64_t>(high) << 32) | low;
#endif
}

[[nodiscard]] bool detect_avx() noexcept
{
    std::array<int, 4> registers{};
    cpuid(registers, 0, 0);
    const int maximum_leaf = registers[0];
    if (maximum_leaf < 1)
    {
        return false;
    }

    cpuid(registers, 1, 0);
    constexpr int osxsave_bit = 1 << 27;
    constexpr int avx_bit = 1 << 28;
    if ((registers[2] & (osxsave_bit | avx_bit)) != (osxsave_bit | avx_bit))
    {
        return false;
    }

    // XCR0 bit 1(XMM)과 bit 2(YMM)가 모두 켜져 있어야 Windows가
    // 컨텍스트 전환 시 256-bit 레지스터 상태를 보존한다.
    if ((xgetbv0() & 0x6) != 0x6)
    {
        return false;
    }

    return true;
}

} // namespace

using TransformPairFunction = TransformPair (*)(const mat4 &, const vec4 &, const vec4 &) noexcept;

[[nodiscard]] TransformPairFunction select_transform_pair() noexcept;

[[nodiscard]] bool avx_available() noexcept
{
    static const bool available = detect_avx();
    return available;
}

[[nodiscard]] TransformPair transform_pair(const mat4 &matrix, const vec4 &first, const vec4 &second) noexcept
{
    // thread-safe 지역 static으로 CPU/OS 지원 여부를 한 번만 선택한다.
    // 이후 핫 패스에는 같은 시그니처의 구현을 부르는 간접 호출만 남는다.
    static const TransformPairFunction implementation = select_transform_pair();
    return implementation(matrix, first, second);
}

// AVX 구현은 SIMD_AVX.cpp만 /arch:AVX로 컴파일한다. 같은 번역 단위에
// 두면 컴파일러가 baseline 함수에도 AVX를 자동 생성할 수 있기 때문이다.
[[nodiscard]] TransformPair transform_pair_avx(const mat4 &, const vec4 &, const vec4 &) noexcept;

[[nodiscard]] TransformPairFunction select_transform_pair() noexcept
{
    return avx_available() ? transform_pair_avx : transform_pair_sse;
}
} // namespace SRMath::SIMD

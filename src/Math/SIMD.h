#pragma once

#include "Math/SRMath.h"

namespace SRMath::SIMD
{
    // 두 vec4는 정확히 8개의 float이므로 256-bit AVX 레지스터 한 개를
    // 낭비 없이 채운다. 단일 vec4 연산은 기존 128-bit SSE가 더 자연스럽다.
    struct TransformPair
    {
        vec4 first;
        vec4 second;
    };

    // 같은 행렬로 두 벡터를 변환한다. 최초 호출 때 CPU/OS 지원을 검사해
    // AVX 또는 SSE 구현을 고르고, 이후에는 저장된 함수 포인터만 호출한다.
    [[nodiscard]] TransformPair transform_pair(const mat4& matrix,
                                               const vec4& first,
                                               const vec4& second) noexcept;

    // CPUID의 AVX/OSXSAVE 비트와 XCR0의 XMM/YMM 상태 저장 비트를 모두
    // 확인한다. CPU만 AVX를 지원하고 OS가 YMM 저장을 지원하지 않는 경우도
    // 안전하게 false를 반환해야 illegal-instruction 예외를 피할 수 있다.
    [[nodiscard]] bool avx_available() noexcept;
}

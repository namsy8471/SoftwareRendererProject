#pragma once

// 컴파일러 전용 확장은 이 헤더에만 둔다. 렌더러 코어는 이 매크로만 사용해
// MSVC(MSBuild)와 GCC/Clang(CMake) 양쪽에서 같은 의미로 빌드된다.
#if defined(_MSC_VER)
#define SR_FORCEINLINE __forceinline
#define SR_FORCEINLINE_LAMBDA [[msvc::forceinline]]
#else
#define SR_FORCEINLINE inline __attribute__((always_inline))
#define SR_FORCEINLINE_LAMBDA __attribute__((always_inline))
#endif

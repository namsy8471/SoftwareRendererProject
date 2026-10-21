#pragma once

// 외부 사용자는 이 진입점을 유지한다. 구현은 책임별로 나누되 SIMD 배치와
// 연산 순서는 각 헤더에서 보존한다.
#include "Math/FixedPoint.h"
#include "Math/Vector.h"
#include "Math/Matrix.h"
#include "Math/Quaternion.h"

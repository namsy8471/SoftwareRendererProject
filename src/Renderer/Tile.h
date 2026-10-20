#pragma once
#include "Renderer/ShaderVertices.h"
#include <cstdint>

struct MeshRenderCommand;

// 타일의 크기는 binning과 rasterizer가 함께 사용하는 픽셀 단위 상수다.
inline constexpr int tileSize = 16; // 타일의 크기 (16x16 픽셀)

// 클리핑 fan마다 한 번 준비하고 모든 타일이 읽기 전용으로 공유한다.
// 주소가 바뀌지 않는 worker pool이 선/타일 패스 종료까지 수명을 보장한다.
struct PreparedTriangle
{
    const MeshRenderCommand *sourceCommand = nullptr; // 수명은 해당 프레임의 RenderQueue가 소유한다.
    RasterizerVertex rv0, rv1, rv2;
    // 화면으로 clamp한 inclusive 픽셀 경계. 타일과 교집합을 구해 사용한다.
    int minX = 0, minY = 0, maxX = 0, maxY = 0;
    float dx01 = 0.f, dy01 = 0.f, dx12 = 0.f, dy12 = 0.f, dx20 = 0.f, dy20 = 0.f;
    // 기존 float * 256 절삭 규칙은 유지하되 큰 화면의 누적 overflow를 피한다.
    std::int64_t dx01Fixed = 0, dy01Fixed = 0, dx12Fixed = 0, dy12Fixed = 0;
    std::int64_t dx20Fixed = 0, dy20Fixed = 0;
    std::uint64_t primitiveId = 0;
    float maximumInverseW = 0.f;
};

using TriangleRef = PreparedTriangle;

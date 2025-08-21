#pragma once
#include <vector>
#include "Renderer/ShaderVertices.h"

#define CACHE_LINE_SIZE 64

class MeshRenderCommand;

constexpr int TILE_SIZE = 16; // 타일의 크기 (16x16 픽셀)

// 어떤 메쉬의 몇 번째 삼각형인지를 가리키는 구조체
struct alignas(CACHE_LINE_SIZE) TriangleRef
{
    const MeshRenderCommand* sourceCommand;        // 원본 렌더 명령 (월드 행렬, 재질 등의 정보 포함)
    ShadedVertex sv0;                              // 셰이딩된 버텍스들
	ShadedVertex sv1;                              // 셰이딩된 버텍스들
    ShadedVertex sv2;                              // 셰이딩된 버텍스들
};

struct Tile {
    std::vector<TriangleRef> triangles;
};
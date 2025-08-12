#pragma once
#include <vector>

#define CACHE_LINE_SIZE 64

class MeshRenderCommand;

constexpr int TILE_SIZE = 32; // 타일의 크기 (16x16 픽셀)

// 어떤 메쉬의 몇 번째 삼각형인지를 가리키는 구조체
struct alignas(CACHE_LINE_SIZE) TriangleRef
{
    const MeshRenderCommand* sourceCommand; // 원본 렌더 명령 (월드 행렬, 재질 등의 정보 포함)
    uint32_t triangleIndex;                 // 메쉬의 인덱스 버퍼 기준 삼각형 인덱스 (i*3)
};

struct Tile {
    std::vector<TriangleRef> triangles;
};
#pragma once
#include <vector>

#define CACHE_LINE_SIZE 64

class MeshRenderCommand;

constexpr int TILE_SIZE = 32; // Ÿ���� ũ�� (16x16 �ȼ�)

// � �޽��� �� ��° �ﰢ�������� ����Ű�� ����ü
struct alignas(CACHE_LINE_SIZE) TriangleRef
{
    const MeshRenderCommand* sourceCommand; // ���� ���� ��� (���� ���, ���� ���� ���� ����)
    uint32_t triangleIndex;                 // �޽��� �ε��� ���� ���� �ﰢ�� �ε��� (i*3)
};

struct Tile {
    std::vector<TriangleRef> triangles;
};
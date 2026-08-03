#pragma once
#include <vector>
#include "Renderer/ShaderVertices.h"

#define CACHE_LINE_SIZE 64

struct MeshRenderCommand;

constexpr int TILE_SIZE = 16; // Ÿ���� ũ�� (16x16 �ȼ�)

// � �޽��� �� ��° �ﰢ�������� ����Ű�� ����ü
struct alignas(CACHE_LINE_SIZE) TriangleRef
{
    const MeshRenderCommand* sourceCommand;        // ���� ���� ��� (���� ���, ���� ���� ���� ����)
    ShadedVertex sv0;                              // ���̵��� ���ؽ���
	ShadedVertex sv1;                              // ���̵��� ���ؽ���
    ShadedVertex sv2;                              // ���̵��� ���ؽ���
};

struct Tile {
    std::vector<TriangleRef> triangles;
};
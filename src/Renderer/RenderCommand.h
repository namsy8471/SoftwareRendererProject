#pragma once
#include "Math/SRMath.h"
#include "Math/AABB.h"
#include "Utils/DebugUtils.h"
#include <vector>

enum class ERasterizeMode {
	Fill, // ä��� ���
	Wireframe // ���̾������� ���
};

struct Mesh;
struct Material;

// �޽� �������� ���� ��û��
struct MeshRenderCommand {
	const Mesh* sourceMesh; // �������� �޽�
	const std::vector<unsigned int>* indicesToDraw; // �������� �ε�����
	SRMath::mat4 worldTransform; // ���� ��ȯ ���
	const Material* material = nullptr; // �޽��� ����

	ERasterizeMode rasterizeMode = ERasterizeMode::Fill; // ������ȭ ���
};

// ����׿� AABB �������� ���� ��û��
struct DebugPrimitiveCommand {
	std::vector<DebugVertex> vertices; // �׸� ������
	SRMath::mat4 worldTransform;
	DebugPrimitiveType type = DebugPrimitiveType::Line; // ����� ������Ƽ�� Ÿ��

	DebugPrimitiveCommand() = default;
};
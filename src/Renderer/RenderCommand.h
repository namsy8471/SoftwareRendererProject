#pragma once

#include "Math/SRMath.h"
#include "Math/AABB.h"
#include "Utils/DebugUtils.h"
#include <vector>

struct Mesh;

// �޽� �������� ���� ��û��
struct MeshRenderCommand {
	const Mesh* sourceMesh;							// �Ž�
	const std::vector<unsigned int>* indicesToDraw; // �ε�����
	SRMath::mat4 worldTransform;					// ���� ��ȯ
};

// ����׿� AABB �������� ���� ��û��
struct DebugPrimitiveCommand {
	const std::vector<DebugVertex>* vertices; // �׸� ������
	SRMath::mat4 worldTransform;
	DebugPrimitiveType type; // ����� ������Ƽ�� Ÿ��
};
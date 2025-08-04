#pragma once

#include "SRMath.h"
#include "AABB.h"
#include <vector>

struct Mesh;

// �޽� �������� ���� ��û��
struct MeshRenderCommand {
	const Mesh* sourMesh;			// '������' �׸� ���ΰ�?
	SRMath::mat4 worldTransform;	// '���' �׸� ���ΰ�?
};

struct MeshPartRenderCommand {
	const Mesh* sourceMesh;
	const std::vector<unsigned int>* indicesToDraw; // '�� �ε����鸸 �׷���'
	SRMath::mat4 worldTransform;
};

// ����׿� AABB �������� ���� ��û��
struct DebugAABBRenderCommand {
	AABB bounds;
	SRMath::mat4 worldTransform;
	SRMath::vec4 color = { 1.0f, 1.0f, 1.0f, 1.0f }; // �⺻ ���
};
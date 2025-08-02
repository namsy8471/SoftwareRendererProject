#pragma once

#include "SRMath.h"
#include "AABB.h"

struct Mesh;

struct MeshRenderCommand{
	const Mesh* mesh;          // �������� �޽�
	SRMath::mat4 modelMatrix;  // �� ��ȯ ���
	SRMath::mat4 mvp;          // ��-��-�������� ���
	SRMath::mat4 mv;           // ��-�� ���
	SRMath::mat4 normal_matrix; // ���� ��ȯ ���
	SRMath::vec3 light_dir;    // ���� ���� ����
	AABB aabb;                 // �޽��� AABB (Axis-Aligned Bounding Box)
};

// ����׿� AABB �������� ���� ��û��
struct DebugAABBRenderCommand {
	AABB bounds;
	SRMath::mat4 worldTransform;
	SRMath::vec4 color = { 1.0f, 1.0f, 1.0f, 1.0f }; // �⺻ ���
};
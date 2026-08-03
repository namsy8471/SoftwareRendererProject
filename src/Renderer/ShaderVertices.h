#pragma once

#include "Math/SRMath.h"

// Vertex Shading
// ����: ���� ���̵� ���(����/Ŭ�� ��ǥ, ����, UV)�� ��� ����ü
struct ShadedVertex {
    SRMath::vec3 posWorld;
    SRMath::vec4 posClip;
    SRMath::vec3 normalWorld;
    SRMath::vec2 texcoord;

    // ������ �߰�
    ShadedVertex() = default;
    ShadedVertex(const SRMath::vec3& posWorld, const SRMath::vec4& posClip, const SRMath::vec3& normalWorld, const SRMath::vec2& tex)
        : posWorld(posWorld), posClip(posClip), normalWorld(normalWorld), texcoord(tex) {
    }
};

// --- ������ȭ�� ���� ���� ���� ������ �غ� ---
// ����: ���� ���� �� ����Ʈ ��ȯ ����, �ȼ� ���̵��� �ʿ��� ������
struct RasterizerVertex {
    SRMath::vec2 screenPos;         // ���� ȭ�� ��ǥ
    float oneOverW = 0.0f;                 // ���� ������ ���� 1/w
    SRMath::vec3 normalWorldOverW;  // ���� ������ ����
    SRMath::vec2 texcoordOverW;     // ���� ������ UV
    SRMath::vec3 worldPosOverW;     // ���� ������ ���� ��ǥ

	// ������ �߰�
	/*RasterizerVertex() = default;
    RasterizerVertex(const SRMath::vec2& screenPos, float oneOverW, const SRMath::vec3& normalWorldOverW,
        const SRMath::vec2& texcoordOverW, const SRMath::vec3& worldPosOverW)
        : screenPos(screenPos), oneOverW(oneOverW), normalWorldOverW(normalWorldOverW),
        texcoordOverW(texcoordOverW), worldPosOverW(worldPosOverW) {
	}*/
};
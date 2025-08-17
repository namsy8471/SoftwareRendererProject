#pragma once

#include "Math/SRMath.h"

// Vertex Shading
// 설명: 정점 셰이딩 결과(월드/클립 좌표, 법선, UV)를 담는 구조체
struct ShadedVertex {
    SRMath::vec3 posWorld;
    SRMath::vec4 posClip;
    SRMath::vec3 normalWorld;
    SRMath::vec2 texcoord;

    // 생성자 추가 (컴파일 오류 해결)
    ShadedVertex() = default;
    ShadedVertex(const SRMath::vec3& posWorld, const SRMath::vec4& posClip, const SRMath::vec3& normalWorld, const SRMath::vec2& tex)
        : posWorld(posWorld), posClip(posClip), normalWorld(normalWorld), texcoord(tex) {
    }
};

// --- 래스터화를 위한 최종 정점 데이터 준비 ---
// 설명: 원근 분할 및 뷰포트 변환 이후, 픽셀 셰이딩에 필요한 데이터
struct RasterizerVertex {
    SRMath::vec2 screenPos;         // 최종 화면 좌표
    float oneOverW;                 // 원근 보간을 위한 1/w
    SRMath::vec3 normalWorldOverW;  // 원근 보정된 법선
    SRMath::vec2 texcoordOverW;     // 원근 보정된 UV
    SRMath::vec3 worldPosOverW;     // 원근 보정된 월드 좌표
};
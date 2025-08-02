#pragma once

#include "SRMath.h"
#include "AABB.h"

struct Mesh;

struct MeshRenderCommand{
	const Mesh* mesh;          // 렌더링할 메쉬
	SRMath::mat4 modelMatrix;  // 모델 변환 행렬
	SRMath::mat4 mvp;          // 모델-뷰-프로젝션 행렬
	SRMath::mat4 mv;           // 모델-뷰 행렬
	SRMath::mat4 normal_matrix; // 법선 변환 행렬
	SRMath::vec3 light_dir;    // 조명 방향 벡터
	AABB aabb;                 // 메쉬의 AABB (Axis-Aligned Bounding Box)
};

// 디버그용 AABB 렌더링을 위한 요청서
struct DebugAABBRenderCommand {
	AABB bounds;
	SRMath::mat4 worldTransform;
	SRMath::vec4 color = { 1.0f, 1.0f, 1.0f, 1.0f }; // 기본 흰색
};
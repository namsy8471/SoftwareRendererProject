#pragma once

#include "SRMath.h"
#include "AABB.h"
#include <vector>

struct Mesh;

// 메시 렌더링을 위한 요청서
struct MeshRenderCommand {
	const Mesh* sourMesh;			// '무엇을' 그릴 것인가?
	SRMath::mat4 worldTransform;	// '어디에' 그릴 것인가?
};

struct MeshPartRenderCommand {
	const Mesh* sourceMesh;
	const std::vector<unsigned int>* indicesToDraw; // '이 인덱스들만 그려줘'
	SRMath::mat4 worldTransform;
};

// 디버그용 AABB 렌더링을 위한 요청서
struct DebugAABBRenderCommand {
	AABB bounds;
	SRMath::mat4 worldTransform;
	SRMath::vec4 color = { 1.0f, 1.0f, 1.0f, 1.0f }; // 기본 흰색
};
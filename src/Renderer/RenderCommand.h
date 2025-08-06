#pragma once

#include "Math/SRMath.h"
#include "Math/AABB.h"
#include "Utils/DebugUtils.h"
#include <vector>

struct Mesh;

// 메시 렌더링을 위한 요청서
struct MeshRenderCommand {
	const Mesh* sourceMesh;							// 매시
	const std::vector<unsigned int>* indicesToDraw; // 인덱스들
	SRMath::mat4 worldTransform;					// 월드 변환
};

// 디버그용 AABB 렌더링을 위한 요청서
struct DebugPrimitiveCommand {
	const std::vector<DebugVertex>* vertices; // 그릴 정점들
	SRMath::mat4 worldTransform;
	DebugPrimitiveType type; // 디버그 프리미티브 타입
};
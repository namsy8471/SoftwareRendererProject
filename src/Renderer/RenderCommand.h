#pragma once
#include "Math/SRMath.h"
#include "Math/AABB.h"
#include "Utils/DebugUtils.h"
#include <vector>

enum class ERasterizeMode {
	Fill, // 채우기 모드
	Wireframe // 와이어프레임 모드
};

struct Mesh;
struct Material;

// 메시 렌더링을 위한 요청서
struct MeshRenderCommand {
	const Mesh* sourceMesh; // 렌더링할 메시
	const std::vector<unsigned int>* indicesToDraw; // 렌더링할 인덱스들
	SRMath::mat4 worldTransform; // 월드 변환 행렬
	const Material* material = nullptr; // 메시의 재질

	ERasterizeMode rasterizeMode = ERasterizeMode::Fill; // 래스터화 모드
};

// 디버그용 AABB 렌더링을 위한 요청서
struct DebugPrimitiveCommand {
	std::vector<DebugVertex> vertices; // 그릴 정점들
	SRMath::mat4 worldTransform;
	DebugPrimitiveType type; // 디버그 프리미티브 타입
};
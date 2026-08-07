#pragma once
#include "Math/SRMath.h"
#include "Math/AABB.h"
#include "Utils/DebugUtils.h"
#include <vector>
#include <span>

enum class ERasterizeMode {
	Fill, // 채우기 모드
	Wireframe // 와이어프레임 모드
};

struct Mesh;
struct Material;

// 메시 렌더링을 위한 요청서
struct MeshRenderCommand {
	// 두 포인터와 span은 비소유 뷰이며 한 프레임 동안 원본 Model이 살아 있다.
	// nullptr 기본값은 미완성 명령을 디버거에서 즉시 식별하게 한다.
	const Mesh* sourceMesh = nullptr;
	std::span<const unsigned int> indicesToDraw; // 렌더링할 인덱스들
	SRMath::mat4 worldTransform; // 월드 변환 행렬
	const Material* material = nullptr; // 메시의 재질

	ERasterizeMode rasterizeMode = ERasterizeMode::Fill; // 래스터화 모드
};

// 디버그용 렌더링을 위한 요청서
struct DebugPrimitiveCommand {
	std::vector<DebugVertex> vertices; // 그릴 정점들
	SRMath::mat4 worldTransform;
	DebugPrimitiveType type = DebugPrimitiveType::Line; // 디버그 프리미티브 타입

};

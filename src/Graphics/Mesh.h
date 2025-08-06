#pragma once
#include <vector>
#include <string>
#include <memory>
#include "Math/SRMath.h"

class Octree;

struct Vertex {
	SRMath::vec3 position;	// v
	SRMath::vec2 texcoord;	// vt
	SRMath::vec3 normal;	// vn
};

struct Mesh {
	std::vector<Vertex>       vertices;     // 이 메시에 속한 고유 정점 데이터 (VBO용)
	std::vector<unsigned int> indices;      // 정점을 연결해 삼각형을 만드는 방법 (IBO용)
	std::string               materialName; // 이 메시에 적용할 재질의 이름
	std::unique_ptr<Octree>	  octree;

	Mesh();
	~Mesh();
	Mesh(Mesh&&) noexcept;
	Mesh& operator=(Mesh&&) noexcept;
};


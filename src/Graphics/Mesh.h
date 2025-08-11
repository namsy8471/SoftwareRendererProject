#pragma once
#include <vector>
#include <string>
#include <memory>
#include "Math/SRMath.h"
#include "Graphics/Material.h"

class Octree;

struct Vertex {
	SRMath::vec3 position;	// v
	SRMath::vec2 texcoord;	// vt
	SRMath::vec3 normal;	// vn
};

struct Mesh {
	std::vector<Vertex>       vertices;     // �� �޽ÿ� ���� ���� ���� ������ (VBO��)
	std::vector<unsigned int> indices;      // ������ ������ �ﰢ���� ����� ��� (IBO��)
	Material				  material;		// �� �޽ÿ� ������ ����
	std::unique_ptr<Octree>	  octree;

	Mesh();
	~Mesh();
	Mesh(Mesh&&) noexcept;
	Mesh& operator=(Mesh&&) noexcept;
};


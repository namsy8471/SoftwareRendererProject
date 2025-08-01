#pragma once
#include <vector>
#include <string>
#include "SRMath.h"
#include "ModelLoader.h"
#include "Octree.h"

struct Vertex {
	SRMath::vec3 position;	// v
	SRMath::vec2 texcoord;	// vt
	SRMath::vec3 normal;	// vn
};

struct Mesh {
	std::vector<Vertex>       vertices;     // 이 메시에 속한 고유 정점 데이터 (VBO용)
	std::vector<unsigned int> indices;      // 정점을 연결해 삼각형을 만드는 방법 (IBO용)
	std::string               materialName; // 이 메시에 적용할 재질의 이름
	Octree					  octree;
};

class Model
{
	// To Optimize to use private variable in LoadOBJ
	friend std::unique_ptr<Model> ModelLoader::LoadOBJ(const std::string& filepath);

private:
	std::vector<Mesh> m_meshes;
	SRMath::vec3 m_boundingSphereCenter;
	float m_boundingSphereRadius;

public:
	const std::vector<Mesh>& GetMeshes() const { return m_meshes; }
	const SRMath::vec3 GetBoundingSphereCenter() const { return m_boundingSphereCenter; }
	const float GetBoundingSphereRadius() const { return m_boundingSphereRadius; }
};

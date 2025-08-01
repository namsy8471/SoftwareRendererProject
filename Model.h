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
	std::vector<Vertex>       vertices;     // �� �޽ÿ� ���� ���� ���� ������ (VBO��)
	std::vector<unsigned int> indices;      // ������ ������ �ﰢ���� ����� ��� (IBO��)
	std::string               materialName; // �� �޽ÿ� ������ ������ �̸�
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

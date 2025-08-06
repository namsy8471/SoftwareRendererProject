#pragma once
#include <vector>
#include "Graphics/ModelLoader.h"
#include "Math/SRMath.h"
#include "Graphics/Mesh.h"
#include "Math/AABB.h"

class RenderQueue; // ���� ����

class Model
{
	// To Optimize to use private variable in LoadOBJ
	friend std::unique_ptr<Model> ModelLoader::LoadOBJ(const std::string& filepath);

private:
	std::vector<Mesh> m_meshes;
	AABB m_localAABB; // <-- �� ��ü�� ���� AABB ��� �߰�

public:
	const std::vector<Mesh>& GetMeshes() const { return m_meshes; }
	const AABB& GetLocalAABB() const { return m_localAABB; }

	void SubmitToRenderQueue(RenderQueue& renderQueue, const SRMath::mat4& worldTransform);
};

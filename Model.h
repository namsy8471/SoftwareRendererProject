#pragma once
#include <vector>
#include "ModelLoader.h"
#include "SRMath.h"
#include "Mesh.h"


class RenderQueue; // 전방 선언

class Model
{
	// To Optimize to use private variable in LoadOBJ
	friend std::unique_ptr<Model> ModelLoader::LoadOBJ(const std::string& filepath);

private:

	std::vector<Mesh> m_meshes;

public:
	const std::vector<Mesh>& GetMeshes() const { return m_meshes; }
	void SubmitToRenderQueue(RenderQueue& renderQueue, const SRMath::mat4& worldTransform);

};

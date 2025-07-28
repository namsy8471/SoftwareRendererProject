#pragma once
#include <vector>
#include <string>
#include "SRMath.h"
#include "ModelLoader.h"

class Model
{
	// To Optimize to use private variable in LoadOBJ
	friend std::unique_ptr<Model> ModelLoader::LoadOBJ(const std::string& filepath);

private:
	std::vector<SRMath::vec3> m_positions;	// v
	std::vector<SRMath::vec2> m_texcoords;	// vt
	std::vector<SRMath::vec3> m_normals;		// vn
	std::vector<int>		  m_pos_indices;	// f
	std::vector<int>		  m_tex_indices;	// f
	std::vector<int>		  m_nrm_indices;	// f

public:

	const std::vector<SRMath::vec3>& GetPositions() const { return m_positions; }
	const std::vector<SRMath::vec2>& GetTexcoords() const { return m_texcoords; }
	const std::vector<SRMath::vec3>& Getnormals() const { return m_normals; }
	const std::vector<int>& GetPositionIndices() const { return m_pos_indices; }
	const std::vector<int>& GetTextureIndices() const { return m_tex_indices; }
	const std::vector<int>& GetNormalIndices() const { return m_nrm_indices; }

};

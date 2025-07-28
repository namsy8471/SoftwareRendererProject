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
	std::vector<SRMath::vec3> positions;	// v
	std::vector<SRMath::vec2> texcoords;	// vt
	std::vector<SRMath::vec3> normals;		// vn
	std::vector<int>		  pos_indices;	// f
	std::vector<int>		  tex_indices;	// f
	std::vector<int>		  nrm_indices;	// f

public:

	const std::vector<SRMath::vec3>& GetPositions() const { return positions; }
	const std::vector<SRMath::vec2>& GetTexcoords() const { return texcoords; }
	const std::vector<SRMath::vec3>& Getnormals() const { return normals; }
	const std::vector<int>& GetPositionIndices() const { return pos_indices; }
	const std::vector<int>& GetTextureIndices() const { return tex_indices; }
	const std::vector<int>& GetNormalIndices() const { return nrm_indices; }

};

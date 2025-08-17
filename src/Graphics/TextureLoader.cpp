#include "TextureLoader.h"
#include <fstream>
#include <sstream>
#include "Texture.h"
#include "Material.h"


#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

void StbiImageDeleter::operator()(unsigned char* p) const
{
    stbi_image_free(p);
}

std::shared_ptr<Texture> TextureLoader::LoadImageFile(const std::string& filepath)
{
    auto texture = std::make_shared<Texture>();

    int channels;
    unsigned char* pixels = stbi_load(filepath.c_str(), &texture->m_width, &texture->m_height, &channels, 4);

    if (!pixels) return nullptr;

    texture->SetPixels(StbiImagePtr(pixels));
    return texture;
}

std::unordered_map<std::string, Material> TextureLoader::LoadMTLFile(const std::string& filepath)
{
	std::unordered_map<std::string, Material> materials;
	std::ifstream file(filepath);

	if (!file.is_open()) return materials;

	materials.reserve(100); // �ʱ� �뷮 ���� (MTL ������ ���� ������ ���� ����)

	std::string line;
    Material* currentMaterial = nullptr;

    while (std::getline(file, line))
    {
		if (line.empty() || line[0] == '#') continue; // �� �� �Ǵ� �ּ��� ����

		std::stringstream ss(line);
		std::string prefix;
		ss >> prefix;

		if (prefix == "newmtl")
		{
			std::string materialName;
			ss >> materialName;

			materials[materialName] = Material();
			currentMaterial = &materials[materialName];
			currentMaterial->name = materialName;
		}

		else if (currentMaterial != nullptr)
		{
			if (prefix == "Ka")
				ss >> currentMaterial->ka.x >> currentMaterial->ka.y >> currentMaterial->ka.z;// Ambient color
			else if (prefix == "Kd")
				ss >> currentMaterial->kd.x >> currentMaterial->kd.y >> currentMaterial->kd.z; // Diffuse color
			else if (prefix == "Ks")
				ss >> currentMaterial->ks.x >> currentMaterial->ks.y >> currentMaterial->ks.z; // Specular color
			else if (prefix == "Ns")
				ss >> currentMaterial->Ns; // Shininess factor
			else if (prefix == "d")
				ss >> currentMaterial->d; // Diffuse texture map
			else if (prefix == "illum")
				ss >> currentMaterial->illum; // Diffuse texture file
		}
    }

	auto shrink_unordered_map = [](auto& map) {
		std::unordered_map<std::string, Material> temp_map;

		map.swap(temp_map); // ���� ���� ���� ���� �Ҵ�
		return temp_map; // ���� �Ҵ�� �� ��ȯ
		};

	materials = shrink_unordered_map(materials); // �޸� ����ȭ�� ���� ���� ���

	return materials;
}

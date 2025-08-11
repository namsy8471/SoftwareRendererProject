#pragma once
#include <string>
#include <unordered_map>
#include <memory>

struct StbiImageDeleter
{
	void operator()(unsigned char* p) const;
};

using StbiImagePtr = std::unique_ptr<unsigned char, StbiImageDeleter>;

class Texture;
class Material;

class TextureLoader
{
public:
	static std::shared_ptr<Texture> LoadImageFile(const std::string& filepath);
	static std::unordered_map<std::string, Material> LoadMTLFile(const std::string& filepath);
};


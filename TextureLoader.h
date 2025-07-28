#pragma once
#include <string>
#include <memory>

struct StbiImageDeleter
{
	void operator()(unsigned char* p) const;
};

using StbiImagePtr = std::unique_ptr<unsigned char, StbiImageDeleter>;

class Texture;

class TextureLoader
{
public:
	static std::shared_ptr<Texture> LoadImageFile(const std::string& filepath);
};


#pragma once
#include "TextureLoader.h"
#include <memory>

class Texture
{
	friend std::shared_ptr<Texture> TextureLoader::LoadImageFile(const std::string& filepath);

private:
	StbiImagePtr m_pPixels = nullptr;
	int m_width = 0;
	int m_height = 0;

public:
	Texture();
	~Texture();

	const unsigned int GetPixels(float u, float v) const;
	const int GetWidth() const { return m_width; }
	const int GetHeight() const { return m_height; }

	void SetPixels(StbiImagePtr pixels) { m_pPixels = std::move(pixels); }
};


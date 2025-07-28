#include "TextureLoader.h"
#include "Texture.h"

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
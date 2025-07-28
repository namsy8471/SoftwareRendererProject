#include "TextuerLoader.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

void StbiImageDeleter::operator()(unsigned char* p) const
{
    stbi_image_free(p);
}

StbiImagePtr TextuerLoader::LoadImageFile(const std::string& filepath, int& width, int& height)
{
    int channels;
    unsigned char* pixels = stbi_load(filepath.c_str(), &width, &height, &channels, 4);

    return StbiImagePtr(pixels);
}

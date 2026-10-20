#include "Texture.h"
#include <algorithm>
#include <utility>

Texture::Texture() = default;
Texture::Texture(int width, int height, std::vector<unsigned char> rgba)
{
    if (width <= 0 || height <= 0 ||
        rgba.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4)
        return;
    m_pixels = std::move(rgba);
    m_width = width;
    m_height = height;
}
Texture::~Texture() = default;

SRMath::Color Texture::Sample(float u, float v) const noexcept
{
    if (m_pixels.empty())
        return {};

    const int x = static_cast<int>(std::clamp(u, 0.0f, 1.0f) * (m_width - 1));
    const int y = static_cast<int>(std::clamp(v, 0.0f, 1.0f) * (m_height - 1));
    const std::size_t offset = (static_cast<std::size_t>(y) * m_width + x) * 4;
    const auto *pixels = m_pixels.data();
    constexpr float byteToUnit = 1.0f / 255.0f;

    return {pixels[offset] * byteToUnit, pixels[offset + 1] * byteToUnit, pixels[offset + 2] * byteToUnit};
}

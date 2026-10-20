#include "Texture.h"
#include <algorithm>

Texture::Texture() = default;
Texture::~Texture() = default;

SRMath::Color Texture::Sample(float u, float v) const noexcept
{
    if (!m_pPixels || m_width == 0 || m_height == 0)
        return {};

    const int x = static_cast<int>(std::clamp(u, 0.0f, 1.0f) * (m_width - 1));
    const int y = static_cast<int>(std::clamp(v, 0.0f, 1.0f) * (m_height - 1));
    const std::size_t offset = (static_cast<std::size_t>(y) * m_width + x) * 4;
    const auto *pixels = m_pPixels.get();
    constexpr float byteToUnit = 1.0f / 255.0f;

    return {pixels[offset] * byteToUnit, pixels[offset + 1] * byteToUnit, pixels[offset + 2] * byteToUnit};
}

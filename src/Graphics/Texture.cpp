#include "Texture.h"
#include <algorithm>

Texture::Texture()
{
}

Texture::~Texture()
{
}

const unsigned int Texture::GetPixels(float u, float v) const
{
	if (!m_pPixels || m_width == 0 || m_height == 0)
		return 0;

	int x = static_cast<int>(std::clamp(u, 0.0f, 1.0f) * (m_width - 1));
	int y = static_cast<int>(std::clamp(v, 0.0f, 1.0f) * (m_height - 1));

	unsigned int* pixels_as_int = (unsigned int*)m_pPixels.get();
	return pixels_as_int[y * m_width + x];
}

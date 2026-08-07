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
	const auto* pixels = m_pPixels.get();
	constexpr float byte_to_unit = 1.0f / 255.0f;

	// stbi_load(..., 4)는 메모리에 R,G,B,A 순서로 저장한다. 이전 구현은 네
	// 바이트를 unsigned int로 묶은 뒤 float 한 값으로 변환해 텍스처가 거의
	// 흰색이 되는 문제가 있었다. 채널 단위의 값 타입을 반환해 이를 막는다.
	return {
		pixels[offset] * byte_to_unit,
		pixels[offset + 1] * byte_to_unit,
		pixels[offset + 2] * byte_to_unit
	};
}

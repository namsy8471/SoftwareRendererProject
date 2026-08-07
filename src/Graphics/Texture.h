#pragma once
#include "TextureLoader.h"
#include "Math/SRMath.h"
#include <memory>

class Texture
{
	friend std::expected<std::shared_ptr<Texture>, AssetLoadError> TextureLoader::LoadImageFile(const std::filesystem::path& filepath);

private:
	StbiImagePtr m_pPixels = nullptr;
	int m_width = 0;
	int m_height = 0;

public:
	Texture();
	~Texture();

	// 정규화 UV를 가장 가까운 texel의 RGB 값으로 변환한다. packed 정수를
	// 노출하지 않으므로 호출자가 픽셀 포맷/엔디언을 잘못 해석할 수 없다.
	[[nodiscard]] SRMath::Color Sample(float u, float v) const noexcept;
	[[nodiscard]] int GetWidth() const noexcept { return m_width; }
	[[nodiscard]] int GetHeight() const noexcept { return m_height; }

	void SetPixels(StbiImagePtr pixels) noexcept { m_pPixels = std::move(pixels); }
};

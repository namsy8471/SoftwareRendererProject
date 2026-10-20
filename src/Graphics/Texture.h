#pragma once
#include "Math/SRMath.h"
#include <cstddef>
#include <vector>

// RGBA8 픽셀을 값으로 소유하는 이미지다. 어떤 디코더(stb, 자체 파서)가 만들었는지 모른다.
class Texture
{
  private:
    std::vector<unsigned char> m_pixels; // width * height * 4, 위에서 아래 행 순서
    int m_width = 0;
    int m_height = 0;

  public:
    Texture();
    // rgba.size()가 width * height * 4가 아니면 빈 텍스처가 되어 Sample이 검정을 돌려준다.
    Texture(int width, int height, std::vector<unsigned char> rgba);
    ~Texture();

    // 정규화 UV를 가장 가까운 texel의 RGB 값으로 변환한다. packed 정수를
    // 노출하지 않으므로 호출자가 픽셀 포맷/엔디언을 잘못 해석할 수 없다.
    [[nodiscard]] SRMath::Color Sample(float u, float v) const noexcept;
    [[nodiscard]] int GetWidth() const noexcept { return m_width; }
    [[nodiscard]] int GetHeight() const noexcept { return m_height; }
};

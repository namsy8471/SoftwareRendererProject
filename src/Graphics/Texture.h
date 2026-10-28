#pragma once
#include "Math/SRMath.h"
#include <cstddef>
#include <vector>

// [0,1] 밖 UV를 다루는 방법. glTF sampler의 wrapS/wrapT와 같다. 기본은 기존 동작인 가장자리 고정이다.
enum class TextureWrap : unsigned char
{
    ClampToEdge,
    Repeat,
    MirroredRepeat
};

enum class TextureFilter : unsigned char
{
    Nearest,
    Bilinear,
    Trilinear
};

// RGBA8 픽셀을 값으로 소유하는 이미지다. 어떤 디코더(src/Graphics/Codecs)가 만들었는지 모른다.
class Texture
{
  public:
    struct MipLevel
    {
        int width = 0;
        int height = 0;
        std::vector<unsigned char> pixels;
    };

  private:
    std::vector<unsigned char> m_pixels; // width * height * 4, 위에서 아래 행 순서
    int m_width = 0;
    int m_height = 0;
    TextureWrap m_wrapU = TextureWrap::ClampToEdge;
    TextureWrap m_wrapV = TextureWrap::ClampToEdge;
    TextureFilter m_filter = TextureFilter::Nearest;
    std::vector<MipLevel> m_mips;

  public:
    Texture();
    // rgba.size()가 width * height * 4가 아니면 빈 텍스처가 되어 Sample이 검정을 돌려준다.
    Texture(int width, int height, std::vector<unsigned char> rgba);
    ~Texture();

    // 정규화 UV를 가장 가까운 texel의 RGB 값으로 변환한다. packed 정수를
    // 노출하지 않으므로 호출자가 픽셀 포맷/엔디언을 잘못 해석할 수 없다.
    [[nodiscard]] SRMath::Color Sample(float u, float v) const noexcept;
    [[nodiscard]] SRMath::Color SampleNearest(float u, float v, int mipLevel = 0) const noexcept;
    [[nodiscard]] SRMath::Color SampleBilinear(float u, float v, int mipLevel = 0) const noexcept;
    [[nodiscard]] SRMath::Color Sample(float u, float v, float lod) const noexcept;

    void GenerateMipmaps();
    [[nodiscard]] bool HasMipmaps() const noexcept { return !m_mips.empty(); }
    [[nodiscard]] int GetMipCount() const noexcept
    {
        return m_pixels.empty() ? 0 : static_cast<int>(m_mips.size()) + 1;
    }
    [[nodiscard]] int GetMipWidth(int mipLevel) const noexcept;
    [[nodiscard]] int GetMipHeight(int mipLevel) const noexcept;

    [[nodiscard]] int GetWidth() const noexcept { return m_width; }
    [[nodiscard]] int GetHeight() const noexcept { return m_height; }
    void SetWrap(TextureWrap u, TextureWrap v) noexcept
    {
        m_wrapU = u;
        m_wrapV = v;
    }
    [[nodiscard]] TextureWrap GetWrapU() const noexcept { return m_wrapU; }
    [[nodiscard]] TextureWrap GetWrapV() const noexcept { return m_wrapV; }
    void SetFilter(TextureFilter filter) noexcept { m_filter = filter; }
    [[nodiscard]] TextureFilter GetFilter() const noexcept { return m_filter; }
};

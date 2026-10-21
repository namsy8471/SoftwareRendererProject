// Truevision TGA 디코더. 15/16비트 픽셀은 알파 비트를 무시하고 불투명 RGB로 읽는다
// (속성 비트를 0으로 채우는 도구가 많아 알파로 쓰면 이미지 전체가 투명해진다).
#include "Graphics/Codecs/CodecSupport.h"

#include <algorithm>
#include <array>

namespace sr::codec
{
namespace
{
using detail::ByteReader;
using detail::fail;

// 한 픽셀의 디스크 표현을 RGBA로 바꾼다. 팔레트 항목과 트루컬러 픽셀이 같은 규칙을 쓴다.
[[nodiscard]] std::array<std::uint8_t, 4> readColor(ByteReader &reader, int bits, bool gray) noexcept
{
    switch (bits)
    {
    case 8:
    {
        const auto value = reader.u8();
        return {value, value, value, 255};
    }
    case 15:
    case 16:
    {
        if (gray)
        {
            const auto value = reader.u8();
            return {value, value, value, reader.u8()};
        }
        // 5비트를 v * 255 / 31(버림)로 늘린다. 비트 반복과 최대 1 차이지만 stb·기존 텍스처와 같은 값이다.
        const std::uint16_t pixel = reader.u16le();
        const auto to8 = [](int value) { return static_cast<std::uint8_t>(value * 255 / 31); };
        return {to8((pixel >> 10) & 31), to8((pixel >> 5) & 31), to8(pixel & 31), 255};
    }
    case 24:
    {
        const auto b = reader.u8(), g = reader.u8(), r = reader.u8();
        return {r, g, b, 255};
    }
    case 32:
    {
        const auto b = reader.u8(), g = reader.u8(), r = reader.u8();
        return {r, g, b, reader.u8()};
    }
    default:
        return {0, 0, 0, 255};
    }
}
} // namespace

std::expected<DecodedImage, std::string> DecodeTga(std::span<const std::uint8_t> bytes)
{
    ByteReader reader(bytes);
    const int idLength = reader.u8();
    const int colorMapType = reader.u8();
    int imageType = reader.u8();
    const int colorMapFirst = reader.u16le();
    const int colorMapLength = reader.u16le();
    const int colorMapBits = reader.u8();
    reader.skip(4); // x/y 원점: 화면 배치용이라 픽셀 순서와 무관하다
    const int width = reader.u16le();
    const int height = reader.u16le();
    const int pixelBits = reader.u8();
    const int descriptor = reader.u8();
    if (!reader.ok())
        return fail("tga", "truncated header");

    const bool rle = imageType >= 8;
    if (rle)
        imageType -= 8;
    const bool indexed = imageType == 1;
    const bool gray = imageType == 3;
    if (imageType < 1 || imageType > 3 || colorMapType > 1 || (indexed && colorMapType != 1))
        return fail("tga", "unsupported image type");
    if (indexed && pixelBits != 8 && pixelBits != 16)
        return fail("tga", "unsupported index size");
    if (!indexed &&
        !(pixelBits == 8 || pixelBits == 15 || pixelBits == 16 || pixelBits == 24 || pixelBits == 32))
        return fail("tga", "unsupported pixel size");
    if (gray && pixelBits != 8 && pixelBits != 16)
        return fail("tga", "unsupported gray pixel size");
    if (!gray && !indexed && pixelBits == 8)
        return fail("tga", "8-bit truecolor");

    auto image = detail::allocateImage("tga", width, height);
    if (!image)
        return std::unexpected(std::move(image.error()));

    reader.skip(static_cast<std::size_t>(idLength));
    std::vector<std::array<std::uint8_t, 4>> palette;
    if (colorMapType == 1)
    {
        if (colorMapBits != 15 && colorMapBits != 16 && colorMapBits != 24 && colorMapBits != 32)
            return fail("tga", "unsupported palette entry size");
        // 팔레트가 없는 형식이라도 색 맵이 있으면 건너뛰어야 픽셀 데이터에 닿는다.
        palette.resize(static_cast<std::size_t>(colorMapLength));
        for (auto &entry : palette)
            entry = readColor(reader, colorMapBits, false);
        if (indexed && palette.empty())
            return fail("tga", "empty palette");
    }
    if (!reader.ok())
        return fail("tga", "truncated palette");

    const auto readPixel = [&]() -> std::array<std::uint8_t, 4>
    {
        if (!indexed)
            return readColor(reader, pixelBits, gray);
        // 범위 밖 인덱스는 첫 항목으로 둔다(손상 파일에서 범위 밖 읽기를 막는다).
        const int index = (pixelBits == 8 ? reader.u8() : reader.u16le()) - colorMapFirst;
        return index >= 0 && static_cast<std::size_t>(index) < palette.size() ? palette[index] : palette[0];
    };

    // 기술자 bit 5가 0이면 아래 행부터, bit 4가 1이면 오른쪽 열부터 저장된다.
    const bool topToBottom = (descriptor & 0x20) != 0;
    const bool rightToLeft = (descriptor & 0x10) != 0;
    const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    std::size_t written = 0;
    const auto store = [&](const std::array<std::uint8_t, 4> &color)
    {
        const std::size_t fileX = written % static_cast<std::size_t>(width);
        const std::size_t fileY = written / static_cast<std::size_t>(width);
        const std::size_t x = rightToLeft ? width - 1 - fileX : fileX;
        const std::size_t y = topToBottom ? fileY : height - 1 - fileY;
        std::copy(color.begin(), color.end(), image->rgba.data() + (y * width + x) * 4);
        ++written;
    };

    while (written < pixelCount)
    {
        if (!rle)
        {
            store(readPixel());
        }
        else
        {
            const int packet = reader.u8();
            const std::size_t count =
                std::min<std::size_t>(static_cast<std::size_t>(packet & 127) + 1, pixelCount - written);
            if (packet & 128)
            {
                const auto color = readPixel();
                for (std::size_t i = 0; i < count; ++i)
                    store(color);
            }
            else
            {
                for (std::size_t i = 0; i < count; ++i)
                    store(readPixel());
            }
        }
        if (!reader.ok())
            return fail("tga", "truncated pixel data");
    }
    return image;
}
} // namespace sr::codec

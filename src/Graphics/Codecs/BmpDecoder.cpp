// Windows BMP 디코더. 헤더 크기 12(OS/2 core), 40(INFO), 52/56(V2/V3), 108(V4), 124(V5)를 읽는다.
#include "Graphics/Codecs/CodecSupport.h"

#include <algorithm>
#include <array>
#include <bit>

namespace sr::codec
{
namespace
{
using detail::ByteReader;
using detail::fail;

constexpr std::uint32_t biRgb = 0;
constexpr std::uint32_t biBitfields = 3;
constexpr std::uint32_t biAlphaBitfields = 6;

// 마스크 하나를 8비트 채널로 꺼낸다. 8비트보다 넓은 마스크는 상위 8비트만 쓴다.
struct Channel
{
    std::uint32_t mask = 0;
    int shift = 0;
    int bits = 0;

    explicit Channel(std::uint32_t value = 0) noexcept : mask(value)
    {
        if (mask == 0)
            return;
        shift = std::countr_zero(mask);
        bits = static_cast<int>(std::bit_width(mask >> shift));
    }
    [[nodiscard]] std::uint8_t extract(std::uint32_t pixel) const noexcept
    {
        return detail::expandBits((pixel & mask) >> shift, bits);
    }
};
} // namespace

std::expected<DecodedImage, std::string> DecodeBmp(std::span<const std::uint8_t> bytes)
{
    ByteReader reader(bytes);
    if (reader.u8() != 'B' || reader.u8() != 'M')
        return fail("bmp", "bad signature");
    reader.skip(8); // 파일 크기, 예약
    const std::uint32_t dataOffset = reader.u32le();
    const std::uint32_t headerSize = reader.u32le();

    std::int64_t width = 0;
    std::int64_t height = 0;
    int bitsPerPixel = 0;
    std::uint32_t compression = biRgb;
    std::uint32_t colorsUsed = 0;
    std::array<std::uint32_t, 4> masks{}; // R, G, B, A
    if (headerSize == 12)
    {
        width = reader.u16le();
        height = static_cast<std::int16_t>(reader.u16le());
        if (reader.u16le() != 1)
            return fail("bmp", "bad plane count");
        bitsPerPixel = reader.u16le();
    }
    else if (headerSize == 40 || headerSize == 52 || headerSize == 56 || headerSize == 108 ||
             headerSize == 124)
    {
        width = static_cast<std::int32_t>(reader.u32le());
        height = static_cast<std::int32_t>(reader.u32le());
        if (reader.u16le() != 1)
            return fail("bmp", "bad plane count");
        bitsPerPixel = reader.u16le();
        compression = reader.u32le();
        reader.skip(12); // 이미지 크기, 해상도
        colorsUsed = reader.u32le();
        reader.skip(4); // 중요 색 수
        // V2 이상은 마스크가 헤더 안에, INFO(40)는 헤더 바로 뒤에 온다. 순서는 같다.
        const bool masksFollow =
            headerSize == 40 && (compression == biBitfields || compression == biAlphaBitfields);
        const int maskCount = headerSize >= 56   ? 4
                              : headerSize == 52 ? 3
                              : masksFollow      ? (compression == biAlphaBitfields ? 4 : 3)
                                                 : 0;
        for (int i = 0; i < maskCount; ++i)
            masks[i] = reader.u32le();
    }
    else
        return fail("bmp", "unsupported header size");
    if (!reader.ok())
        return fail("bmp", "truncated header");

    if (compression != biRgb && compression != biBitfields && compression != biAlphaBitfields)
        return fail("bmp", "compressed BMP (RLE/JPEG/PNG) not supported");
    if (compression != biRgb && bitsPerPixel != 16 && bitsPerPixel != 32)
        return fail("bmp", "bitfields need 16 or 32 bits per pixel");
    if (width <= 0 || height == 0)
        return fail("bmp", "invalid dimensions");
    // 높이가 음수면 위에서 아래 순서다.
    const bool bottomUp = height > 0;
    height = height < 0 ? -height : height;

    auto image = detail::allocateImage("bmp", width, height);
    if (!image)
        return std::unexpected(std::move(image.error()));

    // BI_RGB 32비트는 BGRA로 보되, 알파가 전부 0인 파일(알파 없는 도구가 0으로 채운 경우)은 불투명으로 본다.
    bool alphaAllZeroMeansOpaque = false;
    if (compression == biRgb)
    {
        if (bitsPerPixel == 16)
            masks = {0x7C00u, 0x03E0u, 0x001Fu, 0u};
        else if (bitsPerPixel == 32)
        {
            masks = {0x00FF0000u, 0x0000FF00u, 0x000000FFu, 0xFF000000u};
            alphaAllZeroMeansOpaque = true;
        }
    }

    std::vector<std::array<std::uint8_t, 4>> palette;
    if (bitsPerPixel <= 8)
    {
        if (bitsPerPixel != 1 && bitsPerPixel != 2 && bitsPerPixel != 4 && bitsPerPixel != 8)
            return fail("bmp", "unsupported bit depth");
        const std::size_t entrySize = headerSize == 12 ? 3 : 4;
        const std::size_t paletteStart = reader.position();
        if (dataOffset < paletteStart)
            return fail("bmp", "bad data offset");
        std::size_t count = colorsUsed != 0 ? colorsUsed : std::size_t{1} << bitsPerPixel;
        count = std::min({count, std::size_t{1} << bitsPerPixel, (dataOffset - paletteStart) / entrySize});
        if (count == 0)
            return fail("bmp", "missing palette");
        // 팔레트 밖 인덱스는 검정이다.
        palette.assign(std::size_t{1} << bitsPerPixel, {0, 0, 0, 255});
        for (std::size_t i = 0; i < count; ++i)
        {
            const auto b = reader.u8(), g = reader.u8(), r = reader.u8();
            if (entrySize == 4)
                reader.skip(1);
            palette[i] = {r, g, b, 255};
        }
    }
    else if (bitsPerPixel != 16 && bitsPerPixel != 24 && bitsPerPixel != 32)
        return fail("bmp", "unsupported bit depth");
    else if (bitsPerPixel != 24 && (masks[0] == 0 || masks[1] == 0 || masks[2] == 0))
        return fail("bmp", "bad channel masks");

    reader.seek(dataOffset);
    const std::size_t rowBytes =
        (static_cast<std::size_t>(width) * static_cast<std::size_t>(bitsPerPixel) + 31) / 32 * 4;
    const Channel red(masks[0]), green(masks[1]), blue(masks[2]), alpha(masks[3]);
    const bool hasAlpha = bitsPerPixel != 24 && masks[3] != 0;
    unsigned alphaUnion = 0;

    for (std::int64_t fileY = 0; fileY < height; ++fileY)
    {
        const auto row = reader.take(rowBytes);
        if (!reader.ok())
            return fail("bmp", "truncated pixel data");
        const std::int64_t y = bottomUp ? height - 1 - fileY : fileY;
        std::uint8_t *out = image->rgba.data() + static_cast<std::size_t>(y * width) * 4;
        for (std::int64_t x = 0; x < width; ++x, out += 4)
        {
            const auto column = static_cast<std::size_t>(x);
            if (bitsPerPixel <= 8)
            {
                const std::size_t bit = column * static_cast<std::size_t>(bitsPerPixel);
                const int shift = 8 - bitsPerPixel - static_cast<int>(bit % 8);
                const auto index = (row[bit / 8] >> shift) & ((1u << bitsPerPixel) - 1);
                std::copy(palette[index].begin(), palette[index].end(), out);
            }
            else if (bitsPerPixel == 24)
            {
                const std::uint8_t *pixel = row.data() + column * 3;
                out[0] = pixel[2];
                out[1] = pixel[1];
                out[2] = pixel[0];
                out[3] = 255;
            }
            else
            {
                const std::uint8_t *pixel = row.data() + column * static_cast<std::size_t>(bitsPerPixel / 8);
                std::uint32_t value = pixel[0] | (std::uint32_t{pixel[1]} << 8);
                if (bitsPerPixel == 32)
                    value |= (std::uint32_t{pixel[2]} << 16) | (std::uint32_t{pixel[3]} << 24);
                out[0] = red.extract(value);
                out[1] = green.extract(value);
                out[2] = blue.extract(value);
                out[3] = hasAlpha ? alpha.extract(value) : std::uint8_t{255};
                alphaUnion |= out[3];
            }
        }
    }
    if (alphaAllZeroMeansOpaque && alphaUnion == 0)
        for (std::size_t i = 3; i < image->rgba.size(); i += 4)
            image->rgba[i] = 255;
    return image;
}
} // namespace sr::codec

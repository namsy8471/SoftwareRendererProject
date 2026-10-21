#include "Graphics/Codecs/ImageCodecs.h"

#include <algorithm>
#include <cctype>

namespace sr::codec
{
namespace
{
[[nodiscard]] bool startsWith(std::span<const std::uint8_t> bytes,
                              std::initializer_list<std::uint8_t> prefix) noexcept
{
    return bytes.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), bytes.begin());
}

[[nodiscard]] bool equalsIgnoreCase(std::string_view left, std::string_view right) noexcept
{
    return left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin(),
                                                     [](char a, char b) {
                                                         return std::tolower(static_cast<unsigned char>(a)) ==
                                                                std::tolower(static_cast<unsigned char>(b));
                                                     });
}
} // namespace

ImageFormat DetectImageFormat(std::span<const std::uint8_t> bytes, std::string_view extension) noexcept
{
    if (startsWith(bytes, {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'}))
        return ImageFormat::Png;
    if (startsWith(bytes, {0xFF, 0xD8, 0xFF}))
        return ImageFormat::Jpeg;
    if (startsWith(bytes, {'D', 'D', 'S', ' '}))
        return ImageFormat::Dds;
    if (startsWith(bytes, {'B', 'M'}))
        return ImageFormat::Bmp;
    // TGA에는 시그니처가 없다. 확장자를 믿되, 헤더 18바이트도 없으면 TGA가 아니다.
    if (bytes.size() >= 18 && (equalsIgnoreCase(extension, ".tga") || equalsIgnoreCase(extension, "tga")))
        return ImageFormat::Tga;
    return ImageFormat::Unknown;
}

std::expected<DecodedImage, std::string> DecodeImage(std::span<const std::uint8_t> bytes,
                                                     std::string_view extension)
{
    switch (DetectImageFormat(bytes, extension))
    {
    case ImageFormat::Png:
        return DecodePng(bytes);
    case ImageFormat::Jpeg:
        return DecodeJpeg(bytes);
    case ImageFormat::Bmp:
        return DecodeBmp(bytes);
    case ImageFormat::Tga:
        return DecodeTga(bytes);
    case ImageFormat::Dds:
        return DecodeDds(bytes);
    case ImageFormat::Unknown:
        break;
    }
    return std::unexpected(std::string("unknown image format"));
}
} // namespace sr::codec

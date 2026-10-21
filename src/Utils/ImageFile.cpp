#include "Utils/ImageFile.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>

#include "Graphics/Codecs/ImageCodecs.h"

namespace sr::image
{
namespace
{
[[nodiscard]] int channel(std::uint32_t pixel, int shift) noexcept
{
    return static_cast<int>((pixel >> shift) & 0xFFu);
}
} // namespace

Image CopySurface(ConstPixelSurfaceView surface)
{
    Image image{surface.width, surface.height, {}};
    if (surface.width <= 0 || surface.height <= 0)
        return image;
    image.pixels.resize(static_cast<std::size_t>(surface.width) * surface.height);
    for (int y = 0; y < surface.height; ++y)
    {
        const auto row = surface.pixels.subspan(static_cast<std::size_t>(y) * surface.stridePixels,
                                                static_cast<std::size_t>(surface.width));
        std::ranges::copy(row, image.pixels.begin() + static_cast<std::ptrdiff_t>(y) * surface.width);
    }
    return image;
}

std::expected<void, std::string> WritePng(const std::filesystem::path &path, const Image &image)
{
    if (image.width <= 0 || image.height <= 0 ||
        image.pixels.size() != static_cast<std::size_t>(image.width) * image.height)
        return std::unexpected("invalid image dimensions");
    std::vector<unsigned char> rgb(image.pixels.size() * 3);
    for (std::size_t index = 0; index < image.pixels.size(); ++index)
    {
        rgb[index * 3 + 0] = static_cast<unsigned char>(channel(image.pixels[index], 16));
        rgb[index * 3 + 1] = static_cast<unsigned char>(channel(image.pixels[index], 8));
        rgb[index * 3 + 2] = static_cast<unsigned char>(channel(image.pixels[index], 0));
    }
    std::error_code error;
    if (path.has_parent_path())
        std::filesystem::create_directories(path.parent_path(), error);
    const auto png = sr::codec::EncodePngRgb(image.width, image.height, rgb);
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char *>(png.data()), static_cast<std::streamsize>(png.size()));
    if (!file)
        return std::unexpected("failed to write PNG: " + path.string());
    return {};
}

std::expected<Image, std::string> ReadPng(const std::filesystem::path &path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return std::unexpected("failed to read PNG: " + path.string());
    const std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    const auto decoded = sr::codec::DecodePng(bytes);
    if (!decoded)
        return std::unexpected("failed to read PNG: " + path.string() + " (" + decoded.error() + ")");
    // 알파는 버린다. 골든 비교는 RGB만 본다.
    Image image{decoded->width, decoded->height, std::vector<std::uint32_t>(static_cast<std::size_t>(decoded->width) * decoded->height)};
    for (std::size_t index = 0; index < image.pixels.size(); ++index)
    {
        const std::uint8_t *rgba = decoded->rgba.data() + index * 4;
        image.pixels[index] = (static_cast<std::uint32_t>(rgba[0]) << 16) |
                              (static_cast<std::uint32_t>(rgba[1]) << 8) | static_cast<std::uint32_t>(rgba[2]);
    }
    return image;
}

std::uint64_t HashPixels(const Image &image) noexcept
{
    std::uint64_t hash = 14695981039346656037ull;
    for (const auto pixel : image.pixels)
        hash = (hash ^ pixel) * 1099511628211ull;
    return hash;
}

ImageDiff Compare(const Image &expected, const Image &actual, int channelTolerance)
{
    ImageDiff diff;
    if (expected.width != actual.width || expected.height != actual.height ||
        expected.pixels.size() != actual.pixels.size())
    {
        diff.sizeMismatch = true;
        diff.differentRatio = 1.0;
        return diff;
    }
    diff.visualization = {expected.width, expected.height, std::vector<std::uint32_t>(expected.pixels.size())};
    for (std::size_t index = 0; index < expected.pixels.size(); ++index)
    {
        int delta = 0;
        for (const int shift : {0, 8, 16})
            delta = std::max(delta, std::abs(channel(expected.pixels[index], shift) -
                                             channel(actual.pixels[index], shift)));
        diff.maxChannelDelta = std::max(diff.maxChannelDelta, delta);
        if (delta > channelTolerance)
        {
            ++diff.differentPixels;
            diff.visualization.pixels[index] = 0x00FF0000u;
        }
        else
        {
            // 위치를 알아볼 수 있도록 기대 이미지를 1/4 밝기로 남긴다.
            diff.visualization.pixels[index] = (expected.pixels[index] >> 2) & 0x003F3F3Fu;
        }
    }
    diff.differentRatio = expected.pixels.empty()
                              ? 0.0
                              : static_cast<double>(diff.differentPixels) / static_cast<double>(expected.pixels.size());
    return diff;
}
} // namespace sr::image

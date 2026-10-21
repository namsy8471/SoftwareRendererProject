// PNG(ISO/IEC 15948) 디코더. 청크 CRC는 검사하지 않는다(손상은 zlib Adler-32와 크기 검사가 잡는다).
// 감마·색 공간 청크는 무시하고 저장된 값을 그대로 RGBA8로 옮긴다.
#include "Graphics/Codecs/CodecSupport.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <utility>

namespace sr::codec
{
namespace
{
using detail::ByteReader;
using detail::fail;

constexpr std::array<std::uint8_t, 8> signature{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};

struct Header
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    int bitDepth = 0;
    int colorType = 0;
    bool interlaced = false;

    [[nodiscard]] int channels() const noexcept
    {
        switch (colorType)
        {
        case 0:
            return 1; // 회색
        case 2:
            return 3; // RGB
        case 3:
            return 1; // 팔레트 인덱스
        case 4:
            return 2; // 회색 + 알파
        case 6:
            return 4; // RGBA
        default:
            return 0;
        }
    }
    [[nodiscard]] int bitsPerPixel() const noexcept { return channels() * bitDepth; }
    // 필터가 왼쪽 이웃으로 쓰는 거리(최소 1바이트).
    [[nodiscard]] std::size_t filterStride() const noexcept
    {
        return static_cast<std::size_t>(std::max(1, bitsPerPixel() / 8));
    }
    [[nodiscard]] std::size_t rowBytes(std::uint32_t pixels) const noexcept
    {
        return (static_cast<std::size_t>(pixels) * static_cast<std::size_t>(bitsPerPixel()) + 7) / 8;
    }
};

[[nodiscard]] bool validDepth(int colorType, int depth) noexcept
{
    switch (colorType)
    {
    case 0:
        return depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16;
    case 3:
        return depth == 1 || depth == 2 || depth == 4 || depth == 8;
    case 2:
    case 4:
    case 6:
        return depth == 8 || depth == 16;
    default:
        return false;
    }
}

[[nodiscard]] std::uint8_t paeth(int a, int b, int c) noexcept
{
    const int p = a + b - c;
    const int pa = std::abs(p - a);
    const int pb = std::abs(p - b);
    const int pc = std::abs(p - c);
    if (pa <= pb && pa <= pc)
        return static_cast<std::uint8_t>(a);
    return static_cast<std::uint8_t>(pb <= pc ? b : c);
}

// 한 패스(인터레이스가 아니면 전체 이미지)의 필터를 제자리에서 푼다. data는 [필터 바이트 + 행] × rows.
[[nodiscard]] std::expected<void, std::string> unfilter(std::span<std::uint8_t> data, std::size_t rowBytes,
                                                        std::uint32_t rows, std::size_t stride)
{
    std::uint8_t *previous = nullptr;
    for (std::uint32_t y = 0; y < rows; ++y)
    {
        std::uint8_t *line = data.data() + static_cast<std::size_t>(y) * (rowBytes + 1);
        const std::uint8_t filter = line[0];
        std::uint8_t *row = line + 1;
        switch (filter)
        {
        case 0:
            break;
        case 1:
            for (std::size_t i = stride; i < rowBytes; ++i)
                row[i] = static_cast<std::uint8_t>(row[i] + row[i - stride]);
            break;
        case 2:
            if (previous)
                for (std::size_t i = 0; i < rowBytes; ++i)
                    row[i] = static_cast<std::uint8_t>(row[i] + previous[i]);
            break;
        case 3:
            for (std::size_t i = 0; i < rowBytes; ++i)
            {
                const int left = i >= stride ? row[i - stride] : 0;
                const int up = previous ? previous[i] : 0;
                row[i] = static_cast<std::uint8_t>(row[i] + ((left + up) >> 1));
            }
            break;
        case 4:
            for (std::size_t i = 0; i < rowBytes; ++i)
            {
                const int left = i >= stride ? row[i - stride] : 0;
                const int up = previous ? previous[i] : 0;
                const int upLeft = previous && i >= stride ? previous[i - stride] : 0;
                row[i] = static_cast<std::uint8_t>(row[i] + paeth(left, up, upLeft));
            }
            break;
        default:
            return fail("png", "bad filter type");
        }
        previous = row;
    }
    return {};
}

struct Transparency
{
    bool hasKey = false;
    std::array<std::uint16_t, 3> key{}; // 회색은 key[0]만, RGB는 세 값을 원본 비트 깊이로 비교한다
};

// 필터를 푼 행을 RGBA8로 옮긴다. x0/dx, y0/dy는 Adam7 패스의 시작과 간격이다.
void expandPass(const Header &header, std::span<const std::uint8_t> data, std::uint32_t passWidth,
                std::uint32_t passHeight, std::uint32_t x0, std::uint32_t y0, std::uint32_t dx,
                std::uint32_t dy, std::span<const std::array<std::uint8_t, 4>> palette,
                const Transparency &transparency, DecodedImage &image)
{
    const std::size_t rowBytes = header.rowBytes(passWidth);
    const int depth = header.bitDepth;
    const int channels = header.channels();
    for (std::uint32_t y = 0; y < passHeight; ++y)
    {
        const std::uint8_t *row = data.data() + static_cast<std::size_t>(y) * (rowBytes + 1) + 1;
        // 샘플 단위(채널 하나)를 원본 깊이 그대로 읽는다.
        const auto sample = [&](std::size_t index) -> std::uint32_t
        {
            if (depth == 8)
                return row[index];
            if (depth == 16)
                return (std::uint32_t{row[index * 2]} << 8) | row[index * 2 + 1];
            const std::size_t bit = index * static_cast<std::size_t>(depth);
            const int shift = 8 - depth - static_cast<int>(bit % 8);
            return (row[bit / 8] >> shift) & ((1u << depth) - 1);
        };
        // 16비트는 상위 바이트, 1~4비트는 비트 반복으로 8비트로 만든다.
        const auto to8 = [&](std::uint32_t value) -> std::uint8_t
        { return depth == 16 ? static_cast<std::uint8_t>(value >> 8) : detail::expandBits(value, depth); };

        const std::size_t outputY = y0 + static_cast<std::size_t>(y) * dy;
        for (std::uint32_t x = 0; x < passWidth; ++x)
        {
            const std::size_t outputX = x0 + static_cast<std::size_t>(x) * dx;
            std::uint8_t *out = image.rgba.data() + (outputY * image.width + outputX) * 4;
            const std::size_t first = static_cast<std::size_t>(x) * static_cast<std::size_t>(channels);
            switch (header.colorType)
            {
            case 0:
            {
                const auto gray = sample(first);
                out[0] = out[1] = out[2] = to8(gray);
                out[3] =
                    transparency.hasKey && gray == transparency.key[0] ? std::uint8_t{0} : std::uint8_t{255};
                break;
            }
            case 2:
            {
                const auto r = sample(first), g = sample(first + 1), b = sample(first + 2);
                out[0] = to8(r);
                out[1] = to8(g);
                out[2] = to8(b);
                const bool keyed = transparency.hasKey && r == transparency.key[0] &&
                                   g == transparency.key[1] && b == transparency.key[2];
                out[3] = keyed ? std::uint8_t{0} : std::uint8_t{255};
                break;
            }
            case 3:
            {
                // 팔레트 밖 인덱스는 PNG 규격상 오류지만, 한 픽셀 때문에 텍스처 전체를 버리지 않고 검정으로
                // 둔다.
                const auto index = sample(first);
                const std::array<std::uint8_t, 4> black{0, 0, 0, 255};
                const auto &entry = index < palette.size() ? palette[index] : black;
                std::copy(entry.begin(), entry.end(), out);
                break;
            }
            case 4:
                out[0] = out[1] = out[2] = to8(sample(first));
                out[3] = to8(sample(first + 1));
                break;
            case 6:
                for (int c = 0; c < 4; ++c)
                    out[c] = to8(sample(first + static_cast<std::size_t>(c)));
                break;
            default:
                break;
            }
        }
    }
}
} // namespace

std::expected<DecodedImage, std::string> DecodePng(std::span<const std::uint8_t> bytes)
{
    if (bytes.size() < signature.size() || !std::equal(signature.begin(), signature.end(), bytes.begin()))
        return fail("png", "bad signature");
    ByteReader reader(bytes);
    reader.skip(signature.size());

    Header header;
    bool seenHeader = false;
    bool seenEnd = false;
    std::vector<std::array<std::uint8_t, 4>> palette;
    Transparency transparency;
    std::vector<std::uint8_t> compressed;

    while (!seenEnd)
    {
        const std::uint32_t length = reader.u32be();
        const std::uint32_t type = reader.u32be();
        const auto chunk = reader.take(length);
        reader.skip(4); // CRC
        if (!reader.ok())
            return fail("png", "truncated chunk");
        ByteReader body(chunk);

        constexpr auto tag = [](const char(&name)[5]) constexpr
        {
            return (std::uint32_t(std::uint8_t(name[0])) << 24) |
                   (std::uint32_t(std::uint8_t(name[1])) << 16) |
                   (std::uint32_t(std::uint8_t(name[2])) << 8) | std::uint32_t(std::uint8_t(name[3]));
        };
        if (type == tag("IHDR"))
        {
            if (seenHeader || length != 13)
                return fail("png", "bad IHDR");
            header.width = body.u32be();
            header.height = body.u32be();
            header.bitDepth = body.u8();
            header.colorType = body.u8();
            const int compression = body.u8();
            const int filterMethod = body.u8();
            const int interlace = body.u8();
            if (!validDepth(header.colorType, header.bitDepth))
                return fail("png", "unsupported bit depth/color type");
            if (compression != 0 || filterMethod != 0 || interlace > 1)
                return fail("png", "unsupported compression/filter/interlace method");
            if (header.width == 0 || header.height == 0 || header.width > (1u << 24) ||
                header.height > (1u << 24))
                return fail("png", "invalid dimensions");
            header.interlaced = interlace == 1;
            seenHeader = true;
            continue;
        }
        if (!seenHeader)
            return fail("png", "first chunk is not IHDR");

        if (type == tag("PLTE"))
        {
            if (length % 3 != 0 || length / 3 > 256 || length == 0)
                return fail("png", "bad PLTE");
            palette.resize(length / 3);
            for (auto &entry : palette)
                entry = {body.u8(), body.u8(), body.u8(), 255};
        }
        else if (type == tag("tRNS"))
        {
            if (header.colorType == 3)
            {
                if (palette.empty() || length > palette.size())
                    return fail("png", "bad tRNS");
                for (std::uint32_t i = 0; i < length; ++i)
                    palette[i][3] = body.u8();
            }
            else if (header.colorType == 0 && length == 2)
            {
                transparency.hasKey = true;
                transparency.key[0] = body.u16be();
            }
            else if (header.colorType == 2 && length == 6)
            {
                transparency.hasKey = true;
                for (auto &value : transparency.key)
                    value = body.u16be();
            }
            else
                return fail("png", "bad tRNS");
        }
        else if (type == tag("IDAT"))
        {
            if (compressed.size() + chunk.size() > std::size_t{1} << 31)
                return fail("png", "too much image data");
            compressed.insert(compressed.end(), chunk.begin(), chunk.end());
        }
        else if (type == tag("IEND"))
            seenEnd = true;
        else if ((type >> 29 & 1) == 0)
        {
            // 첫 글자가 대문자인 청크는 필수(critical)다. 모르는 필수 청크(예: Apple CgBI)는 해석할 수 없다.
            return fail("png", "unknown critical chunk");
        }
    }
    if (header.colorType == 3 && palette.empty())
        return fail("png", "missing PLTE");
    if (compressed.empty())
        return fail("png", "missing IDAT");

    auto image = detail::allocateImage("png", header.width, header.height);
    if (!image)
        return std::unexpected(std::move(image.error()));

    // Adam7 패스: (x 시작, y 시작, x 간격, y 간격). 인터레이스가 아니면 (0,0,1,1) 한 패스다.
    struct Pass
    {
        std::uint32_t x0, y0, dx, dy;
    };
    constexpr std::array<Pass, 7> adam7{
        {{0, 0, 8, 8}, {4, 0, 8, 8}, {0, 4, 4, 8}, {2, 0, 4, 4}, {0, 2, 2, 4}, {1, 0, 2, 2}, {0, 1, 1, 2}}};
    constexpr std::array<Pass, 1> progressive{{{0, 0, 1, 1}}};
    const std::span<const Pass> passes = header.interlaced ? std::span<const Pass>(adam7) : progressive;

    const auto passSize = [&](const Pass &pass)
    {
        const std::uint32_t w = header.width > pass.x0 ? (header.width - pass.x0 + pass.dx - 1) / pass.dx : 0;
        const std::uint32_t h =
            header.height > pass.y0 ? (header.height - pass.y0 + pass.dy - 1) / pass.dy : 0;
        return std::pair{w, h};
    };
    std::size_t expected = 0;
    for (const auto &pass : passes)
    {
        const auto [w, h] = passSize(pass);
        if (w != 0 && h != 0)
            expected += (header.rowBytes(w) + 1) * h;
    }

    auto raw = InflateZlib(compressed, expected, expected);
    if (!raw)
        return std::unexpected("png: " + raw.error());
    if (raw->size() != expected)
        return fail("png", "image data size mismatch");

    std::size_t offset = 0;
    for (const auto &pass : passes)
    {
        const auto [w, h] = passSize(pass);
        if (w == 0 || h == 0)
            continue;
        const std::size_t size = (header.rowBytes(w) + 1) * h;
        const auto passData = std::span(*raw).subspan(offset, size);
        if (auto filtered = unfilter(passData, header.rowBytes(w), h, header.filterStride()); !filtered)
            return std::unexpected(std::move(filtered.error()));
        expandPass(header, passData, w, h, pass.x0, pass.y0, pass.dx, pass.dy, palette, transparency, *image);
        offset += size;
    }
    return image;
}
} // namespace sr::codec

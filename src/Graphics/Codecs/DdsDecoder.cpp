// DirectDraw Surface(DDS) 디코더. 첫 번째 면·첫 번째 밉 레벨만 읽는다(큐브맵·배열·볼륨도 첫 장).
// 비압축: 비트 마스크 RGB/RGBA/휘도/알파(8~32비트), DX10 R8G8B8A8·B8G8R8A8·B8G8R8X8.
// 블록 압축: BC1(DXT1), BC2(DXT2/3), BC3(DXT4/5), BC4(ATI1), BC5(ATI2), BC7. sRGB 형식도 값 그대로 읽는다.
// BC1~BC5 보간은 정수 나눗셈(버림), BC7은 BPTC 규격(ARB_texture_compression_bptc)의 6비트 가중치를 쓴다.
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

using Rgba = std::array<std::uint8_t, 4>;
using Block = std::array<Rgba, 16>; // 4×4 텍셀, 행 우선

constexpr std::uint32_t fourCc(const char (&code)[5]) noexcept
{
    return std::uint32_t(std::uint8_t(code[0])) | (std::uint32_t(std::uint8_t(code[1])) << 8) |
           (std::uint32_t(std::uint8_t(code[2])) << 16) | (std::uint32_t(std::uint8_t(code[3])) << 24);
}

enum class Encoding
{
    Bc1,
    Bc2,
    Bc3,
    Bc4,
    Bc5,
    Bc7,
    Masked, // 비트 마스크 비압축
};

// DDS_PIXELFORMAT.dwFlags
constexpr std::uint32_t ddpfAlphaPixels = 0x1;
constexpr std::uint32_t ddpfAlpha = 0x2;
constexpr std::uint32_t ddpfFourCc = 0x4;
constexpr std::uint32_t ddpfRgb = 0x40;
constexpr std::uint32_t ddpfLuminance = 0x20000;

[[nodiscard]] Rgba decode565(std::uint16_t value) noexcept
{
    return {detail::expandBits(value >> 11, 5), detail::expandBits((value >> 5) & 63, 6),
            detail::expandBits(value & 31, 5), 255};
}

// BC1 색 블록(8바이트). fourColor면 BC2/BC3처럼 끝점 순서와 무관하게 4색 보간이다.
void decodeColorBlock(const std::uint8_t *src, bool fourColor, Block &block) noexcept
{
    const std::uint16_t c0 = static_cast<std::uint16_t>(src[0] | (src[1] << 8));
    const std::uint16_t c1 = static_cast<std::uint16_t>(src[2] | (src[3] << 8));
    std::array<Rgba, 4> palette{decode565(c0), decode565(c1), Rgba{}, Rgba{}};
    const auto mix = [&](int w0, int w1, int divisor)
    {
        Rgba color{0, 0, 0, 255};
        for (int c = 0; c < 3; ++c)
            color[c] = static_cast<std::uint8_t>((w0 * palette[0][c] + w1 * palette[1][c]) / divisor);
        return color;
    };
    if (c0 > c1 || fourColor)
    {
        palette[2] = mix(2, 1, 3);
        palette[3] = mix(1, 2, 3);
    }
    else
    {
        // 3색 + 투명 검정. 투명 텍셀을 쓰는 BC1(DXT1a)과 불투명 BC1을 같은 규칙으로 읽는다.
        palette[2] = mix(1, 1, 2);
        palette[3] = {0, 0, 0, 0};
    }
    for (int i = 0; i < 16; ++i)
        block[i] = palette[(src[4 + i / 4] >> (2 * (i % 4))) & 3];
}

// BC3 알파 / BC4·BC5 채널 블록(8바이트)을 block[i][channel]에 쓴다.
void decodeChannelBlock(const std::uint8_t *src, int channel, Block &block) noexcept
{
    const int a0 = src[0];
    const int a1 = src[1];
    std::array<std::uint8_t, 8> values{static_cast<std::uint8_t>(a0), static_cast<std::uint8_t>(a1)};
    if (a0 > a1)
        for (int i = 1; i < 7; ++i)
            values[i + 1] = static_cast<std::uint8_t>(((7 - i) * a0 + i * a1) / 7);
    else
    {
        for (int i = 1; i < 5; ++i)
            values[i + 1] = static_cast<std::uint8_t>(((5 - i) * a0 + i * a1) / 5);
        values[6] = 0;
        values[7] = 255;
    }
    std::uint64_t bits = 0;
    for (int i = 0; i < 6; ++i)
        bits |= std::uint64_t{src[2 + i]} << (8 * i);
    for (int i = 0; i < 16; ++i)
        block[i][channel] = values[(bits >> (3 * i)) & 7];
}

// ---- BC7 (BPTC) ----
struct Bc7Mode
{
    int subsets, partitionBits, rotationBits, indexSelectionBits, colorBits, alphaBits;
    int endpointPBits, sharedPBits, indexBits, secondaryIndexBits;
};
constexpr std::array<Bc7Mode, 8> bc7Modes{{{3, 4, 0, 0, 4, 0, 1, 0, 3, 0},
                                           {2, 6, 0, 0, 6, 0, 0, 1, 3, 0},
                                           {3, 6, 0, 0, 5, 0, 0, 0, 2, 0},
                                           {2, 6, 0, 0, 7, 0, 1, 0, 2, 0},
                                           {1, 0, 2, 1, 5, 6, 0, 0, 2, 3},
                                           {1, 0, 2, 0, 7, 8, 0, 0, 2, 2},
                                           {1, 0, 0, 0, 7, 7, 1, 0, 4, 0},
                                           {2, 6, 0, 0, 5, 5, 1, 0, 2, 0}}};

// 규격 표 P2(2분할, 텍셀당 1비트)와 P3(3분할, 텍셀당 2비트). 비트 i가 텍셀 i의 분할 번호다.
constexpr std::array<std::uint16_t, 64> bc7Partition2{
    0xcccc, 0x8888, 0xeeee, 0xecc8, 0xc880, 0xfeec, 0xfec8, 0xec80, 0xc800, 0xffec, 0xfe80, 0xe800, 0xffe8,
    0xff00, 0xfff0, 0xf000, 0xf710, 0x008e, 0x7100, 0x08ce, 0x008c, 0x7310, 0x3100, 0x8cce, 0x088c, 0x3110,
    0x6666, 0x366c, 0x17e8, 0x0ff0, 0x718e, 0x399c, 0xaaaa, 0xf0f0, 0x5a5a, 0x33cc, 0x3c3c, 0x55aa, 0x9696,
    0xa55a, 0x73ce, 0x13c8, 0x324c, 0x3bdc, 0x6996, 0xc33c, 0x9966, 0x0660, 0x0272, 0x04e4, 0x4e40, 0x2720,
    0xc936, 0x936c, 0x39c6, 0x639c, 0x9336, 0x9cc6, 0x817e, 0xe718, 0xccf0, 0x0fcc, 0x7744, 0xee22};
constexpr std::array<std::uint32_t, 64> bc7Partition3{
    0xaa685050, 0x6a5a5040, 0x5a5a4200, 0x5450a0a8, 0xa5a50000, 0xa0a05050, 0x5555a0a0, 0x5a5a5050,
    0xaa550000, 0xaa555500, 0xaaaa5500, 0x90909090, 0x94949494, 0xa4a4a4a4, 0xa9a59450, 0x2a0a4250,
    0xa5945040, 0x0a425054, 0xa5a5a500, 0x55a0a0a0, 0xa8a85454, 0x6a6a4040, 0xa4a45000, 0x1a1a0500,
    0x0050a4a4, 0xaaa59090, 0x14696914, 0x69691400, 0xa08585a0, 0xaa821414, 0x50a4a450, 0x6a5a0200,
    0xa9a58000, 0x5090a0a8, 0xa8a09050, 0x24242424, 0x00aa5500, 0x24924924, 0x24499224, 0x50a50a50,
    0x500aa550, 0xaaaa4444, 0x66660000, 0xa5a0a5a0, 0x50a050a0, 0x69286928, 0x44aaaa44, 0x66666600,
    0xaa444444, 0x54a854a8, 0x95809580, 0x96969600, 0xa85454a8, 0x80959580, 0xaa141414, 0x96960000,
    0xaaaa1414, 0xa05050a0, 0xa0a5a5a0, 0x96000000, 0x40804080, 0xa9a8a9a8, 0xaaaaaa44, 0x2a4a5254};
// 고정(anchor) 텍셀: 인덱스 최상위 비트가 생략된 텍셀. 분할 0은 항상 텍셀 0이다.
constexpr std::array<std::uint8_t, 64> bc7Anchor2{
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 2,  8, 2, 2, 8,
    8,  15, 2,  8,  2,  2,  8,  8,  2,  2,  15, 15, 6,  8,  2,  8,  15, 15, 2, 8, 2, 2,
    2,  15, 15, 6,  6,  2,  6,  8,  15, 15, 2,  2,  15, 15, 15, 15, 15, 2,  2, 15};
constexpr std::array<std::uint8_t, 64> bc7Anchor3a{
    3,  3,  15, 15, 8, 3,  15, 15, 8,  8,  6, 6,  6, 5,  3, 3,  3,  3,  8,  15, 3,  3,
    6,  10, 5,  8,  8, 6,  8,  5,  15, 15, 8, 15, 3, 5,  6, 10, 8,  15, 15, 3,  15, 5,
    15, 15, 15, 15, 3, 15, 5,  5,  5,  8,  5, 10, 5, 10, 8, 13, 15, 12, 3,  3};
constexpr std::array<std::uint8_t, 64> bc7Anchor3b{
    15, 8, 8, 3,  15, 15, 3,  8,  15, 15, 15, 15, 15, 15, 15, 8,  15, 8,  15, 3,  15, 8,
    15, 8, 3, 15, 6,  10, 15, 15, 10, 8,  15, 3,  15, 10, 10, 8,  9,  10, 6,  15, 8,  15,
    3,  6, 6, 8,  15, 3,  15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 3,  15, 15, 8};
constexpr std::array<std::uint8_t, 4> bc7Weights2{0, 21, 43, 64};
constexpr std::array<std::uint8_t, 8> bc7Weights3{0, 9, 18, 27, 37, 46, 55, 64};
constexpr std::array<std::uint8_t, 16> bc7Weights4{0,  4,  9,  13, 17, 21, 26, 30,
                                                   34, 38, 43, 47, 51, 55, 60, 64};

[[nodiscard]] const std::uint8_t *bc7Weights(int bits) noexcept
{
    return bits == 2 ? bc7Weights2.data() : bits == 3 ? bc7Weights3.data() : bc7Weights4.data();
}

// 128비트 블록을 LSB부터 읽는다.
class Bc7Bits
{
  public:
    explicit Bc7Bits(const std::uint8_t *src) noexcept
    {
        for (int i = 0; i < 8; ++i)
        {
            m_low |= std::uint64_t{src[i]} << (8 * i);
            m_high |= std::uint64_t{src[8 + i]} << (8 * i);
        }
    }
    [[nodiscard]] int read(int count) noexcept { return readAt(m_position, count); }
    [[nodiscard]] int readAt(int &position, int count) const noexcept
    {
        int value = 0;
        for (int i = 0; i < count; ++i, ++position)
        {
            const std::uint64_t word = position < 64 ? m_low : m_high;
            value |= static_cast<int>((word >> (position & 63)) & 1) << i;
        }
        return value;
    }
    [[nodiscard]] int position() const noexcept { return m_position; }

  private:
    std::uint64_t m_low = 0;
    std::uint64_t m_high = 0;
    int m_position = 0;
};

void decodeBc7Block(const std::uint8_t *src, Block &block) noexcept
{
    // 모드는 첫 1비트의 위치다. 1비트가 없는 예약 모드는 규격대로 모든 채널 0이다.
    if (src[0] == 0)
    {
        block.fill({0, 0, 0, 0});
        return;
    }
    const int modeIndex = std::countr_zero(static_cast<unsigned>(src[0]));
    const Bc7Mode &mode = bc7Modes[static_cast<std::size_t>(modeIndex)];
    Bc7Bits bits(src);
    (void)bits.read(modeIndex + 1);
    const int partition = bits.read(mode.partitionBits);
    const int rotation = bits.read(mode.rotationBits);
    const int indexSelection = bits.read(mode.indexSelectionBits);

    const int endpointCount = mode.subsets * 2;
    std::array<std::array<int, 4>, 6> endpoints{};
    for (int channel = 0; channel < 3; ++channel)
        for (int e = 0; e < endpointCount; ++e)
            endpoints[e][channel] = bits.read(mode.colorBits);
    for (int e = 0; e < endpointCount; ++e)
        endpoints[e][3] = mode.alphaBits ? bits.read(mode.alphaBits) : 255;

    int colorBits = mode.colorBits;
    int alphaBits = mode.alphaBits;
    // p비트는 모든 채널의 최하위 비트로 붙는다(끝점마다 또는 분할마다 하나).
    if (mode.endpointPBits || mode.sharedPBits)
    {
        ++colorBits;
        if (alphaBits)
            ++alphaBits;
        for (int e = 0; e < endpointCount; ++e)
        {
            if (mode.sharedPBits && (e & 1))
                continue;
            const int pBit = bits.read(1);
            const int last = mode.sharedPBits ? e + 1 : e;
            for (int target = e; target <= last; ++target)
                for (int channel = 0; channel < (alphaBits ? 4 : 3); ++channel)
                    endpoints[target][channel] = (endpoints[target][channel] << 1) | pBit;
        }
    }
    const auto expand = [](int value, int count)
    {
        value <<= 8 - count;
        return value | (value >> count);
    };
    for (int e = 0; e < endpointCount; ++e)
    {
        for (int channel = 0; channel < 3; ++channel)
            endpoints[e][channel] = expand(endpoints[e][channel], colorBits);
        if (alphaBits)
            endpoints[e][3] = expand(endpoints[e][3], alphaBits);
    }

    const auto subsetOf = [&](int texel)
    {
        if (mode.subsets == 2)
            return (bc7Partition2[static_cast<std::size_t>(partition)] >> texel) & 1;
        if (mode.subsets == 3)
            return static_cast<int>((bc7Partition3[static_cast<std::size_t>(partition)] >> (2 * texel)) & 3);
        return 0;
    };
    const auto isAnchor = [&](int texel)
    {
        if (texel == 0)
            return true;
        if (mode.subsets == 2)
            return texel == bc7Anchor2[static_cast<std::size_t>(partition)];
        if (mode.subsets == 3)
            return texel == bc7Anchor3a[static_cast<std::size_t>(partition)] ||
                   texel == bc7Anchor3b[static_cast<std::size_t>(partition)];
        return false;
    };

    // 색 인덱스 다음에 (있으면) 두 번째 인덱스가 온다. 고정 텍셀은 분할마다 1비트씩 짧다.
    int colorIndexPosition = bits.position();
    int alphaIndexPosition = colorIndexPosition + 16 * mode.indexBits - mode.subsets;
    const std::uint8_t *colorWeights = bc7Weights(mode.indexBits);
    const std::uint8_t *alphaWeights =
        bc7Weights(mode.secondaryIndexBits ? mode.secondaryIndexBits : mode.indexBits);
    for (int texel = 0; texel < 16; ++texel)
    {
        const int subset = subsetOf(texel);
        const int primary = bits.readAt(colorIndexPosition, mode.indexBits - (isAnchor(texel) ? 1 : 0));
        int colorWeight = colorWeights[primary];
        int alphaWeight = colorWeight;
        if (mode.secondaryIndexBits)
        {
            const int secondary =
                bits.readAt(alphaIndexPosition, mode.secondaryIndexBits - (texel == 0 ? 1 : 0));
            alphaWeight = alphaWeights[secondary];
            // 인덱스 선택 비트가 1이면 색이 두 번째(긴) 인덱스를, 알파가 첫 번째 인덱스를 쓴다.
            if (indexSelection)
                std::swap(colorWeight, alphaWeight);
        }
        const auto &e0 = endpoints[static_cast<std::size_t>(subset * 2)];
        const auto &e1 = endpoints[static_cast<std::size_t>(subset * 2 + 1)];
        Rgba &out = block[static_cast<std::size_t>(texel)];
        for (int channel = 0; channel < 4; ++channel)
        {
            const int weight = channel < 3 ? colorWeight : alphaWeight;
            out[channel] =
                static_cast<std::uint8_t>(((64 - weight) * e0[channel] + weight * e1[channel] + 32) >> 6);
        }
        // 회전: 알파와 지정한 색 채널을 맞바꾼다(1=R, 2=G, 3=B).
        if (rotation != 0)
            std::swap(out[3], out[static_cast<std::size_t>(rotation - 1)]);
    }
}

// 비압축 마스크 형식의 한 채널.
struct MaskChannel
{
    std::uint32_t mask = 0;
    int shift = 0;
    int bits = 0;

    explicit MaskChannel(std::uint32_t value = 0) noexcept : mask(value)
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

struct Layout
{
    Encoding encoding = Encoding::Masked;
    int bitsPerPixel = 0;
    std::uint32_t masks[4]{}; // R, G, B, A
    bool luminance = false;
    bool alphaOnly = false;
};

// DX10 확장 헤더의 DXGI_FORMAT. 지원하는 것만 나열한다.
[[nodiscard]] std::expected<Layout, std::string> layoutFromDxgi(std::uint32_t format)
{
    Layout layout;
    switch (format)
    {
    case 70:
    case 71:
    case 72:
        layout.encoding = Encoding::Bc1;
        return layout;
    case 73:
    case 74:
    case 75:
        layout.encoding = Encoding::Bc2;
        return layout;
    case 76:
    case 77:
    case 78:
        layout.encoding = Encoding::Bc3;
        return layout;
    case 79:
    case 80:
        layout.encoding = Encoding::Bc4;
        return layout;
    case 82:
    case 83:
        layout.encoding = Encoding::Bc5;
        return layout;
    case 97:
    case 98:
    case 99:
        layout.encoding = Encoding::Bc7;
        return layout;
    case 27:
    case 28:
    case 29: // R8G8B8A8
        layout.bitsPerPixel = 32;
        layout.masks[0] = 0x000000FFu;
        layout.masks[1] = 0x0000FF00u;
        layout.masks[2] = 0x00FF0000u;
        layout.masks[3] = 0xFF000000u;
        return layout;
    case 87:
    case 90:
    case 91: // B8G8R8A8
    case 88:
    case 92:
    case 93: // B8G8R8X8
        layout.bitsPerPixel = 32;
        layout.masks[0] = 0x00FF0000u;
        layout.masks[1] = 0x0000FF00u;
        layout.masks[2] = 0x000000FFu;
        layout.masks[3] = (format == 87 || format == 90 || format == 91) ? 0xFF000000u : 0u;
        return layout;
    case 81:
    case 84:
        return fail("dds", "signed BC4/BC5 not supported");
    case 94:
    case 95:
    case 96:
        return fail("dds", "BC6H (HDR) not supported");
    default:
        return fail("dds", "unsupported DXGI format " + std::to_string(format));
    }
}
} // namespace

std::expected<DecodedImage, std::string> DecodeDds(std::span<const std::uint8_t> bytes)
{
    ByteReader reader(bytes);
    if (reader.u32le() != fourCc("DDS "))
        return fail("dds", "bad signature");
    if (reader.u32le() != 124)
        return fail("dds", "bad header size");
    reader.skip(4); // flags
    const std::uint32_t height = reader.u32le();
    const std::uint32_t width = reader.u32le();
    reader.skip(4 + 4 + 4 + 44); // pitch/linear size, depth, mip 수, 예약
    if (reader.u32le() != 32)
        return fail("dds", "bad pixel format size");
    const std::uint32_t pixelFlags = reader.u32le();
    const std::uint32_t code = reader.u32le();
    const std::uint32_t bitCount = reader.u32le();
    std::array<std::uint32_t, 4> masks{};
    for (auto &mask : masks)
        mask = reader.u32le();
    reader.skip(16); // caps
    reader.skip(4);  // 예약
    if (!reader.ok())
        return fail("dds", "truncated header");

    Layout layout;
    if (pixelFlags & ddpfFourCc)
    {
        if (code == fourCc("DX10"))
        {
            const std::uint32_t format = reader.u32le();
            reader.skip(16); // 차원, misc, 배열 크기, misc2
            if (!reader.ok())
                return fail("dds", "truncated DX10 header");
            auto dxgi = layoutFromDxgi(format);
            if (!dxgi)
                return std::unexpected(std::move(dxgi.error()));
            layout = *dxgi;
        }
        else if (code == fourCc("DXT1"))
            layout.encoding = Encoding::Bc1;
        else if (code == fourCc("DXT2") || code == fourCc("DXT3"))
            layout.encoding = Encoding::Bc2;
        else if (code == fourCc("DXT4") || code == fourCc("DXT5"))
            layout.encoding = Encoding::Bc3;
        else if (code == fourCc("ATI1") || code == fourCc("BC4U"))
            layout.encoding = Encoding::Bc4;
        else if (code == fourCc("ATI2") || code == fourCc("BC5U"))
            layout.encoding = Encoding::Bc5;
        else
            return fail("dds", "unsupported FourCC");
    }
    else if (pixelFlags & (ddpfRgb | ddpfLuminance | ddpfAlpha))
    {
        if (bitCount != 8 && bitCount != 16 && bitCount != 24 && bitCount != 32)
            return fail("dds", "unsupported bit count");
        layout.bitsPerPixel = static_cast<int>(bitCount);
        // 픽셀 크기를 넘는 마스크 비트는 버린다. 휘도 형식은 마스크를 엉뚱하게 쓰는 도구가 있어(예: L8에 0xFF000000)
        // 남는 마스크가 없으면 관례 배치(L = 하위 8비트, A = 그다음 8비트)를 쓴다.
        const std::uint32_t limit = bitCount == 32 ? 0xFFFFFFFFu : (1u << bitCount) - 1;
        for (auto &mask : masks)
            mask &= limit;
        if ((pixelFlags & ddpfLuminance) && masks[0] == 0)
            masks[0] = 0xFFu;
        if ((pixelFlags & ddpfLuminance) && (pixelFlags & ddpfAlphaPixels) && masks[3] == 0 && bitCount == 16)
            masks[3] = 0xFF00u;
        layout.luminance = (pixelFlags & ddpfLuminance) != 0;
        layout.alphaOnly = (pixelFlags & (ddpfRgb | ddpfLuminance)) == 0;
        for (int i = 0; i < 3; ++i)
            layout.masks[i] = layout.alphaOnly ? 0 : masks[static_cast<std::size_t>(i)];
        layout.masks[3] = (pixelFlags & (ddpfAlphaPixels | ddpfAlpha)) ? masks[3] : 0;
        if (layout.alphaOnly && layout.masks[3] == 0)
            return fail("dds", "alpha format without alpha mask");
        if (!layout.alphaOnly && layout.masks[0] == 0)
            return fail("dds", "missing color mask");
    }
    else
        return fail("dds", "unsupported pixel format");

    auto image = detail::allocateImage("dds", width, height);
    if (!image)
        return std::unexpected(std::move(image.error()));

    if (layout.encoding == Encoding::Masked)
    {
        const std::size_t pixelBytes = static_cast<std::size_t>(layout.bitsPerPixel / 8);
        const MaskChannel red(layout.masks[0]), green(layout.masks[1]), blue(layout.masks[2]),
            alpha(layout.masks[3]);
        for (std::uint32_t y = 0; y < height; ++y)
        {
            // 행 사이 패딩은 없다고 본다(DDS 규격의 pitch = (width * bpp + 7) / 8).
            const auto row = reader.take(pixelBytes * width);
            if (!reader.ok())
                return fail("dds", "truncated pixel data");
            std::uint8_t *out = image->rgba.data() + static_cast<std::size_t>(y) * width * 4;
            for (std::uint32_t x = 0; x < width; ++x, out += 4)
            {
                std::uint32_t value = 0;
                for (std::size_t b = 0; b < pixelBytes; ++b)
                    value |= std::uint32_t{row[x * pixelBytes + b]} << (8 * b);
                if (layout.luminance)
                    out[0] = out[1] = out[2] = red.extract(value);
                else if (layout.alphaOnly)
                    out[0] = out[1] = out[2] = 0;
                else
                {
                    out[0] = red.extract(value);
                    out[1] = green.extract(value);
                    out[2] = blue.extract(value);
                }
                out[3] = layout.masks[3] ? alpha.extract(value) : std::uint8_t{255};
            }
        }
        return image;
    }

    const bool eightByteBlocks = layout.encoding == Encoding::Bc1 || layout.encoding == Encoding::Bc4;
    const std::size_t blockBytes = eightByteBlocks ? 8 : 16;
    const std::uint32_t blocksWide = (width + 3) / 4;
    const std::uint32_t blocksHigh = (height + 3) / 4;
    const auto data = reader.take(blockBytes * blocksWide * blocksHigh);
    if (!reader.ok())
        return fail("dds", "truncated block data");

    Block block{};
    for (std::uint32_t by = 0; by < blocksHigh; ++by)
        for (std::uint32_t bx = 0; bx < blocksWide; ++bx)
        {
            const std::uint8_t *src =
                data.data() + (static_cast<std::size_t>(by) * blocksWide + bx) * blockBytes;
            switch (layout.encoding)
            {
            case Encoding::Bc1:
                decodeColorBlock(src, false, block);
                break;
            case Encoding::Bc2:
                decodeColorBlock(src + 8, true, block);
                for (int i = 0; i < 16; ++i)
                {
                    const int nibble = (src[i / 2] >> (4 * (i % 2))) & 15;
                    block[i][3] = static_cast<std::uint8_t>(nibble * 17);
                }
                break;
            case Encoding::Bc3:
                decodeColorBlock(src + 8, true, block);
                decodeChannelBlock(src, 3, block);
                break;
            case Encoding::Bc4:
                decodeChannelBlock(src, 0, block);
                for (auto &texel : block)
                    texel = {texel[0], texel[0], texel[0], 255};
                break;
            case Encoding::Bc5:
                // 법선 맵용 두 채널(R, G). B는 0으로 둔다(Z 복원은 셰이더 몫이다).
                decodeChannelBlock(src, 0, block);
                decodeChannelBlock(src + 8, 1, block);
                for (auto &texel : block)
                {
                    texel[2] = 0;
                    texel[3] = 255;
                }
                break;
            case Encoding::Bc7:
                decodeBc7Block(src, block);
                break;
            case Encoding::Masked:
                break;
            }
            // 가장자리 블록은 이미지 밖 텍셀을 버린다.
            for (std::uint32_t ty = 0; ty < 4 && by * 4 + ty < height; ++ty)
                for (std::uint32_t tx = 0; tx < 4 && bx * 4 + tx < width; ++tx)
                {
                    const std::size_t index =
                        (static_cast<std::size_t>(by * 4 + ty) * width + bx * 4 + tx) * 4;
                    std::copy_n(block[ty * 4 + tx].begin(), 4, image->rgba.data() + index);
                }
        }
    return image;
}
} // namespace sr::codec

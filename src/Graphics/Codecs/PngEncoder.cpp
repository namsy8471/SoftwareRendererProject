// 8-bit RGB PNG 인코더. 행마다 다섯 필터 중 절댓값 합이 가장 작은 것을 고르고(libpng 기본 휴리스틱),
// 해시 체인 LZ77 + 고정 허프만 DEFLATE 블록 하나로 압축한다. 골든 이미지처럼 작은 이미지를 위한 단순한
// 구현이다.
#include "Graphics/Codecs/CodecSupport.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <string_view>

namespace sr::codec
{
namespace
{
// 비트를 LSB부터 채운다(DEFLATE 순서).
class BitWriter
{
  public:
    explicit BitWriter(std::vector<std::uint8_t> &out) noexcept : m_out(out) {}

    void write(std::uint32_t value, int bits)
    {
        m_buffer |= static_cast<std::uint64_t>(value) << m_count;
        m_count += bits;
        while (m_count >= 8)
        {
            m_out.push_back(static_cast<std::uint8_t>(m_buffer));
            m_buffer >>= 8;
            m_count -= 8;
        }
    }
    // 허프만 코드는 MSB부터 정의되므로 뒤집어 쓴다.
    void writeCode(std::uint32_t code, int bits)
    {
        std::uint32_t reversed = 0;
        for (int i = 0; i < bits; ++i)
            reversed |= ((code >> i) & 1u) << (bits - 1 - i);
        write(reversed, bits);
    }
    void flush()
    {
        if (m_count > 0)
            write(0, 8 - m_count);
    }

  private:
    std::vector<std::uint8_t> &m_out;
    std::uint64_t m_buffer = 0;
    int m_count = 0;
};

constexpr std::array<std::uint16_t, 29> lengthBase{3,  4,  5,  6,   7,   8,   9,   10,  11, 13,
                                                   15, 17, 19, 23,  27,  31,  35,  43,  51, 59,
                                                   67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::array<std::uint8_t, 29> lengthExtra{0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                                   2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::array<std::uint16_t, 30> distanceBase{
    1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::array<std::uint8_t, 30> distanceExtra{0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                                     6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

void writeLiteral(BitWriter &writer, int symbol)
{
    if (symbol < 144)
        writer.writeCode(0x30u + symbol, 8);
    else if (symbol < 256)
        writer.writeCode(0x190u + (symbol - 144), 9);
    else if (symbol < 280)
        writer.writeCode(static_cast<std::uint32_t>(symbol - 256), 7);
    else
        writer.writeCode(0xC0u + (symbol - 280), 8);
}

void writeMatch(BitWriter &writer, int length, int distance)
{
    int lengthSymbol = 28;
    while (lengthSymbol > 0 && lengthBase[lengthSymbol] > length)
        --lengthSymbol;
    writeLiteral(writer, 257 + lengthSymbol);
    writer.write(static_cast<std::uint32_t>(length - lengthBase[lengthSymbol]), lengthExtra[lengthSymbol]);
    int distanceSymbol = 29;
    while (distanceSymbol > 0 && distanceBase[distanceSymbol] > distance)
        --distanceSymbol;
    writer.writeCode(static_cast<std::uint32_t>(distanceSymbol), 5);
    writer.write(static_cast<std::uint32_t>(distance - distanceBase[distanceSymbol]),
                 distanceExtra[distanceSymbol]);
}

[[nodiscard]] std::vector<std::uint8_t> deflateFixed(std::span<const std::uint8_t> data)
{
    constexpr int windowSize = 32768;
    constexpr int hashBits = 15;
    constexpr int maxChain = 64;
    constexpr int minMatch = 3;
    constexpr int maxMatch = 258;

    std::vector<std::uint8_t> out;
    out.reserve(data.size() / 2 + 64);
    BitWriter writer(out);
    writer.write(1, 1); // 마지막 블록
    writer.write(1, 2); // 고정 허프만

    std::vector<int> head(1 << hashBits, -1);
    std::vector<int> previous(windowSize, -1);
    const auto hash = [&](std::size_t at)
    {
        const std::uint32_t value =
            data[at] | (std::uint32_t{data[at + 1]} << 8) | (std::uint32_t{data[at + 2]} << 16);
        return static_cast<int>((value * 2654435761u) >> (32 - hashBits));
    };
    const auto insert = [&](std::size_t at)
    {
        if (at + minMatch > data.size())
            return;
        const int key = hash(at);
        previous[at % windowSize] = head[key];
        head[key] = static_cast<int>(at);
    };

    std::size_t position = 0;
    while (position < data.size())
    {
        int bestLength = 0;
        int bestDistance = 0;
        if (position + minMatch <= data.size())
        {
            int candidate = head[hash(position)];
            const std::size_t limit = std::min<std::size_t>(maxMatch, data.size() - position);
            for (int chain = 0; candidate >= 0 && chain < maxChain; ++chain)
            {
                const auto distance = static_cast<int>(position - static_cast<std::size_t>(candidate));
                if (distance > windowSize - 1)
                    break;
                std::size_t length = 0;
                while (length < limit &&
                       data[static_cast<std::size_t>(candidate) + length] == data[position + length])
                    ++length;
                if (static_cast<int>(length) > bestLength)
                {
                    bestLength = static_cast<int>(length);
                    bestDistance = distance;
                    if (length == limit)
                        break;
                }
                const int next = previous[static_cast<std::size_t>(candidate) % windowSize];
                if (next >= candidate)
                    break; // 창을 한 바퀴 돌아 덮어쓴 항목
                candidate = next;
            }
        }
        if (bestLength >= minMatch)
        {
            writeMatch(writer, bestLength, bestDistance);
            for (int i = 0; i < bestLength; ++i)
                insert(position + static_cast<std::size_t>(i));
            position += static_cast<std::size_t>(bestLength);
        }
        else
        {
            writeLiteral(writer, data[position]);
            insert(position);
            ++position;
        }
    }
    writeLiteral(writer, 256);
    writer.flush();
    return out;
}

[[nodiscard]] std::uint32_t crc32(std::span<const std::uint8_t> data, std::uint32_t crc = 0) noexcept
{
    static const auto table = []
    {
        std::array<std::uint32_t, 256> values{};
        for (std::uint32_t n = 0; n < 256; ++n)
        {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            values[n] = c;
        }
        return values;
    }();
    crc = ~crc;
    for (const auto byte : data)
        crc = table[(crc ^ byte) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

[[nodiscard]] std::uint32_t adler32(std::span<const std::uint8_t> data) noexcept
{
    std::uint32_t a = 1, b = 0;
    for (const auto byte : data)
    {
        a = (a + byte) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

void appendU32(std::vector<std::uint8_t> &out, std::uint32_t value)
{
    for (int shift = 24; shift >= 0; shift -= 8)
        out.push_back(static_cast<std::uint8_t>(value >> shift));
}

void appendChunk(std::vector<std::uint8_t> &out, std::string_view type, std::span<const std::uint8_t> body)
{
    appendU32(out, static_cast<std::uint32_t>(body.size()));
    const std::size_t typeStart = out.size();
    out.insert(out.end(), type.begin(), type.end());
    out.insert(out.end(), body.begin(), body.end());
    appendU32(out, crc32(std::span(out).subspan(typeStart)));
}

[[nodiscard]] std::uint8_t paeth(int a, int b, int c) noexcept
{
    const int p = a + b - c;
    const int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    if (pa <= pb && pa <= pc)
        return static_cast<std::uint8_t>(a);
    return static_cast<std::uint8_t>(pb <= pc ? b : c);
}
} // namespace

std::vector<std::uint8_t> EncodePngRgb(int width, int height, std::span<const std::uint8_t> rgb)
{
    if (width <= 0 || height <= 0 || rgb.size() != static_cast<std::size_t>(width) * height * 3)
        return {};
    const std::size_t rowBytes = static_cast<std::size_t>(width) * 3;
    std::vector<std::uint8_t> filtered;
    filtered.reserve((rowBytes + 1) * static_cast<std::size_t>(height));
    std::vector<std::uint8_t> candidate(rowBytes);
    std::vector<std::uint8_t> best(rowBytes);
    for (int y = 0; y < height; ++y)
    {
        const std::uint8_t *row = rgb.data() + static_cast<std::size_t>(y) * rowBytes;
        const std::uint8_t *up = y > 0 ? row - rowBytes : nullptr;
        std::uint64_t bestCost = ~std::uint64_t{0};
        std::uint8_t bestFilter = 0;
        for (std::uint8_t filter = 0; filter < 5; ++filter)
        {
            std::uint64_t cost = 0;
            for (std::size_t i = 0; i < rowBytes; ++i)
            {
                const int left = i >= 3 ? row[i - 3] : 0;
                const int above = up ? up[i] : 0;
                const int upLeft = up && i >= 3 ? up[i - 3] : 0;
                int predicted = 0;
                switch (filter)
                {
                case 1:
                    predicted = left;
                    break;
                case 2:
                    predicted = above;
                    break;
                case 3:
                    predicted = (left + above) >> 1;
                    break;
                case 4:
                    predicted = paeth(left, above, upLeft);
                    break;
                default:
                    break;
                }
                candidate[i] = static_cast<std::uint8_t>(row[i] - predicted);
                // 부호 있는 바이트로 본 절댓값 합: 0 근처 잔차가 많을수록 잘 압축된다.
                cost += static_cast<std::uint64_t>(
                    std::abs(static_cast<int>(static_cast<std::int8_t>(candidate[i]))));
            }
            if (cost < bestCost)
            {
                bestCost = cost;
                bestFilter = filter;
                best.swap(candidate);
            }
        }
        filtered.push_back(bestFilter);
        filtered.insert(filtered.end(), best.begin(), best.end());
    }

    std::vector<std::uint8_t> zlib{0x78, 0x01};
    const auto compressed = deflateFixed(filtered);
    zlib.insert(zlib.end(), compressed.begin(), compressed.end());
    appendU32(zlib, adler32(filtered));

    std::vector<std::uint8_t> png{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::array<std::uint8_t, 13> header{};
    for (int i = 0; i < 4; ++i)
    {
        header[i] = static_cast<std::uint8_t>(static_cast<std::uint32_t>(width) >> (24 - 8 * i));
        header[4 + i] = static_cast<std::uint8_t>(static_cast<std::uint32_t>(height) >> (24 - 8 * i));
    }
    header[8] = 8; // 비트 깊이
    header[9] = 2; // RGB
    appendChunk(png, "IHDR", header);
    appendChunk(png, "IDAT", zlib);
    appendChunk(png, "IEND", {});
    return png;
}
} // namespace sr::codec

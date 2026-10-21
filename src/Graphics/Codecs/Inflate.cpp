// DEFLATE(RFC 1951) 해제와 zlib(RFC 1950) 래퍼. PNG IDAT 전용이라 사전(FDICT)은 지원하지 않는다.
#include "Graphics/Codecs/CodecSupport.h"

#include <algorithm>
#include <array>
#include <utility>

namespace sr::codec
{
namespace
{
using detail::fail;

constexpr int maxCodeBits = 15;
// 이 길이 이하 코드는 표 한 번으로 찾는다. 9비트면 고정 허프만 리터럴(7~9비트)이 전부 들어간다.
constexpr int fastBits = 9;

// DEFLATE는 비트를 LSB부터 채운다. 64비트 버퍼에 미리 채워 두고 필요한 만큼 꺼낸다.
// 입력 끝을 넘으면 0을 채우고 overrun을 센다. 정상 스트림은 끝난 뒤 남은 비트만 버리므로
// 끝 블록을 다 읽기 전에 overrun이 생기면 잘린 입력이다.
class BitReader
{
  public:
    explicit BitReader(std::span<const std::uint8_t> bytes) noexcept : m_bytes(bytes) {}

    void refill() noexcept
    {
        while (m_count <= 56)
        {
            std::uint64_t byte = 0;
            if (m_position < m_bytes.size())
                byte = m_bytes[m_position];
            else
                ++m_overrunBytes;
            ++m_position;
            m_buffer |= byte << m_count;
            m_count += 8;
        }
    }
    [[nodiscard]] std::uint32_t peek(int width) noexcept
    {
        if (m_count < width)
            refill();
        return static_cast<std::uint32_t>(m_buffer & ((std::uint64_t{1} << width) - 1));
    }
    void consume(int width) noexcept
    {
        m_buffer >>= width;
        m_count -= width;
    }
    [[nodiscard]] std::uint32_t bits(int count) noexcept
    {
        if (count == 0)
            return 0;
        const auto value = peek(count);
        consume(count);
        return value;
    }
    void alignToByte() noexcept { consume(m_count % 8); }
    // 버퍼에 남은 비트 중 실제 입력에서 온 것이 모자라면 잘린 스트림이다.
    [[nodiscard]] bool overrun() const noexcept
    {
        return static_cast<std::size_t>(m_overrunBytes) * 8 > static_cast<std::size_t>(m_count);
    }
    // 바이트 정렬 뒤 남은 입력 위치. 저장(stored) 블록과 Adler-32를 읽을 때 쓴다.
    [[nodiscard]] std::size_t bytePosition() const noexcept { return m_position - m_count / 8; }

  private:
    std::span<const std::uint8_t> m_bytes;
    std::size_t m_position = 0;
    std::uint64_t m_buffer = 0;
    int m_count = 0;
    int m_overrunBytes = 0;
};

// 정규(canonical) 허프만 표. fast는 (심볼 << 4) | 길이, 0이면 긴 코드라 느린 경로로 간다.
struct Huffman
{
    std::array<std::uint16_t, 1 << fastBits> fast{};
    std::array<std::uint16_t, maxCodeBits + 1> count{}; // 길이별 코드 수
    std::array<std::uint16_t, 288> symbols{};           // 길이·코드 순으로 정렬된 심볼

    [[nodiscard]] bool build(std::span<const std::uint8_t> lengths) noexcept
    {
        count.fill(0);
        fast.fill(0);
        for (const auto length : lengths)
            ++count[length];
        count[0] = 0;
        // 과다 구독(over-subscribed) 표는 거부한다. 부족한 표는 DEFLATE가 허용한다(코드 1개 등).
        int left = 1;
        for (int length = 1; length <= maxCodeBits; ++length)
        {
            left = (left << 1) - count[length];
            if (left < 0)
                return false;
        }
        std::array<std::uint16_t, maxCodeBits + 2> offsets{};
        for (int length = 1; length <= maxCodeBits; ++length)
            offsets[length + 1] = static_cast<std::uint16_t>(offsets[length] + count[length]);
        for (std::size_t symbol = 0; symbol < lengths.size(); ++symbol)
            if (lengths[symbol] != 0)
                symbols[offsets[lengths[symbol]]++] = static_cast<std::uint16_t>(symbol);

        // 빠른 표: 코드는 MSB부터 정의되지만 스트림은 LSB부터 읽으므로 비트를 뒤집어 넣는다.
        int code = 0;
        int index = 0;
        for (int length = 1; length <= fastBits; ++length)
        {
            for (int i = 0; i < count[length]; ++i, ++code, ++index)
            {
                int reversed = 0;
                for (int bit = 0; bit < length; ++bit)
                    reversed |= ((code >> bit) & 1) << (length - 1 - bit);
                const auto entry = static_cast<std::uint16_t>((symbols[index] << 4) | length);
                for (int fill = reversed; fill < (1 << fastBits); fill += 1 << length)
                    fast[fill] = entry;
            }
            code <<= 1;
        }
        return true;
    }

    // 성공하면 심볼(0 이상), 표에 없는 코드면 -1.
    [[nodiscard]] int decode(BitReader &reader) const noexcept
    {
        const auto entry = fast[reader.peek(fastBits)];
        if (entry != 0)
        {
            reader.consume(entry & 15);
            return entry >> 4;
        }
        // zlib "puff"와 같은 비트 단위 정규 코드 탐색.
        int code = 0;
        int first = 0;
        int index = 0;
        for (int length = 1; length <= maxCodeBits; ++length)
        {
            code |= static_cast<int>(reader.bits(1));
            const int countAtLength = count[length];
            if (code - countAtLength < first)
                return symbols[index + (code - first)];
            index += countAtLength;
            first = (first + countAtLength) << 1;
            code <<= 1;
        }
        return -1;
    }
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

class Inflater
{
  public:
    Inflater(std::span<const std::uint8_t> data, std::size_t expectedSize, std::size_t maxOutput)
        : m_reader(data), m_maxOutput(maxOutput)
    {
        m_output.reserve(std::min(expectedSize, maxOutput));
    }

    [[nodiscard]] std::expected<void, std::string> run()
    {
        bool last = false;
        while (!last)
        {
            last = m_reader.bits(1) != 0;
            const auto type = m_reader.bits(2);
            std::expected<void, std::string> block;
            if (type == 0)
                block = stored();
            else if (type == 1)
                block = fixed();
            else if (type == 2)
                block = dynamic();
            else
                return fail("inflate", "invalid block type");
            if (!block)
                return block;
            if (m_reader.overrun())
                return fail("inflate", "truncated stream");
        }
        return {};
    }

    [[nodiscard]] std::vector<std::uint8_t> takeOutput() noexcept { return std::move(m_output); }
    [[nodiscard]] std::size_t endPosition() noexcept
    {
        m_reader.alignToByte();
        return m_reader.bytePosition();
    }

  private:
    BitReader m_reader;
    std::vector<std::uint8_t> m_output;
    std::size_t m_maxOutput;
    Huffman m_literals;
    Huffman m_distances;

    [[nodiscard]] std::expected<void, std::string> stored()
    {
        m_reader.alignToByte();
        const auto length = m_reader.bits(16);
        const auto inverted = m_reader.bits(16);
        if ((length ^ 0xFFFFu) != inverted)
            return fail("inflate", "stored block length mismatch");
        if (m_output.size() + length > m_maxOutput)
            return fail("inflate", "output larger than expected");
        // 정렬된 뒤라 8비트씩 꺼내면 원본 바이트 그대로다. PNG 인코더가 저장 블록을 쓰는 일은 드물다.
        for (std::uint32_t i = 0; i < length; ++i)
            m_output.push_back(static_cast<std::uint8_t>(m_reader.bits(8)));
        if (m_reader.overrun())
            return fail("inflate", "truncated stored block");
        return {};
    }

    [[nodiscard]] std::expected<void, std::string> fixed()
    {
        std::array<std::uint8_t, 288 + 32> lengths{};
        for (int i = 0; i < 144; ++i)
            lengths[i] = 8;
        for (int i = 144; i < 256; ++i)
            lengths[i] = 9;
        for (int i = 256; i < 280; ++i)
            lengths[i] = 7;
        for (int i = 280; i < 288; ++i)
            lengths[i] = 8;
        for (int i = 288; i < 288 + 30; ++i)
            lengths[i] = 5;
        // 고정 표는 항상 유효하다.
        [[maybe_unused]] const bool literals = m_literals.build(std::span(lengths).first(288));
        [[maybe_unused]] const bool distances = m_distances.build(std::span(lengths).subspan(288, 30));
        return codes();
    }

    [[nodiscard]] std::expected<void, std::string> dynamic()
    {
        const auto literalCount = static_cast<int>(m_reader.bits(5)) + 257;
        const auto distanceCount = static_cast<int>(m_reader.bits(5)) + 1;
        const auto codeLengthCount = static_cast<int>(m_reader.bits(4)) + 4;
        if (literalCount > 286 || distanceCount > 30)
            return fail("inflate", "bad code counts");

        constexpr std::array<std::uint8_t, 19> order{16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                                     11, 4,  12, 3, 13, 2, 14, 1, 15};
        std::array<std::uint8_t, 19> codeLengthLengths{};
        for (int i = 0; i < codeLengthCount; ++i)
            codeLengthLengths[order[i]] = static_cast<std::uint8_t>(m_reader.bits(3));
        Huffman codeLengths;
        if (!codeLengths.build(codeLengthLengths))
            return fail("inflate", "bad code length table");

        std::array<std::uint8_t, 286 + 30> lengths{};
        int index = 0;
        while (index < literalCount + distanceCount)
        {
            const int symbol = codeLengths.decode(m_reader);
            if (symbol < 0)
                return fail("inflate", "bad code length code");
            if (symbol < 16)
            {
                lengths[index++] = static_cast<std::uint8_t>(symbol);
                continue;
            }
            std::uint8_t value = 0;
            int repeat = 0;
            if (symbol == 16)
            {
                if (index == 0)
                    return fail("inflate", "repeat without previous length");
                value = lengths[index - 1];
                repeat = 3 + static_cast<int>(m_reader.bits(2));
            }
            else if (symbol == 17)
                repeat = 3 + static_cast<int>(m_reader.bits(3));
            else
                repeat = 11 + static_cast<int>(m_reader.bits(7));
            if (index + repeat > literalCount + distanceCount)
                return fail("inflate", "code lengths overflow");
            for (int i = 0; i < repeat; ++i)
                lengths[index++] = value;
        }
        if (m_reader.overrun())
            return fail("inflate", "truncated stream");
        if (lengths[256] == 0)
            return fail("inflate", "missing end-of-block code");
        if (!m_literals.build(std::span(lengths).first(static_cast<std::size_t>(literalCount))) ||
            !m_distances.build(std::span(lengths).subspan(static_cast<std::size_t>(literalCount),
                                                          static_cast<std::size_t>(distanceCount))))
            return fail("inflate", "bad huffman table");
        return codes();
    }

    [[nodiscard]] std::expected<void, std::string> codes()
    {
        for (;;)
        {
            const int symbol = m_literals.decode(m_reader);
            if (symbol < 0)
                return fail("inflate", "bad literal/length code");
            if (symbol < 256)
            {
                if (m_output.size() >= m_maxOutput)
                    return fail("inflate", "output larger than expected");
                m_output.push_back(static_cast<std::uint8_t>(symbol));
                continue;
            }
            if (symbol == 256)
                return {};
            const int lengthSymbol = symbol - 257;
            if (lengthSymbol >= 29)
                return fail("inflate", "bad length symbol");
            const std::size_t length = lengthBase[lengthSymbol] + m_reader.bits(lengthExtra[lengthSymbol]);
            const int distanceSymbol = m_distances.decode(m_reader);
            if (distanceSymbol < 0 || distanceSymbol >= 30)
                return fail("inflate", "bad distance code");
            const std::size_t distance =
                distanceBase[distanceSymbol] + m_reader.bits(distanceExtra[distanceSymbol]);
            if (distance > m_output.size())
                return fail("inflate", "distance before start of output");
            if (m_output.size() + length > m_maxOutput)
                return fail("inflate", "output larger than expected");
            if (m_reader.overrun())
                return fail("inflate", "truncated stream");
            // 겹치는 복사(distance < length)는 앞에서부터 한 바이트씩 해야 반복 패턴이 된다.
            const std::size_t start = m_output.size() - distance;
            m_output.resize(m_output.size() + length);
            std::uint8_t *out = m_output.data();
            const std::size_t base = m_output.size() - length;
            for (std::size_t i = 0; i < length; ++i)
                out[base + i] = out[start + i];
        }
    }
};

[[nodiscard]] std::uint32_t adler32(std::span<const std::uint8_t> data) noexcept
{
    constexpr std::uint32_t modulus = 65521;
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    std::size_t index = 0;
    while (index < data.size())
    {
        // 5552바이트마다 나머지를 구하면 32비트 합이 넘치지 않는다(zlib과 같은 값).
        const std::size_t end = std::min(data.size(), index + 5552);
        for (; index < end; ++index)
        {
            a += data[index];
            b += a;
        }
        a %= modulus;
        b %= modulus;
    }
    return (b << 16) | a;
}
} // namespace

std::expected<std::vector<std::uint8_t>, std::string> InflateZlib(std::span<const std::uint8_t> stream,
                                                                  std::size_t expectedSize,
                                                                  std::size_t maxOutput)
{
    if (stream.size() < 6)
        return fail("zlib", "stream too short");
    const std::uint8_t cmf = stream[0];
    const std::uint8_t flags = stream[1];
    if ((cmf & 15) != 8 || (cmf >> 4) > 7)
        return fail("zlib", "unsupported compression method");
    if (((cmf << 8) | flags) % 31 != 0)
        return fail("zlib", "bad header check");
    if (flags & 0x20)
        return fail("zlib", "preset dictionary not supported");

    Inflater inflater(stream.subspan(2), expectedSize, maxOutput);
    if (auto result = inflater.run(); !result)
        return std::unexpected(std::move(result.error()));
    const std::size_t trailer = 2 + inflater.endPosition();
    auto output = inflater.takeOutput();
    if (trailer + 4 > stream.size())
        return fail("zlib", "missing adler-32");
    const std::uint32_t expected =
        (std::uint32_t{stream[trailer]} << 24) | (std::uint32_t{stream[trailer + 1]} << 16) |
        (std::uint32_t{stream[trailer + 2]} << 8) | std::uint32_t{stream[trailer + 3]};
    if (adler32(output) != expected)
        return fail("zlib", "adler-32 mismatch");
    return output;
}
} // namespace sr::codec

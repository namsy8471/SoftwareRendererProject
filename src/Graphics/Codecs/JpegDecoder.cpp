// 베이스라인 JPEG(ITU-T T.81) 디코더.
// 결과 픽셀 값은 널리 쓰이는 정수 경로와 같게 맞췄다: IJG jidctint(ISLOW) 정수 IDCT,
// JFIF 중심 맞춤 "fancy" 업샘플링(2배 방향은 3:1 삼각 필터, 그 밖의 비율은 최근접), 12비트 고정소수점
// YCbCr→RGB. 그래서 같은 파일을 stb_image·libjpeg 기본 설정으로 읽은 텍스처와 픽셀 단위로 같다.
#include "Graphics/Codecs/CodecSupport.h"

#include <algorithm>
#include <array>
#include <utility>
#include <vector>

namespace sr::codec
{
namespace
{
using detail::ByteReader;
using detail::fail;

constexpr std::array<std::uint8_t, 64> dezigzag{
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48,
    41, 34, 27, 20, 13, 6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23,
    30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

constexpr int fastBits = 9;

// JPEG 허프만 표(코드 길이 1~16). fast는 상위 9비트로 바로 찾는 심볼 인덱스, 255면 긴 코드다.
struct Huffman
{
    std::array<std::uint8_t, 1 << fastBits> fast{};
    std::array<std::uint8_t, 256> values{};
    std::array<std::uint8_t, 257> sizes{};
    std::array<std::uint16_t, 256> codes{};
    std::array<std::uint32_t, 18> maxCode{}; // 길이 k의 최대 코드 + 1을 16비트로 왼쪽 정렬한 값
    std::array<int, 17> delta{};             // 길이 k의 코드 → 심볼 인덱스 보정
    bool defined = false;

    [[nodiscard]] bool build(std::span<const std::uint8_t, 16> counts) noexcept
    {
        int symbol = 0;
        for (int length = 1; length <= 16; ++length)
            for (int i = 0; i < counts[length - 1]; ++i)
            {
                if (symbol >= 256)
                    return false;
                sizes[symbol++] = static_cast<std::uint8_t>(length);
            }
        if (symbol >= static_cast<int>(sizes.size()))
            return false;
        sizes[symbol] = 0;

        std::uint32_t code = 0;
        int index = 0;
        for (int length = 1; length <= 16; ++length)
        {
            delta[length] = index - static_cast<int>(code);
            if (sizes[index] == length)
            {
                while (sizes[index] == length)
                    codes[index++] = static_cast<std::uint16_t>(code++);
                if (code - 1 >= (1u << length))
                    return false;
            }
            maxCode[length] = code << (16 - length);
            code <<= 1;
        }
        maxCode[17] = 0xFFFFFFFFu;

        fast.fill(255);
        for (int i = 0; i < index; ++i)
        {
            const int size = sizes[i];
            if (size > fastBits)
                continue;
            const int first = codes[i] << (fastBits - size);
            for (int j = 0; j < (1 << (fastBits - size)); ++j)
                fast[first + j] = static_cast<std::uint8_t>(i);
        }
        defined = true;
        return true;
    }
};

struct Component
{
    int id = 0;
    int h = 1, v = 1; // 샘플링 계수
    int quantTable = 0;
    int dcTable = 0, acTable = 0;
    int dcPrediction = 0;
    int width = 0, height = 0; // 실제 샘플 수(올림)
    int stride = 0, rows = 0;  // MCU 경계까지 채운 평면 크기
    std::vector<std::uint8_t> plane;
};

// 엔트로피 부호 비트 읽기(MSB 우선). 0xFF 00은 0xFF 한 바이트, 그 밖의 0xFF xx는 마커라서 멈추고 0을 채운다.
class BitReader
{
  public:
    BitReader(std::span<const std::uint8_t> bytes, std::size_t position) noexcept
        : m_bytes(bytes), m_position(position)
    {
    }

    void reset() noexcept
    {
        m_buffer = 0;
        m_count = 0;
        m_marker = 0;
        m_noMore = false;
    }
    void fill() noexcept
    {
        while (m_count <= 24)
        {
            std::uint32_t byte = 0;
            if (!m_noMore && m_position < m_bytes.size())
            {
                byte = m_bytes[m_position++];
                if (byte == 0xFF)
                {
                    int next = m_position < m_bytes.size() ? m_bytes[m_position++] : 0xD9;
                    while (next == 0xFF)
                        next = m_position < m_bytes.size() ? m_bytes[m_position++] : 0xD9;
                    if (next != 0)
                    {
                        m_marker = next;
                        m_noMore = true;
                        return;
                    }
                }
            }
            else
                m_noMore = true;
            m_buffer |= byte << (24 - m_count);
            m_count += 8;
        }
    }
    [[nodiscard]] int decode(const Huffman &table) noexcept
    {
        if (m_count < 16)
            fill();
        const int fast = table.fast[(m_buffer >> (32 - fastBits)) & ((1u << fastBits) - 1)];
        if (fast < 255)
        {
            const int size = table.sizes[fast];
            if (size > m_count)
                return -1;
            consume(size);
            return table.values[fast];
        }
        const std::uint32_t top = m_buffer >> 16;
        int length = fastBits + 1;
        while (top >= table.maxCode[length])
            ++length;
        if (length == 17 || length > m_count)
            return -1;
        const int index =
            static_cast<int>((m_buffer >> (32 - length)) & ((1u << length) - 1)) + table.delta[length];
        if (index < 0 || index >= 256)
            return -1;
        consume(length);
        return table.values[index];
    }
    // receive + extend: n비트 값을 부호 있는 계수로 바꾼다(첫 비트 0이면 음수).
    [[nodiscard]] int receiveExtend(int bits) noexcept
    {
        if (bits == 0)
            return 0;
        if (m_count < bits)
            fill();
        if (m_count < bits)
            return 0;
        const auto value = static_cast<int>(m_buffer >> (32 - bits));
        consume(bits);
        return value < (1 << (bits - 1)) ? value - (1 << bits) + 1 : value;
    }
    [[nodiscard]] int marker() noexcept
    {
        if (m_count < 24)
            fill();
        return m_marker;
    }
    // 스캔이 끝난 뒤 다음 마커를 찾을 입력 위치. 마커를 이미 만났으면 그 마커 바로 뒤다.
    [[nodiscard]] std::size_t position() const noexcept { return m_position; }
    [[nodiscard]] int pendingMarker() const noexcept { return m_marker; }

  private:
    void consume(int bits) noexcept
    {
        m_buffer <<= bits;
        m_count -= bits;
    }

    std::span<const std::uint8_t> m_bytes;
    std::size_t m_position;
    std::uint32_t m_buffer = 0;
    int m_count = 0;
    int m_marker = 0;
    bool m_noMore = false;
};

// IJG jidctint(ISLOW)의 1차원 변환. 상수는 12비트 고정소수점이다.
// 손상 파일의 큰 계수에서 부호 있는 오버플로(UB)가 나지 않도록 64비트로 계산한다. 정상 파일 결과는 32비트와
// 같다.
struct Idct1d
{
    std::int64_t x0, x1, x2, x3, t0, t1, t2, t3;

    static constexpr std::int64_t fix(double value) noexcept
    {
        return static_cast<std::int64_t>(value * 4096 + 0.5);
    }

    Idct1d(std::int64_t s0, std::int64_t s1, std::int64_t s2, std::int64_t s3, std::int64_t s4,
           std::int64_t s5, std::int64_t s6, std::int64_t s7) noexcept
    {
        std::int64_t p2 = s2, p3 = s6;
        std::int64_t p1 = (p2 + p3) * fix(0.5411961f);
        t2 = p1 + p3 * fix(-1.847759065f);
        t3 = p1 + p2 * fix(0.765366865f);
        p2 = s0;
        p3 = s4;
        t0 = (p2 + p3) * 4096;
        t1 = (p2 - p3) * 4096;
        x0 = t0 + t3;
        x3 = t0 - t3;
        x1 = t1 + t2;
        x2 = t1 - t2;
        t0 = s7;
        t1 = s5;
        t2 = s3;
        t3 = s1;
        p3 = t0 + t2;
        std::int64_t p4 = t1 + t3;
        p1 = t0 + t3;
        p2 = t1 + t2;
        const std::int64_t p5 = (p3 + p4) * fix(1.175875602f);
        t0 = t0 * fix(0.298631336f);
        t1 = t1 * fix(2.053119869f);
        t2 = t2 * fix(3.072711026f);
        t3 = t3 * fix(1.501321110f);
        p1 = p5 + p1 * fix(-0.899976223f);
        p2 = p5 + p2 * fix(-2.562915447f);
        p3 = p3 * fix(-1.961570560f);
        p4 = p4 * fix(-0.390180644f);
        t3 += p1 + p4;
        t2 += p2 + p3;
        t1 += p2 + p4;
        t0 += p1 + p3;
    }
};

[[nodiscard]] std::uint8_t clampByte(std::int64_t value) noexcept
{
    return static_cast<std::uint8_t>(std::clamp<std::int64_t>(value, 0, 255));
}

void inverseDct(std::uint8_t *out, int stride, const std::array<std::int16_t, 64> &block) noexcept
{
    std::array<std::int64_t, 64> columns{};
    for (int i = 0; i < 8; ++i)
    {
        const std::int16_t *d = block.data() + i;
        std::int64_t *v = columns.data() + i;
        if (d[8] == 0 && d[16] == 0 && d[24] == 0 && d[32] == 0 && d[40] == 0 && d[48] == 0 && d[56] == 0)
        {
            // AC가 없는 열은 DC만 퍼뜨린다(첫 단계가 남기는 2비트 정밀도 그대로).
            const std::int64_t dc = d[0] * 4;
            for (int row = 0; row < 8; ++row)
                v[row * 8] = dc;
            continue;
        }
        Idct1d t(d[0], d[8], d[16], d[24], d[32], d[40], d[48], d[56]);
        t.x0 += 512;
        t.x1 += 512;
        t.x2 += 512;
        t.x3 += 512;
        v[0] = (t.x0 + t.t3) >> 10;
        v[56] = (t.x0 - t.t3) >> 10;
        v[8] = (t.x1 + t.t2) >> 10;
        v[48] = (t.x1 - t.t2) >> 10;
        v[16] = (t.x2 + t.t1) >> 10;
        v[40] = (t.x2 - t.t1) >> 10;
        v[24] = (t.x3 + t.t0) >> 10;
        v[32] = (t.x3 - t.t0) >> 10;
    }
    for (int i = 0; i < 8; ++i, out += stride)
    {
        const std::int64_t *v = columns.data() + i * 8;
        Idct1d t(v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7]);
        // 12비트 상수 + 첫 단계 2비트 + 두 방향 sqrt(8) 스케일 = 17비트. 반올림과 +128 레벨 이동을 함께
        // 더한다.
        constexpr std::int64_t bias = 65536 + (std::int64_t{128} << 17);
        t.x0 += bias;
        t.x1 += bias;
        t.x2 += bias;
        t.x3 += bias;
        out[0] = clampByte((t.x0 + t.t3) >> 17);
        out[7] = clampByte((t.x0 - t.t3) >> 17);
        out[1] = clampByte((t.x1 + t.t2) >> 17);
        out[6] = clampByte((t.x1 - t.t2) >> 17);
        out[2] = clampByte((t.x2 + t.t1) >> 17);
        out[5] = clampByte((t.x2 - t.t1) >> 17);
        out[3] = clampByte((t.x3 + t.t0) >> 17);
        out[4] = clampByte((t.x3 - t.t0) >> 17);
    }
}

class JpegDecoder
{
  public:
    explicit JpegDecoder(std::span<const std::uint8_t> bytes) noexcept : m_bytes(bytes), m_reader(bytes) {}

    [[nodiscard]] std::expected<DecodedImage, std::string> decode()
    {
        if (m_reader.u8() != 0xFF || m_reader.u8() != 0xD8)
            return fail("jpeg", "missing SOI");
        bool frameSeen = false;
        bool scanSeen = false;
        for (;;)
        {
            const int marker = nextMarker();
            if (marker < 0)
            {
                // 일부 인코더는 EOI 없이 끝난다. 스캔을 하나라도 읽었으면 그대로 쓴다.
                if (scanSeen)
                    break;
                return fail("jpeg", "unexpected end of file");
            }
            if (marker == 0xD9)
                break;
            if (marker == 0xC0 || marker == 0xC1)
            {
                if (frameSeen)
                    return fail("jpeg", "multiple frames");
                if (auto frame = readFrame(); !frame)
                    return std::unexpected(std::move(frame.error()));
                frameSeen = true;
            }
            else if (marker == 0xC2 || marker == 0xC6 || marker == 0xCA || marker == 0xCE)
                return fail("jpeg", "progressive JPEG not supported");
            else if ((marker >= 0xC3 && marker <= 0xCF) && marker != 0xC4 && marker != 0xC8 && marker != 0xCC)
                return fail("jpeg", "lossless/arithmetic JPEG not supported");
            else if (marker == 0xC4)
            {
                if (auto table = readHuffmanTables(); !table)
                    return std::unexpected(std::move(table.error()));
            }
            else if (marker == 0xDB)
            {
                if (auto table = readQuantTables(); !table)
                    return std::unexpected(std::move(table.error()));
            }
            else if (marker == 0xDD)
            {
                if (m_reader.u16be() != 4)
                    return fail("jpeg", "bad DRI length");
                m_restartInterval = m_reader.u16be();
            }
            else if (marker == 0xDA)
            {
                if (!frameSeen)
                    return fail("jpeg", "scan before frame");
                if (auto scan = readScan(); !scan)
                    return std::unexpected(std::move(scan.error()));
                scanSeen = true;
            }
            else if (marker >= 0xD0 && marker <= 0xD7)
                continue; // 스캔 밖의 남은 RST는 건너뛴다
            else if (auto skipped = readOtherSegment(marker); !skipped)
                return std::unexpected(std::move(skipped.error()));
            if (!m_reader.ok())
                return fail("jpeg", "truncated segment");
        }
        if (!scanSeen)
            return fail("jpeg", "no image data");
        return convert();
    }

  private:
    std::span<const std::uint8_t> m_bytes;
    ByteReader m_reader;
    int m_pendingMarker = -1; // 엔트로피 부호 안에서 만난 마커(이미 소비됨)
    std::array<std::array<std::uint16_t, 64>, 4> m_quant{};
    std::array<Huffman, 4> m_dcTables{};
    std::array<Huffman, 4> m_acTables{};
    std::vector<Component> m_components;
    int m_width = 0, m_height = 0;
    int m_maxH = 1, m_maxV = 1;
    int m_mcuColumns = 0, m_mcuRows = 0;
    int m_restartInterval = 0;
    bool m_jfif = false;
    int m_adobeTransform = -1;
    int m_rgbIds = 0; // 성분 ID가 'R','G','B' 순서인 수

    // 0xFF 채움 바이트를 건너뛰고 다음 마커를 돌려준다. 마커가 아닌 잡음 바이트는 건너뛴다. 파일 끝이면 -1.
    [[nodiscard]] int nextMarker() noexcept
    {
        if (m_pendingMarker >= 0)
            return std::exchange(m_pendingMarker, -1);
        while (m_reader.remaining() >= 2)
        {
            if (m_reader.u8() != 0xFF)
                continue;
            int code = m_reader.u8();
            while (code == 0xFF && m_reader.remaining() > 0)
                code = m_reader.u8();
            if (code != 0 && code != 0xFF)
                return code;
        }
        return -1;
    }

    [[nodiscard]] std::expected<void, std::string> readOtherSegment(int marker)
    {
        const int length = m_reader.u16be();
        if (length < 2)
            return fail("jpeg", "bad segment length");
        const auto body = m_reader.take(static_cast<std::size_t>(length - 2));
        if (!m_reader.ok())
            return fail("jpeg", "truncated segment");
        constexpr std::array<std::uint8_t, 5> jfif{'J', 'F', 'I', 'F', 0};
        constexpr std::array<std::uint8_t, 6> adobe{'A', 'd', 'o', 'b', 'e', 0};
        if (marker == 0xE0 && body.size() >= jfif.size() &&
            std::equal(jfif.begin(), jfif.end(), body.begin()))
            m_jfif = true;
        else if (marker == 0xEE && body.size() >= 12 && std::equal(adobe.begin(), adobe.end(), body.begin()))
            m_adobeTransform = body[11];
        return {};
    }

    [[nodiscard]] std::expected<void, std::string> readQuantTables()
    {
        int remaining = m_reader.u16be() - 2;
        while (remaining > 0)
        {
            const int info = m_reader.u8();
            const int precision = info >> 4;
            const int table = info & 15;
            if (precision > 1 || table > 3)
                return fail("jpeg", "bad DQT");
            for (int i = 0; i < 64; ++i)
                m_quant[table][dezigzag[i]] = precision ? m_reader.u16be() : std::uint16_t{m_reader.u8()};
            remaining -= precision ? 129 : 65;
        }
        if (remaining != 0 || !m_reader.ok())
            return fail("jpeg", "bad DQT length");
        return {};
    }

    [[nodiscard]] std::expected<void, std::string> readHuffmanTables()
    {
        int remaining = m_reader.u16be() - 2;
        while (remaining > 0)
        {
            const int info = m_reader.u8();
            const int tableClass = info >> 4;
            const int table = info & 15;
            if (tableClass > 1 || table > 3)
                return fail("jpeg", "bad DHT");
            std::array<std::uint8_t, 16> counts{};
            int total = 0;
            for (auto &count : counts)
            {
                count = m_reader.u8();
                total += count;
            }
            if (total > 256)
                return fail("jpeg", "bad DHT");
            Huffman &huffman = tableClass == 0 ? m_dcTables[table] : m_acTables[table];
            if (!huffman.build(counts))
                return fail("jpeg", "bad huffman code lengths");
            for (int i = 0; i < total; ++i)
                huffman.values[i] = m_reader.u8();
            remaining -= 17 + total;
        }
        if (remaining != 0 || !m_reader.ok())
            return fail("jpeg", "bad DHT length");
        return {};
    }

    [[nodiscard]] std::expected<void, std::string> readFrame()
    {
        const int length = m_reader.u16be();
        if (m_reader.u8() != 8)
            return fail("jpeg", "only 8-bit samples supported");
        m_height = m_reader.u16be();
        m_width = m_reader.u16be();
        const int count = m_reader.u8();
        if (m_height == 0)
            return fail("jpeg", "DNL height not supported");
        if (m_width == 0)
            return fail("jpeg", "zero width");
        if (count != 1 && count != 3 && count != 4)
            return fail("jpeg", "unsupported component count");
        if (length != 8 + 3 * count)
            return fail("jpeg", "bad SOF length");
        m_components.resize(static_cast<std::size_t>(count));
        constexpr std::array<int, 3> rgb{'R', 'G', 'B'};
        for (int i = 0; i < count; ++i)
        {
            auto &component = m_components[static_cast<std::size_t>(i)];
            component.id = m_reader.u8();
            if (count == 3 && component.id == rgb[static_cast<std::size_t>(i)])
                ++m_rgbIds;
            const int sampling = m_reader.u8();
            component.h = sampling >> 4;
            component.v = sampling & 15;
            component.quantTable = m_reader.u8();
            if (component.h < 1 || component.h > 4 || component.v < 1 || component.v > 4 ||
                component.quantTable > 3)
                return fail("jpeg", "bad component parameters");
        }
        if (!m_reader.ok())
            return fail("jpeg", "truncated SOF");
        if (static_cast<std::uint64_t>(m_width) * static_cast<std::uint64_t>(m_height) > maxDecodedPixels)
            return fail("jpeg", "image too large");

        for (const auto &component : m_components)
        {
            m_maxH = std::max(m_maxH, component.h);
            m_maxV = std::max(m_maxV, component.v);
        }
        m_mcuColumns = (m_width + m_maxH * 8 - 1) / (m_maxH * 8);
        m_mcuRows = (m_height + m_maxV * 8 - 1) / (m_maxV * 8);
        for (auto &component : m_components)
        {
            // 정수배가 아닌 샘플링(예: 3:2)은 실제 파일에서 보기 힘들고 업샘플러가 다루지 않는다.
            if (m_maxH % component.h != 0 || m_maxV % component.v != 0)
                return fail("jpeg", "non-integer sampling ratio");
            component.width = (m_width * component.h + m_maxH - 1) / m_maxH;
            component.height = (m_height * component.v + m_maxV - 1) / m_maxV;
            component.stride = m_mcuColumns * component.h * 8;
            component.rows = m_mcuRows * component.v * 8;
            component.plane.assign(static_cast<std::size_t>(component.stride) * component.rows, 0);
        }
        return {};
    }

    [[nodiscard]] std::expected<void, std::string> decodeBlock(BitReader &bits, Component &component,
                                                               std::uint8_t *out)
    {
        const Huffman &dc = m_dcTables[component.dcTable];
        const Huffman &ac = m_acTables[component.acTable];
        const auto &quant = m_quant[component.quantTable];
        std::array<std::int16_t, 64> block{};

        const int dcBits = bits.decode(dc);
        if (dcBits < 0 || dcBits > 15)
            return fail("jpeg", "bad DC code");
        const std::int64_t prediction = std::int64_t{component.dcPrediction} + bits.receiveExtend(dcBits);
        if (prediction < -32768 || prediction > 32767)
            return fail("jpeg", "DC out of range");
        component.dcPrediction = static_cast<int>(prediction);
        // 역양자화 결과는 16비트 계수로 저장한다(손상 파일의 범위 밖 값은 C++20 정수 변환 규칙대로 감긴다).
        block[0] = static_cast<std::int16_t>(prediction * quant[0]);

        for (int k = 1; k < 64;)
        {
            const int symbol = bits.decode(ac);
            if (symbol < 0)
                return fail("jpeg", "bad AC code");
            const int run = symbol >> 4;
            const int size = symbol & 15;
            if (size == 0)
            {
                if (symbol != 0xF0)
                    break; // EOB
                k += 16;
                continue;
            }
            k += run;
            if (k > 63)
                return fail("jpeg", "AC index out of range");
            const int zig = dezigzag[static_cast<std::size_t>(k++)];
            block[static_cast<std::size_t>(zig)] = static_cast<std::int16_t>(
                std::int64_t{bits.receiveExtend(size)} * quant[static_cast<std::size_t>(zig)]);
        }
        inverseDct(out, component.stride, block);
        return {};
    }

    [[nodiscard]] std::expected<void, std::string> readScan()
    {
        const int length = m_reader.u16be();
        const int count = m_reader.u8();
        if (count < 1 || count > static_cast<int>(m_components.size()) || length != 6 + 2 * count)
            return fail("jpeg", "bad SOS");
        std::array<Component *, 4> order{};
        for (int i = 0; i < count; ++i)
        {
            const int id = m_reader.u8();
            const int tables = m_reader.u8();
            const auto found = std::ranges::find(m_components, id, &Component::id);
            if (found == m_components.end())
                return fail("jpeg", "SOS references unknown component");
            found->dcTable = tables >> 4;
            found->acTable = tables & 15;
            if (found->dcTable > 3 || found->acTable > 3 || !m_dcTables[found->dcTable].defined ||
                !m_acTables[found->acTable].defined)
                return fail("jpeg", "SOS references undefined huffman table");
            order[static_cast<std::size_t>(i)] = &*found;
        }
        const int spectralStart = m_reader.u8();
        m_reader.skip(1); // 스펙트럼 끝: 베이스라인은 63이어야 하지만 0을 쓰는 인코더가 있어 무시한다
        const int approximation = m_reader.u8();
        if (!m_reader.ok())
            return fail("jpeg", "truncated SOS");
        if (spectralStart != 0 || approximation != 0)
            return fail("jpeg", "bad SOS for baseline");

        BitReader bits(m_bytes, m_reader.position());
        int todo = m_restartInterval ? m_restartInterval : 0x7FFFFFFF;
        const auto resetEntropy = [&]()
        {
            bits.reset();
            for (auto &component : m_components)
                component.dcPrediction = 0;
            todo = m_restartInterval ? m_restartInterval : 0x7FFFFFFF;
        };
        // MCU 하나가 끝날 때마다 호출한다. 재시작 구간이 끝났는데 RST가 없으면 손상 파일이라 스캔을 멈춘다.
        const auto afterMcu = [&]() -> bool
        {
            if (--todo > 0)
                return true;
            const int marker = bits.marker();
            if (marker < 0xD0 || marker > 0xD7)
                return false;
            resetEntropy();
            return true;
        };
        resetEntropy();

        bool keepGoing = true;
        if (count == 1)
        {
            // 비인터리브 스캔: 성분 자기 블록 격자를 행 순서로 읽는다. 블록 하나가 MCU다.
            Component &component = *order[0];
            const int blockColumns = (component.width + 7) / 8;
            const int blockRows = (component.height + 7) / 8;
            for (int by = 0; by < blockRows && keepGoing; ++by)
                for (int bx = 0; bx < blockColumns && keepGoing; ++bx)
                {
                    std::uint8_t *out = component.plane.data() +
                                        static_cast<std::size_t>(by) * 8 * component.stride +
                                        static_cast<std::size_t>(bx) * 8;
                    if (auto block = decodeBlock(bits, component, out); !block)
                        return block;
                    keepGoing = afterMcu();
                }
        }
        else
        {
            for (int my = 0; my < m_mcuRows && keepGoing; ++my)
                for (int mx = 0; mx < m_mcuColumns && keepGoing; ++mx)
                {
                    for (int i = 0; i < count; ++i)
                    {
                        Component &component = *order[static_cast<std::size_t>(i)];
                        for (int y = 0; y < component.v; ++y)
                            for (int x = 0; x < component.h; ++x)
                            {
                                const std::size_t px = static_cast<std::size_t>(mx * component.h + x) * 8;
                                const std::size_t py = static_cast<std::size_t>(my * component.v + y) * 8;
                                std::uint8_t *out = component.plane.data() + py * component.stride + px;
                                if (auto block = decodeBlock(bits, component, out); !block)
                                    return block;
                            }
                    }
                    keepGoing = afterMcu();
                }
        }

        // 엔트로피 부호 뒤 위치로 돌아간다. 비트 읽기가 마커를 이미 소비했으면 그 마커를 다음에 처리한다.
        const int pending = bits.marker();
        m_reader.seek(bits.position());
        if (pending != 0)
            m_pendingMarker = pending;
        return {};
    }

    // 성분 평면을 출력 해상도로 늘리는 행 단위 업샘플러.
    struct Resampler
    {
        int hs = 1, vs = 1;
        int ystep = 0, ypos = 0;
        int lowWidth = 0;
        const std::uint8_t *line0 = nullptr;
        const std::uint8_t *line1 = nullptr;
        std::vector<std::uint8_t> buffer;
    };

    // near: 출력 행에 가까운 원본 행, far: 반대쪽 이웃 행. 반환값은 출력 폭의 샘플 행이다.
    [[nodiscard]] static const std::uint8_t *resampleRow(Resampler &r, const std::uint8_t *nearRow,
                                                         const std::uint8_t *farRow) noexcept
    {
        std::uint8_t *out = r.buffer.data();
        const int w = r.lowWidth;
        const auto div4 = [](int value) { return static_cast<std::uint8_t>(value >> 2); };
        if (r.hs == 1 && r.vs == 1)
            return nearRow;
        if (r.hs == 1 && r.vs == 2)
        {
            for (int i = 0; i < w; ++i)
                out[i] = div4(3 * nearRow[i] + farRow[i] + 2);
            return out;
        }
        if (r.hs == 2 && r.vs == 1)
        {
            if (w == 1)
            {
                out[0] = out[1] = nearRow[0];
                return out;
            }
            out[0] = nearRow[0];
            out[1] = div4(nearRow[0] * 3 + nearRow[1] + 2);
            int i = 1;
            for (; i < w - 1; ++i)
            {
                const int n = 3 * nearRow[i] + 2;
                out[i * 2] = div4(n + nearRow[i - 1]);
                out[i * 2 + 1] = div4(n + nearRow[i + 1]);
            }
            out[i * 2] = div4(nearRow[w - 2] * 3 + nearRow[w - 1] + 2);
            out[i * 2 + 1] = nearRow[w - 1];
            return out;
        }
        if (r.hs == 2 && r.vs == 2)
        {
            if (w == 1)
            {
                out[0] = out[1] = div4(3 * nearRow[0] + farRow[0] + 2);
                return out;
            }
            int t1 = 3 * nearRow[0] + farRow[0];
            out[0] = div4(t1 + 2);
            for (int i = 1; i < w; ++i)
            {
                const int t0 = t1;
                t1 = 3 * nearRow[i] + farRow[i];
                out[i * 2 - 1] = static_cast<std::uint8_t>((3 * t0 + t1 + 8) >> 4);
                out[i * 2] = static_cast<std::uint8_t>((3 * t1 + t0 + 8) >> 4);
            }
            out[w * 2 - 1] = div4(t1 + 2);
            return out;
        }
        for (int i = 0; i < w; ++i)
            for (int j = 0; j < r.hs; ++j)
                out[i * r.hs + j] = nearRow[i];
        return out;
    }

    [[nodiscard]] static std::uint8_t multiplyByte(std::uint8_t x, std::uint8_t y) noexcept
    {
        const unsigned t = unsigned{x} * y + 128;
        return static_cast<std::uint8_t>((t + (t >> 8)) >> 8);
    }

    // 12비트 고정소수점 JFIF YCbCr→RGB. 정수 연산 순서까지 맞춰야 표준 정수 디코더와 같은 값이 나온다.
    static void yCbCrToRgb(std::uint8_t *out, const std::uint8_t *y, const std::uint8_t *cb,
                           const std::uint8_t *cr, int count) noexcept
    {
        constexpr auto fix = [](float value) { return static_cast<int>(value * 4096.0f + 0.5f) * 256; };
        for (int i = 0; i < count; ++i, out += 4)
        {
            const int yFixed = (y[i] << 20) + (1 << 19);
            const int crValue = cr[i] - 128;
            const int cbValue = cb[i] - 128;
            int r = yFixed + crValue * fix(1.40200f);
            // stb와 같이 Cb 항의 하위 16비트를 버린다(SIMD 구현과 결과를 맞추려는 정밀도 축소).
            int g = yFixed + crValue * -fix(0.71414f) +
                    static_cast<int>(static_cast<unsigned>(cbValue * -fix(0.34414f)) & 0xFFFF0000u);
            int b = yFixed + cbValue * fix(1.77200f);
            out[0] = static_cast<std::uint8_t>(std::clamp(r >> 20, 0, 255));
            out[1] = static_cast<std::uint8_t>(std::clamp(g >> 20, 0, 255));
            out[2] = static_cast<std::uint8_t>(std::clamp(b >> 20, 0, 255));
            out[3] = 255;
        }
    }

    [[nodiscard]] std::expected<DecodedImage, std::string> convert()
    {
        auto image = detail::allocateImage("jpeg", m_width, m_height);
        if (!image)
            return std::unexpected(std::move(image.error()));

        const std::size_t count = m_components.size();
        std::array<Resampler, 4> resamplers;
        for (std::size_t k = 0; k < count; ++k)
        {
            auto &r = resamplers[k];
            const auto &component = m_components[k];
            r.hs = m_maxH / component.h;
            r.vs = m_maxV / component.v;
            r.ystep = r.vs >> 1;
            r.lowWidth = (m_width + r.hs - 1) / r.hs;
            r.line0 = r.line1 = component.plane.data();
            r.buffer.assign(static_cast<std::size_t>(m_width) + 3, 0);
        }
        // 성분 ID가 R,G,B이거나, Adobe 변환 0이면서 JFIF가 아니면 RGB로 저장된 파일이다.
        const bool storedRgb = count == 3 && (m_rgbIds == 3 || (m_adobeTransform == 0 && !m_jfif));

        std::array<const std::uint8_t *, 4> rows{};
        for (int y = 0; y < m_height; ++y)
        {
            for (std::size_t k = 0; k < count; ++k)
            {
                auto &r = resamplers[k];
                // 출력 행이 원본 행의 아래쪽 절반이면 다음 원본 행(line1)이 가깝다.
                const bool bottomHalf = r.ystep >= (r.vs >> 1);
                rows[k] = resampleRow(r, bottomHalf ? r.line1 : r.line0, bottomHalf ? r.line0 : r.line1);
                if (++r.ystep >= r.vs)
                {
                    r.ystep = 0;
                    r.line0 = r.line1;
                    if (++r.ypos < m_components[k].height)
                        r.line1 += m_components[k].stride;
                }
            }
            std::uint8_t *out = image->rgba.data() + static_cast<std::size_t>(y) * m_width * 4;
            if (count == 1)
            {
                for (int x = 0; x < m_width; ++x, out += 4)
                {
                    out[0] = out[1] = out[2] = rows[0][x];
                    out[3] = 255;
                }
            }
            else if (count == 3)
            {
                if (storedRgb)
                    for (int x = 0; x < m_width; ++x, out += 4)
                    {
                        out[0] = rows[0][x];
                        out[1] = rows[1][x];
                        out[2] = rows[2][x];
                        out[3] = 255;
                    }
                else
                    yCbCrToRgb(out, rows[0], rows[1], rows[2], m_width);
            }
            else if (m_adobeTransform == 0)
            {
                // CMYK(Adobe는 반전 저장): RGB = CMY 값 × K.
                for (int x = 0; x < m_width; ++x, out += 4)
                {
                    const auto k = rows[3][x];
                    out[0] = multiplyByte(rows[0][x], k);
                    out[1] = multiplyByte(rows[1][x], k);
                    out[2] = multiplyByte(rows[2][x], k);
                    out[3] = 255;
                }
            }
            else
            {
                yCbCrToRgb(out, rows[0], rows[1], rows[2], m_width);
                if (m_adobeTransform == 2)
                {
                    // YCCK: YCbCr로 CMY를 복원한 뒤 K를 곱한다.
                    for (int x = 0; x < m_width; ++x, out += 4)
                    {
                        const auto k = rows[3][x];
                        out[0] = multiplyByte(static_cast<std::uint8_t>(255 - out[0]), k);
                        out[1] = multiplyByte(static_cast<std::uint8_t>(255 - out[1]), k);
                        out[2] = multiplyByte(static_cast<std::uint8_t>(255 - out[2]), k);
                    }
                }
            }
        }
        return image;
    }
};
} // namespace

std::expected<DecodedImage, std::string> DecodeJpeg(std::span<const std::uint8_t> bytes)
{
    return JpegDecoder(bytes).decode();
}
} // namespace sr::codec

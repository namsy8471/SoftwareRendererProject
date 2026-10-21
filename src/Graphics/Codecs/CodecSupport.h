#pragma once

// 코덱 구현 파일끼리만 쓰는 도우미. 공개 API는 ImageCodecs.h에 있다.
#include "Graphics/Codecs/ImageCodecs.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>

namespace sr::codec::detail
{
// 범위를 넘는 읽기는 0을 돌려주고 실패 플래그를 세운다. 호출자는 묶음 단위로 ok()를 확인한다.
// 매 바이트마다 오류를 전파하지 않아도 잘린 파일이 범위 밖을 읽는 일은 없다.
class ByteReader
{
  public:
    explicit ByteReader(std::span<const std::uint8_t> bytes) noexcept : m_bytes(bytes) {}

    [[nodiscard]] bool ok() const noexcept { return !m_failed; }
    [[nodiscard]] std::size_t position() const noexcept { return m_position; }
    [[nodiscard]] std::size_t remaining() const noexcept { return m_bytes.size() - m_position; }

    [[nodiscard]] std::uint8_t u8() noexcept
    {
        if (m_position >= m_bytes.size())
        {
            m_failed = true;
            return 0;
        }
        return m_bytes[m_position++];
    }
    [[nodiscard]] std::uint16_t u16le() noexcept
    {
        const std::uint16_t low = u8();
        return static_cast<std::uint16_t>(low | (u8() << 8));
    }
    [[nodiscard]] std::uint16_t u16be() noexcept
    {
        const std::uint16_t high = u8();
        return static_cast<std::uint16_t>((high << 8) | u8());
    }
    [[nodiscard]] std::uint32_t u32le() noexcept
    {
        const std::uint32_t low = u16le();
        return low | (static_cast<std::uint32_t>(u16le()) << 16);
    }
    [[nodiscard]] std::uint32_t u32be() noexcept
    {
        const std::uint32_t high = u16be();
        return (high << 16) | u16be();
    }
    // 남은 바이트보다 많이 요청하면 빈 span과 실패 플래그를 돌려준다.
    [[nodiscard]] std::span<const std::uint8_t> take(std::size_t count) noexcept
    {
        if (count > remaining())
        {
            m_failed = true;
            m_position = m_bytes.size();
            return {};
        }
        const auto view = m_bytes.subspan(m_position, count);
        m_position += count;
        return view;
    }
    void skip(std::size_t count) noexcept { (void)take(count); }
    void seek(std::size_t position) noexcept
    {
        if (position > m_bytes.size())
        {
            m_failed = true;
            position = m_bytes.size();
        }
        m_position = position;
    }

  private:
    std::span<const std::uint8_t> m_bytes;
    std::size_t m_position = 0;
    bool m_failed = false;
};

[[nodiscard]] inline std::unexpected<std::string> fail(std::string_view format, std::string_view reason)
{
    return std::unexpected(std::string(format) + ": " + std::string(reason));
}

// 크기를 확인한 뒤 0으로 채운 RGBA 버퍼를 만든다. 실패 시 이유를 돌려준다.
[[nodiscard]] inline std::expected<DecodedImage, std::string> allocateImage(std::string_view format,
                                                                            std::int64_t width,
                                                                            std::int64_t height)
{
    if (width <= 0 || height <= 0)
        return fail(format, "invalid dimensions");
    if (static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) > maxDecodedPixels)
        return fail(format, "image too large");
    DecodedImage image;
    image.width = static_cast<int>(width);
    image.height = static_cast<int>(height);
    image.rgba.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4, 0);
    return image;
}

// n비트 값을 비트 반복으로 8비트 전체 범위로 늘린다(예: 5비트 abcde → abcdeabc).
// 255/max 곱셈과 달리 정수 연산만으로 0→0, max→255를 보장한다.
[[nodiscard]] constexpr std::uint8_t expandBits(std::uint32_t value, int bits) noexcept
{
    if (bits <= 0)
        return 0;
    if (bits >= 8)
        return static_cast<std::uint8_t>(value >> (bits - 8));
    std::uint32_t result = 0;
    int filled = 0;
    while (filled < 8)
    {
        result = (result << bits) | value;
        filled += bits;
    }
    return static_cast<std::uint8_t>(result >> (filled - 8));
}
} // namespace sr::codec::detail

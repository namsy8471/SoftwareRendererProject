#pragma once
#include <charconv>
#include <concepts>
#include <expected>
#include <string>
#include <string_view>
#include <utility>

namespace sr::asset
{
// 반환된 view는 원본 줄 버퍼를 참조하며 다음 getline 이후 보관하면 안 된다.
[[nodiscard]] constexpr std::string_view trimLeft(std::string_view text) noexcept
{
    const auto first = text.find_first_not_of(" \t\r");
    return first == std::string_view::npos ? std::string_view{} : text.substr(first);
}
// CRLF 파일을 Windows 이외(텍스트 모드가 \r을 남기는 곳)에서 읽어도 줄 끝 \r이 이름에 섞이지 않게 한다.
constexpr void trimLineEnd(std::string &line) noexcept
{
    if (!line.empty() && line.back() == '\r')
        line.pop_back();
}
[[nodiscard]] constexpr std::string_view takeToken(std::string_view &input) noexcept
{
    input = trimLeft(input);
    const auto separator = input.find_first_of(" \t\r");
    const auto token = input.substr(0, separator);
    input = separator == std::string_view::npos ? std::string_view{} : input.substr(separator);
    return token;
}
[[nodiscard]] constexpr auto splitCommand(std::string_view input) noexcept
{
    const auto command = takeToken(input);
    return std::pair{command, trimLeft(input)};
}
template <class Number>
    requires(std::floating_point<Number> || (std::integral<Number> && !std::same_as<Number, bool>))
[[nodiscard]] std::expected<Number, std::string_view> parseNumber(std::string_view token) noexcept
{
    if (token.empty())
        return std::unexpected(token);
    Number value{};
    const auto [end, error] = std::from_chars(token.data(), token.data() + token.size(), value);
    if (error != std::errc{} || end != token.data() + token.size())
        return std::unexpected(token);
    return value;
}
} // namespace sr::asset

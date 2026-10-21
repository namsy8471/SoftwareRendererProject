#include "IO/Json.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <utility>

namespace sr::json
{
const Value *Value::Find(std::string_view key) const noexcept
{
    const Object *object = AsObject();
    if (!object)
        return nullptr;
    const auto found = std::ranges::find(*object, key, &Member::key);
    return found == object->end() ? nullptr : &found->value;
}

Value &Value::Set(std::string_view key, Value value)
{
    if (!AsObject())
        m_data = Object{};
    Object &object = *AsObject();
    const auto found = std::ranges::find(object, key, &Member::key);
    if (found != object.end())
    {
        found->value = std::move(value);
        return found->value;
    }
    object.push_back({std::string(key), std::move(value)});
    return object.back().value;
}

namespace
{
class Parser
{
  public:
    Parser(std::string_view text, ParseOptions options) : m_text(text), m_options(options) {}

    std::expected<Value, ParseError> Run()
    {
        if (m_text.starts_with("\xEF\xBB\xBF"))
            m_position = 3;
        Value root;
        if (!parseValue(root, 0))
            return std::unexpected(m_error);
        skipWhitespace();
        if (m_position != m_text.size())
        {
            fail("unexpected trailing characters");
            return std::unexpected(m_error);
        }
        return root;
    }

  private:
    std::string_view m_text;
    ParseOptions m_options;
    std::size_t m_position = 0;
    ParseError m_error;

    // 첫 오류 위치만 남긴다. 줄·열은 오류가 났을 때만 앞에서부터 센다.
    bool fail(std::string message)
    {
        return failAt(m_position, std::move(message));
    }
    bool failAt(std::size_t position, std::string message)
    {
        m_error.message = std::move(message);
        m_error.line = 1;
        std::size_t lineStart = 0;
        for (std::size_t index = 0; index < position && index < m_text.size(); ++index)
            if (m_text[index] == '\n')
            {
                ++m_error.line;
                lineStart = index + 1;
            }
        m_error.column = position - lineStart + 1;
        return false;
    }

    [[nodiscard]] bool atEnd() const noexcept { return m_position >= m_text.size(); }
    [[nodiscard]] char peek() const noexcept { return atEnd() ? '\0' : m_text[m_position]; }

    void skipWhitespace() noexcept
    {
        while (!atEnd())
        {
            const char c = m_text[m_position];
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
                break;
            ++m_position;
        }
    }

    bool expectLiteral(std::string_view literal)
    {
        if (m_text.substr(m_position, literal.size()) != literal)
            return fail("invalid literal");
        m_position += literal.size();
        return true;
    }

    bool parseValue(Value &out, std::size_t depth)
    {
        if (depth > m_options.maxDepth)
            return fail("nesting too deep");
        skipWhitespace();
        switch (peek())
        {
        case '{':
            return parseObject(out, depth);
        case '[':
            return parseArray(out, depth);
        case '"':
        {
            std::string text;
            if (!parseString(text))
                return false;
            out = Value(std::move(text));
            return true;
        }
        case 't':
            out = Value(true);
            return expectLiteral("true");
        case 'f':
            out = Value(false);
            return expectLiteral("false");
        case 'n':
            out = Value(nullptr);
            return expectLiteral("null");
        default:
            if (peek() == '-' || (peek() >= '0' && peek() <= '9'))
                return parseNumber(out);
            return fail(atEnd() ? "unexpected end of input" : "unexpected character");
        }
    }

    bool parseObject(Value &out, std::size_t depth)
    {
        ++m_position; // '{'
        Value::Object object;
        skipWhitespace();
        if (peek() == '}')
        {
            ++m_position;
            out = Value(std::move(object));
            return true;
        }
        while (true)
        {
            skipWhitespace();
            if (peek() != '"')
                return fail("expected object key");
            const std::size_t keyPosition = m_position;
            std::string key;
            if (!parseString(key))
                return false;
            if (std::ranges::find(object, key, &Member::key) != object.end())
                return failAt(keyPosition, "duplicate key: " + key);
            skipWhitespace();
            if (peek() != ':')
                return fail("expected ':'");
            ++m_position;
            Value value;
            if (!parseValue(value, depth + 1))
                return false;
            object.push_back({std::move(key), std::move(value)});
            skipWhitespace();
            if (peek() == ',')
            {
                ++m_position;
                continue;
            }
            if (peek() == '}')
            {
                ++m_position;
                out = Value(std::move(object));
                return true;
            }
            return fail("expected ',' or '}'");
        }
    }

    bool parseArray(Value &out, std::size_t depth)
    {
        ++m_position; // '['
        Value::Array array;
        skipWhitespace();
        if (peek() == ']')
        {
            ++m_position;
            out = Value(std::move(array));
            return true;
        }
        while (true)
        {
            Value value;
            if (!parseValue(value, depth + 1))
                return false;
            array.push_back(std::move(value));
            skipWhitespace();
            if (peek() == ',')
            {
                ++m_position;
                continue;
            }
            if (peek() == ']')
            {
                ++m_position;
                out = Value(std::move(array));
                return true;
            }
            return fail("expected ',' or ']'");
        }
    }

    // RFC 8259 숫자 문법을 먼저 검사한 뒤 from_chars로 변환한다. from_chars는 "+1", ".5", "01"도
    // 일부 받아들이므로 문법 검사를 생략할 수 없다.
    bool parseNumber(Value &out)
    {
        const std::size_t start = m_position;
        const auto digits = [this]
        {
            const std::size_t first = m_position;
            while (!atEnd() && m_text[m_position] >= '0' && m_text[m_position] <= '9')
                ++m_position;
            return m_position - first;
        };
        if (peek() == '-')
            ++m_position;
        if (peek() == '0')
            ++m_position;
        else if (digits() == 0)
            return fail("invalid number");
        if (peek() == '.')
        {
            ++m_position;
            if (digits() == 0)
                return fail("invalid number fraction");
        }
        if (peek() == 'e' || peek() == 'E')
        {
            ++m_position;
            if (peek() == '+' || peek() == '-')
                ++m_position;
            if (digits() == 0)
                return fail("invalid number exponent");
        }
        double number = 0;
        const char *first = m_text.data() + start;
        const char *last = m_text.data() + m_position;
        const auto [end, error] = std::from_chars(first, last, number);
        if (error == std::errc::result_out_of_range)
            return failAt(start, "number out of range");
        if (error != std::errc{} || end != last)
            return failAt(start, "invalid number");
        out = Value(number);
        return true;
    }

    bool parseHex4(std::uint32_t &out)
    {
        if (m_text.size() - m_position < 4)
            return fail("truncated \\u escape");
        out = 0;
        for (int index = 0; index < 4; ++index)
        {
            const char c = m_text[m_position++];
            out <<= 4;
            if (c >= '0' && c <= '9')
                out |= static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f')
                out |= static_cast<std::uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                out |= static_cast<std::uint32_t>(c - 'A' + 10);
            else
                return failAt(m_position - 1, "invalid \\u escape");
        }
        return true;
    }

    static void appendUtf8(std::string &out, std::uint32_t codePoint)
    {
        if (codePoint < 0x80)
            out += static_cast<char>(codePoint);
        else if (codePoint < 0x800)
        {
            out += static_cast<char>(0xC0 | (codePoint >> 6));
            out += static_cast<char>(0x80 | (codePoint & 0x3F));
        }
        else if (codePoint < 0x10000)
        {
            out += static_cast<char>(0xE0 | (codePoint >> 12));
            out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (codePoint & 0x3F));
        }
        else
        {
            out += static_cast<char>(0xF0 | (codePoint >> 18));
            out += static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (codePoint & 0x3F));
        }
    }

    // 원문 UTF-8 한 글자의 길이. 과잉 길이 표현, 서로게이트, U+10FFFF 초과는 0(오류)이다.
    [[nodiscard]] std::size_t utf8SequenceLength() const noexcept
    {
        const auto byteAt = [this](std::size_t offset)
        { return static_cast<unsigned char>(m_text[m_position + offset]); };
        const std::size_t remaining = m_text.size() - m_position;
        const unsigned char lead = byteAt(0);
        const auto continuation = [&](std::size_t offset)
        { return offset < remaining && (byteAt(offset) & 0xC0) == 0x80; };
        if (lead >= 0xC2 && lead <= 0xDF)
            return continuation(1) ? 2 : 0;
        if (lead >= 0xE0 && lead <= 0xEF)
        {
            if (!continuation(1) || !continuation(2))
                return 0;
            const unsigned char second = byteAt(1);
            if ((lead == 0xE0 && second < 0xA0) || (lead == 0xED && second >= 0xA0))
                return 0;
            return 3;
        }
        if (lead >= 0xF0 && lead <= 0xF4)
        {
            if (!continuation(1) || !continuation(2) || !continuation(3))
                return 0;
            const unsigned char second = byteAt(1);
            if ((lead == 0xF0 && second < 0x90) || (lead == 0xF4 && second >= 0x90))
                return 0;
            return 4;
        }
        return 0;
    }

    bool parseString(std::string &out)
    {
        ++m_position; // '"'
        while (true)
        {
            if (atEnd())
                return fail("unterminated string");
            const char c = m_text[m_position];
            if (c == '"')
            {
                ++m_position;
                return true;
            }
            if (static_cast<unsigned char>(c) < 0x20)
                return fail("control character in string");
            if (static_cast<unsigned char>(c) >= 0x80)
            {
                const std::size_t length = utf8SequenceLength();
                if (length == 0)
                    return fail("invalid UTF-8");
                out.append(m_text.substr(m_position, length));
                m_position += length;
                continue;
            }
            if (c != '\\')
            {
                out += c;
                ++m_position;
                continue;
            }
            ++m_position;
            if (atEnd())
                return fail("unterminated escape");
            const char escape = m_text[m_position++];
            switch (escape)
            {
            case '"':
            case '\\':
            case '/':
                out += escape;
                break;
            case 'b':
                out += '\b';
                break;
            case 'f':
                out += '\f';
                break;
            case 'n':
                out += '\n';
                break;
            case 'r':
                out += '\r';
                break;
            case 't':
                out += '\t';
                break;
            case 'u':
            {
                const std::size_t escapeStart = m_position - 2;
                std::uint32_t codePoint = 0;
                if (!parseHex4(codePoint))
                    return false;
                if (codePoint >= 0xD800 && codePoint <= 0xDBFF)
                {
                    // 서로게이트 쌍은 😀처럼 바로 이어져야 한다.
                    std::uint32_t low = 0;
                    if (m_text.substr(m_position, 2) != "\\u")
                        return failAt(escapeStart, "unpaired surrogate");
                    m_position += 2;
                    if (!parseHex4(low))
                        return false;
                    if (low < 0xDC00 || low > 0xDFFF)
                        return failAt(escapeStart, "unpaired surrogate");
                    codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (low - 0xDC00);
                }
                else if (codePoint >= 0xDC00 && codePoint <= 0xDFFF)
                    return failAt(escapeStart, "unpaired surrogate");
                appendUtf8(out, codePoint);
                break;
            }
            default:
                return failAt(m_position - 1, "invalid escape");
            }
        }
    }
};

class Writer
{
  public:
    explicit Writer(WriteOptions options) : m_options(options) {}

    std::expected<std::string, std::string> Run(const Value &value)
    {
        if (!write(value, 0))
            return std::unexpected(m_error);
        return std::move(m_out);
    }

  private:
    WriteOptions m_options;
    std::string m_out;
    std::string m_error;

    void newline(int depth)
    {
        if (m_options.indent <= 0)
            return;
        m_out += '\n';
        m_out.append(static_cast<std::size_t>(depth) * static_cast<std::size_t>(m_options.indent), ' ');
    }

    void writeString(const std::string &text)
    {
        static constexpr std::array<char, 16> hex{'0', '1', '2', '3', '4', '5', '6', '7',
                                                  '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
        m_out += '"';
        for (const char c : text)
        {
            switch (c)
            {
            case '"':
                m_out += "\\\"";
                break;
            case '\\':
                m_out += "\\\\";
                break;
            case '\n':
                m_out += "\\n";
                break;
            case '\r':
                m_out += "\\r";
                break;
            case '\t':
                m_out += "\\t";
                break;
            case '\b':
                m_out += "\\b";
                break;
            case '\f':
                m_out += "\\f";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20)
                {
                    m_out += "\\u00";
                    m_out += hex[static_cast<unsigned char>(c) >> 4];
                    m_out += hex[static_cast<unsigned char>(c) & 0xF];
                }
                else
                    m_out += c; // UTF-8 바이트는 그대로 쓴다
            }
        }
        m_out += '"';
    }

    bool writeNumber(double number)
    {
        if (!std::isfinite(number))
        {
            m_error = "NaN or infinity cannot be written as JSON";
            return false;
        }
        if (number == 0.0)
            number = 0.0; // -0은 0으로 쓴다
        std::array<char, 32> buffer{};
        const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), number);
        if (error != std::errc{})
        {
            m_error = "number formatting failed";
            return false;
        }
        m_out.append(buffer.data(), end);
        return true;
    }

    bool write(const Value &value, int depth)
    {
        switch (value.GetType())
        {
        case Value::Type::Null:
            m_out += "null";
            return true;
        case Value::Type::Bool:
            m_out += *value.AsBool() ? "true" : "false";
            return true;
        case Value::Type::Number:
            return writeNumber(*value.AsNumber());
        case Value::Type::String:
            writeString(*value.AsString());
            return true;
        case Value::Type::Array:
        {
            const auto &array = *value.AsArray();
            m_out += '[';
            for (std::size_t index = 0; index < array.size(); ++index)
            {
                if (index != 0)
                    m_out += ',';
                newline(depth + 1);
                if (!write(array[index], depth + 1))
                    return false;
            }
            if (!array.empty())
                newline(depth);
            m_out += ']';
            return true;
        }
        case Value::Type::Object:
        {
            const auto &object = *value.AsObject();
            m_out += '{';
            for (std::size_t index = 0; index < object.size(); ++index)
            {
                if (index != 0)
                    m_out += ',';
                newline(depth + 1);
                writeString(object[index].key);
                m_out += m_options.indent > 0 ? ": " : ":";
                if (!write(object[index].value, depth + 1))
                    return false;
            }
            if (!object.empty())
                newline(depth);
            m_out += '}';
            return true;
        }
        }
        return false;
    }
};
} // namespace

std::expected<Value, ParseError> Parse(std::string_view text, ParseOptions options)
{
    return Parser(text, options).Run();
}

std::expected<std::string, std::string> Write(const Value &value, WriteOptions options)
{
    return Writer(options).Run(value);
}
} // namespace sr::json

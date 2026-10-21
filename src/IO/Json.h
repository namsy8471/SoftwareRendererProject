#pragma once

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// 외부 라이브러리 없이 RFC 8259 JSON을 읽고 쓴다. 프로젝트/씬 파일과 glTF가 같은 파서를 쓴다.
// 입력은 사용자가 끌어다 놓은 임의 파일일 수 있으므로 모든 오류를 줄·열과 함께 돌려주고,
// 깊이 제한으로 스택 넘침을 막는다. 객체는 키 순서를 보존해 같은 값이면 항상 같은 텍스트로 쓴다.
namespace sr::json
{
struct Member;

class Value
{
  public:
    using Array = std::vector<Value>;
    using Object = std::vector<Member>; // 키 중복 없음(파서와 Set이 보장), 입력/삽입 순서 유지

    enum class Type
    {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };

    Value() noexcept = default;
    Value(std::nullptr_t) noexcept {}
    Value(bool value) noexcept : m_data(value) {}
    Value(double value) noexcept : m_data(value) {}
    // int/float를 bool이나 다른 오버로드로 바꾸지 않도록 숫자는 모두 double로 받는다.
    Value(int value) noexcept : m_data(static_cast<double>(value)) {}
    Value(float value) noexcept : m_data(static_cast<double>(value)) {}
    Value(std::string value) : m_data(std::move(value)) {}
    Value(std::string_view value) : m_data(std::string(value)) {}
    Value(const char *value) : m_data(std::string(value)) {}
    Value(Array value) : m_data(std::move(value)) {}
    Value(Object value) : m_data(std::move(value)) {}

    [[nodiscard]] Type GetType() const noexcept { return static_cast<Type>(m_data.index()); }
    [[nodiscard]] bool IsNull() const noexcept { return GetType() == Type::Null; }

    // 타입이 다르면 nullptr이다. 호출자는 파일 형식 오류를 직접 보고한다.
    [[nodiscard]] const bool *AsBool() const noexcept { return std::get_if<bool>(&m_data); }
    [[nodiscard]] const double *AsNumber() const noexcept { return std::get_if<double>(&m_data); }
    [[nodiscard]] const std::string *AsString() const noexcept { return std::get_if<std::string>(&m_data); }
    [[nodiscard]] const Array *AsArray() const noexcept { return std::get_if<Array>(&m_data); }
    [[nodiscard]] Array *AsArray() noexcept { return std::get_if<Array>(&m_data); }
    [[nodiscard]] const Object *AsObject() const noexcept { return std::get_if<Object>(&m_data); }
    [[nodiscard]] Object *AsObject() noexcept { return std::get_if<Object>(&m_data); }

    // 객체가 아니거나 키가 없으면 nullptr. 선형 검색이며 씬 파일 크기에서는 충분하다.
    [[nodiscard]] const Value *Find(std::string_view key) const noexcept;
    // 객체에 키를 넣거나 바꾼다. 객체가 아니면 빈 객체로 바꾼 뒤 넣는다.
    Value &Set(std::string_view key, Value value);

    friend bool operator==(const Value &, const Value &) = default;

  private:
    std::variant<std::nullptr_t, bool, double, std::string, Array, Object> m_data{nullptr};
};

struct Member
{
    std::string key;
    Value value;

    friend bool operator==(const Member &, const Member &) = default;
};

struct ParseError
{
    std::string message;
    std::size_t line = 1;   // 1부터
    std::size_t column = 1; // 1부터, UTF-8 바이트 단위
};

struct ParseOptions
{
    // 파서는 중첩 한 단계마다 재귀한다. Win32 Debug(1MB 스택, 최적화 없음)에서는 깊이 수백이면 스택을
    // 넘칠 수 있으므로 128 이하로 둔다. 씬 파일과 glTF는 10단계 안쪽이다.
    std::size_t maxDepth = 64;
};

// 앞의 UTF-8 BOM은 건너뛴다. 잘못된 UTF-8, 중복 키, 제어 문자, 뒤에 남은 문자는 오류다.
[[nodiscard]] std::expected<Value, ParseError> Parse(std::string_view text, ParseOptions options = {});

struct WriteOptions
{
    int indent = 2; // 0이면 한 줄로 쓴다
};

// NaN/Inf는 JSON에 없으므로 쓰지 않고 오류를 돌려준다. 숫자는 왕복 가능한 최단 표현이다.
[[nodiscard]] std::expected<std::string, std::string> Write(const Value &value, WriteOptions options = {});
} // namespace sr::json

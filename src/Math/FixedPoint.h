#pragma once
#include <cstddef>
#include <cstdint>

namespace SRMath
{
// 정수부와 부호 비트를 남겨 두기 위해 소수 비트 수를 제한한다.
template <std::size_t FRACT_BIT>
    requires(FRACT_BIT > 0 && FRACT_BIT < 31)
struct FixedPoint
{
    static constexpr std::int64_t scale = std::int64_t{1} << FRACT_BIT;
    std::int32_t value = 0;

    constexpr FixedPoint() noexcept = default;
    explicit constexpr FixedPoint(std::int32_t rawValue) noexcept : value(rawValue) {}
    explicit constexpr FixedPoint(float input) noexcept
        : value(static_cast<std::int32_t>(input * static_cast<float>(scale)))
    {
    }
    explicit constexpr FixedPoint(double input) noexcept
        : value(static_cast<std::int32_t>(input * static_cast<double>(scale)))
    {
    }

    [[nodiscard]] constexpr float toFloat() const noexcept
    {
        return static_cast<float>(value) / static_cast<float>(scale);
    }

    [[nodiscard]] constexpr double toDouble() const noexcept
    {
        return static_cast<double>(value) / static_cast<double>(scale);
    }

    constexpr FixedPoint &operator+=(const FixedPoint &other) noexcept
    {
        value += other.value;
        return *this;
    }

    constexpr FixedPoint &operator-=(const FixedPoint &other) noexcept
    {
        value -= other.value;
        return *this;
    }

    constexpr FixedPoint &operator*=(const FixedPoint &other) noexcept
    {
        const std::int64_t product = static_cast<std::int64_t>(value) * other.value;
        value = static_cast<std::int32_t>(product >> FRACT_BIT);
        return *this;
    }

    constexpr FixedPoint &operator/=(const FixedPoint &other) noexcept
    {
        const std::int64_t numerator = static_cast<std::int64_t>(value) << FRACT_BIT;
        value = static_cast<std::int32_t>(numerator / other.value);
        return *this;
    }

    constexpr FixedPoint &operator*=(float scalar) noexcept
    {
        value = static_cast<std::int32_t>(static_cast<float>(value) * scalar);
        return *this;
    }

    constexpr FixedPoint &operator/=(float scalar) noexcept
    {
        value = static_cast<std::int32_t>(static_cast<float>(value) / scalar);
        return *this;
    }
};

template <std::size_t fractBits>
[[nodiscard]] constexpr FixedPoint<fractBits> floatToFixed(float value) noexcept
{
    return FixedPoint<fractBits>{value};
}

template <std::size_t fractBits>
[[nodiscard]] constexpr float fixedToFloat(FixedPoint<fractBits> value) noexcept
{
    return value.toFloat();
}

using Fixed8 = FixedPoint<8>;
using Fixed16 = FixedPoint<16>;
using Fixed24 = FixedPoint<24>;
using Fixed30 = FixedPoint<30>;

template <std::size_t FRACT_BIT>
[[nodiscard]] constexpr FixedPoint<FRACT_BIT> operator+(FixedPoint<FRACT_BIT> lhs,
                                                        const FixedPoint<FRACT_BIT> &rhs) noexcept
{
    lhs += rhs;
    return lhs;
}

template <std::size_t FRACT_BIT>
[[nodiscard]] constexpr FixedPoint<FRACT_BIT> operator-(FixedPoint<FRACT_BIT> lhs,
                                                        const FixedPoint<FRACT_BIT> &rhs) noexcept
{
    lhs -= rhs;
    return lhs;
}

template <std::size_t FRACT_BIT>
[[nodiscard]] constexpr FixedPoint<FRACT_BIT> operator*(FixedPoint<FRACT_BIT> lhs,
                                                        const FixedPoint<FRACT_BIT> &rhs) noexcept
{
    lhs *= rhs;
    return lhs;
}

template <std::size_t FRACT_BIT>
[[nodiscard]] constexpr FixedPoint<FRACT_BIT> operator*(FixedPoint<FRACT_BIT> lhs, float scalar) noexcept
{
    lhs *= scalar;
    return lhs;
}

template <std::size_t FRACT_BIT>
[[nodiscard]] constexpr FixedPoint<FRACT_BIT> operator/(FixedPoint<FRACT_BIT> lhs,
                                                        const FixedPoint<FRACT_BIT> &rhs) noexcept
{
    lhs /= rhs;
    return lhs;
}

template <std::size_t FRACT_BIT>
[[nodiscard]] constexpr FixedPoint<FRACT_BIT> operator/(FixedPoint<FRACT_BIT> lhs, float scalar) noexcept
{
    lhs /= scalar;
    return lhs;
}

// 지원하지 않는 차원은 템플릿 본문 안의 오류가 아니라 호출 지점에서
} // namespace SRMath

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <expected>
#include <type_traits>
#include <xmmintrin.h> // SSE Header
#include <smmintrin.h> // SSE4.1
#include <cstdint>

namespace SRMath
{
template <std::size_t N>
concept SupportedVectorDimension = N >= 2 && N <= 4;

template <std::size_t N>
    requires SupportedVectorDimension<N>
struct alignas(16) Vector;

using vec4 = Vector<4>;
using vec3 = Vector<3>;
using vec2 = Vector<2>;
using Color = Vector<3>;  // RGB Color
using Color4 = Vector<4>; // RGBA Color

// x/y/z/w와 __m128이 같은 16-byte 저장소를 본다. SIMD ABI와 정렬을
// 유지하기 위한 배열이므로 일반 컨테이너로 치환하지 않는다.
#ifdef _MSC_VER
// 익명 성분 구조체는 MSVC 확장이므로 해당 세 타입 안에서만 경고를 제한한다.
#pragma warning(push)
#pragma warning(disable : 4201)
#endif
// Vector2
template <> struct alignas(16) Vector<2>
{
    using value_type = float;
    static constexpr size_t dimension = 2;

    union
    {
        struct
        {
            float x, y;
        };
        struct
        {
            float u, v;
        }; // For texture coordinates;
        float data[4]; // padding 8 bytes for m128
        __m128 m128;
    };

    Vector() noexcept : m128(_mm_setzero_ps()) {}
    explicit Vector(const __m128 v) noexcept : m128(v) {}
    explicit Vector(float x) noexcept : m128(_mm_set_ps(0.0f, 0.f, x, x)) {}
    Vector(float x, float y) noexcept : m128(_mm_set_ps(0.0f, 0.0f, y, x)) {}

    float &operator[](std::size_t index) noexcept { return data[index]; }
    const float &operator[](std::size_t index) const noexcept { return data[index]; }

    Vector<2> &operator+=(const Vector<2> &other)
    {
        // SIMD
        this->m128 = _mm_add_ps(this->m128, other.m128);
        return *this;
    }

    Vector<2> &operator-=(const Vector<2> &other)
    {
        this->m128 = _mm_sub_ps(this->m128, other.m128);
        return *this;
    }

    Vector<2> &operator*=(float scalar)
    {
        __m128 s = _mm_set1_ps(scalar);
        this->m128 = _mm_mul_ps(this->m128, s);
        return *this;
    }

    Vector<2> &operator*=(const Vector<2> &other)
    {
        this->m128 = _mm_mul_ps(this->m128, other.m128);
        return *this;
    }

    Vector<2> &operator/=(float scalar) noexcept
    {
        const float reciprocalScalar = 1.0f / scalar;
        const __m128 reciprocal = _mm_set1_ps(reciprocalScalar);
        this->m128 = _mm_mul_ps(this->m128, reciprocal);
        return *this;
    }

    [[nodiscard]] bool operator==(const Vector<2> &other) const noexcept
    {
        // Compare each component of the vectors
        __m128 cmp = _mm_cmpeq_ps(this->m128, other.m128);

        // Check if all components are equal
        // x, y lane만 본다. padding lane(z, w)은 비교 대상이 아니다.
        return (_mm_movemask_ps(cmp) & 0x3) == 0x3;
    }

    [[nodiscard]] bool operator!=(const Vector<2> &other) const noexcept { return !(*this == other); }

    Vector<2> &clamp(float min, float max) noexcept
    {
        const __m128 min_vec = _mm_set1_ps(min);
        const __m128 max_vec = _mm_set1_ps(max);

        this->m128 = _mm_max_ps(min_vec, _mm_min_ps(max_vec, this->m128));

        return *this;
    }
};

// Vector3
template <> struct alignas(16) Vector<3>
{
    using value_type = float;
    static constexpr size_t dimension = 3;

    union
    {
        struct
        {
            float x, y, z;
        };
        struct
        {
            float r, g, b;
        }; // For RGB Color
        float data[4]; // padding 4 bytes for m128
        __m128 m128;
    };

    Vector() noexcept : m128(_mm_setzero_ps()) {}
    explicit Vector(const __m128 v) noexcept : m128(v) {}
    explicit Vector(float x) noexcept : m128(_mm_set_ps(0.0f, x, x, x)) {}
    Vector(float x, float y, float z) noexcept : m128(_mm_set_ps(0.0f, z, y, x)) {}
    explicit Vector(const SRMath::Vector<2> &v) noexcept : m128(_mm_set_ps(0.0f, 0.0f, v.y, v.x)) {}
    Vector(const SRMath::Vector<2> &v, float z) noexcept : m128(_mm_set_ps(0.0f, z, v.y, v.x)) {}
    Vector(const SRMath::Vector<3> &) noexcept = default;
    explicit Vector(const SRMath::Vector<4> &v) noexcept;

    float &operator[](std::size_t index) noexcept { return data[index]; }
    const float &operator[](std::size_t index) const noexcept { return data[index]; }

    Vector &operator+=(const Vector<3> &other)
    {
        // SIMD
        this->m128 = _mm_add_ps(this->m128, other.m128);
        return *this;
    }

    Vector &operator-=(const Vector<3> &other)
    {
        this->m128 = _mm_sub_ps(this->m128, other.m128);
        return *this;
    }

    Vector &operator*=(float scalar)
    {
        __m128 s = _mm_set1_ps(scalar);
        this->m128 = _mm_mul_ps(this->m128, s);
        return *this;
    }

    Vector &operator*=(const Vector<3> &other)
    {
        this->m128 = _mm_mul_ps(this->m128, other.m128);
        return *this;
    }

    Vector &operator/=(float scalar) noexcept
    {
        const float reciprocalScalar = 1.0f / scalar;
        const __m128 reciprocal = _mm_set1_ps(reciprocalScalar);
        this->m128 = _mm_mul_ps(this->m128, reciprocal);
        return *this;
    }

    [[nodiscard]] bool operator==(const Vector &other) const noexcept
    {
        // Compare each component of the vectors
        __m128 cmp = _mm_cmpeq_ps(this->m128, other.m128);

        // Check if all components are equal
        // x, y, z lane만 본다. padding lane(w)은 비교 대상이 아니다.
        return (_mm_movemask_ps(cmp) & 0x7) == 0x7;
    }

    [[nodiscard]] bool operator!=(const Vector &other) const noexcept { return !(*this == other); }

    Vector &clamp(float min, float max) noexcept
    {
        const __m128 min_vec = _mm_set1_ps(min);
        const __m128 max_vec = _mm_set1_ps(max);

        this->m128 = _mm_max_ps(min_vec, _mm_min_ps(max_vec, this->m128));

        return *this;
    }
};

// Vector4
template <> struct alignas(16) Vector<4>
{
    using value_type = float;
    static constexpr size_t dimension = 4;

    union
    {
        struct
        {
            float x, y, z, w;
        };
        struct
        {
            float r, g, b, a;
        }; // For RGBA Color
        float data[4];
        __m128 m128;
    };

    Vector() noexcept : m128(_mm_setzero_ps()) {}
    explicit Vector(const __m128 v) noexcept : m128(v) {}
    explicit Vector(float x) noexcept : m128(_mm_set_ps(x, x, x, x)) {}
    Vector(float x, float y, float z, float w) noexcept : m128(_mm_set_ps(w, z, y, x)) {}
    Vector(const SRMath::Vector<2> &v, float w) noexcept : m128(_mm_set_ps(w, 0.f, v.y, v.x)) {}
    explicit Vector(const SRMath::Vector<2> &v) noexcept : m128(_mm_set_ps(1.0f, 0.f, v.y, v.x)) {}
    Vector(const SRMath::Vector<3> &v, float w) noexcept : m128(_mm_insert_ps(v.m128, _mm_set_ss(w), 0x30)) {}
    explicit Vector(const SRMath::Vector<3> &v) noexcept : m128(_mm_insert_ps(v.m128, _mm_set_ss(1.0f), 0x30)) {}

    float &operator[](std::size_t index) noexcept { return data[index]; }
    const float &operator[](std::size_t index) const noexcept { return data[index]; }

    Vector<4> &operator+=(const Vector<4> &other)
    {
        // SIMD
        this->m128 = _mm_add_ps(this->m128, other.m128);
        return *this;
    }

    Vector<4> &operator-=(const Vector<4> &other)
    {
        this->m128 = _mm_sub_ps(this->m128, other.m128);
        return *this;
    }

    Vector<4> &operator*=(float scalar)
    {
        __m128 s = _mm_set1_ps(scalar);
        this->m128 = _mm_mul_ps(this->m128, s);
        return *this;
    }

    Vector<4> &operator*=(const Vector<4> &other)
    {
        this->m128 = _mm_mul_ps(this->m128, other.m128);
        return *this;
    }

    Vector<4> &operator/=(float scalar) noexcept
    {
        const float reciprocalScalar = 1.0f / scalar;
        const __m128 reciprocal = _mm_set1_ps(reciprocalScalar);
        this->m128 = _mm_mul_ps(this->m128, reciprocal);
        return *this;
    }

    [[nodiscard]] bool operator==(const Vector<4> &other) const noexcept
    {
        // Compare each component of the vectors
        __m128 cmp = _mm_cmpeq_ps(this->m128, other.m128);

        // Check if all components are equal
        return _mm_movemask_ps(cmp) == 0xF; // All bits set means all components are equal
    }

    [[nodiscard]] bool operator!=(const Vector<4> &other) const noexcept { return !(*this == other); }

    Vector<4> &clamp(float min, float max) noexcept
    {
        const __m128 min_vec = _mm_set1_ps(min);
        const __m128 max_vec = _mm_set1_ps(max);

        this->m128 = _mm_max_ps(min_vec, _mm_min_ps(max_vec, this->m128));

        return *this;
    }
};

inline Vector<3>::Vector(const Vector<4> &v) noexcept
    : m128(_mm_insert_ps(v.m128, v.m128, 0x08)) // w(lane 3)는 0으로 설정
{
}
#ifdef _MSC_VER
#pragma warning(pop)
#endif
static_assert(std::is_trivially_copyable_v<vec2> && std::is_trivially_copyable_v<vec3> &&
                  std::is_trivially_copyable_v<vec4>,
              "SRMath vector types must be trivially copyable for SIMD register passing and fast copies");

// inline operator overloading function
template <std::size_t N>
    requires SupportedVectorDimension<N>
[[nodiscard]] inline Vector<N> operator+(Vector<N> lhs, const Vector<N> &rhs) noexcept
{
    lhs += rhs;
    return lhs;
}

template <std::size_t N>
    requires SupportedVectorDimension<N>
[[nodiscard]] inline Vector<N> operator-(Vector<N> lhs, const Vector<N> &rhs) noexcept
{
    lhs -= rhs;
    return lhs;
}

template <std::size_t N>
    requires SupportedVectorDimension<N>
[[nodiscard]] inline Vector<N> operator*(Vector<N> value, float scalar) noexcept
{
    value *= scalar;
    return value;
}

template <std::size_t N>
    requires SupportedVectorDimension<N>
[[nodiscard]] inline Vector<N> operator*(float scalar, Vector<N> value) noexcept
{
    value *= scalar;
    return value;
}

template <std::size_t N>
    requires SupportedVectorDimension<N>
[[nodiscard]] inline Vector<N> operator*(Vector<N> lhs, const Vector<N> &rhs) noexcept
{
    lhs *= rhs;
    return lhs;
}

template <std::size_t N>
    requires SupportedVectorDimension<N>
[[nodiscard]] inline Vector<N> operator/(Vector<N> value, float scalar) noexcept
{
    value /= scalar;
    return value;
}

template <std::size_t N>
    requires SupportedVectorDimension<N>
[[nodiscard]] inline bool operator==(const Vector<N> &lhs, const Vector<N> &rhs) noexcept
{
    return lhs.operator==(rhs);
}

template <std::size_t N>
    requires SupportedVectorDimension<N>
[[nodiscard]] inline bool operator!=(const Vector<N> &lhs, const Vector<N> &rhs) noexcept
{
    return !lhs.operator==(rhs);
}

// inline utility functions (dot product, cross product)
[[nodiscard]] inline float dot(const vec4 &a, const vec4 &b) noexcept
{
    __m128 ret = _mm_dp_ps(a.m128, b.m128, 0xF1);

    return _mm_cvtss_f32(ret);
}

[[nodiscard]] inline float dot(const vec3 &a, const vec3 &b) noexcept
{
    __m128 ret = _mm_dp_ps(a.m128, b.m128, 0x71);

    return _mm_cvtss_f32(ret);
}

[[nodiscard]] inline float dot(const vec2 &a, const vec2 &b) noexcept
{
    __m128 ret = _mm_dp_ps(a.m128, b.m128, 0x31);

    return _mm_cvtss_f32(ret);
}

// Get Cross Product of Vector
[[nodiscard]] inline vec3 cross(const vec3 &a, const vec3 &b) noexcept
{
    const __m128 a_shuf1 = _mm_shuffle_ps(a.m128, a.m128, _MM_SHUFFLE(3, 0, 2, 1));
    const __m128 b_shuf1 = _mm_shuffle_ps(b.m128, b.m128, _MM_SHUFFLE(3, 1, 0, 2));

    const __m128 term1 = _mm_mul_ps(a_shuf1, b_shuf1);

    const __m128 a_shuf2 = _mm_shuffle_ps(a.m128, a.m128, _MM_SHUFFLE(3, 1, 0, 2));
    const __m128 b_shuf2 = _mm_shuffle_ps(b.m128, b.m128, _MM_SHUFFLE(3, 0, 2, 1));

    const __m128 term2 = _mm_mul_ps(a_shuf2, b_shuf2);

    vec3 ret;
    ret.m128 = _mm_sub_ps(term1, term2);

    return ret;
}

[[nodiscard]] inline vec3 cross(const vec2 &a, const vec2 &b) noexcept
{
    vec3 newA, newB;
    newA.m128 = a.m128;
    newB.m128 = b.m128;

    return cross(newA, newB);
}

// Get power of Length of Vector to compare length with the other Vector
template <std::size_t N>
    requires SupportedVectorDimension<N>
[[nodiscard]] inline float lengthSq(const Vector<N> &v) noexcept
{
    return dot(v, v);
}

// Get Length of Vector
template <std::size_t N>
    requires SupportedVectorDimension<N>
[[nodiscard]] inline float length(const Vector<N> &v) noexcept
{
    return std::sqrt(lengthSq(v));
}

// Normalize Vector
template <std::size_t N>
    requires SupportedVectorDimension<N>
[[nodiscard]] inline Vector<N> normalize(const Vector<N> &v) noexcept
{
    const float vectorLength = length(v);
    if (vectorLength < 1e-5f)
        return v;

    return v / vectorLength;
}

[[nodiscard]] inline vec3 reflect(const vec3 &incident, const vec3 &normal) noexcept
{
    return incident - normal * (2.0f * dot(normal, incident));
}

template <std::size_t N>
    requires SupportedVectorDimension<N>
[[nodiscard]] inline Vector<N> clamp(Vector<N> value, float minimum, float maximum) noexcept
{
    value.clamp(minimum, maximum);
    return value;
}
} // namespace SRMath

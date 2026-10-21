#pragma once

#include <cmath>
#include "Math/Matrix.h"
#include "Math/Vector.h"

namespace SRMath
{
// 단위 쿼터니언 회전. 저장 순서는 glTF와 같은 (x, y, z, w)이고 w가 실수부다.
// 회전 방향과 행렬은 SRMath::rotate(축 * 각도)와 같은 오른손 규약이다: from_axis_angle(a, t)의 to_mat4()는
// rotate(a * t)와 같은 행렬이다. 곱 a * b는 "b를 먼저, a를 나중에" 적용한다(행렬 곱과 같은 순서).
struct quat
{
    float x = 0.f, y = 0.f, z = 0.f, w = 1.f;

    [[nodiscard]] static constexpr quat identity() noexcept { return {}; }
};

[[nodiscard]] inline quat operator*(const quat &a, const quat &b) noexcept
{
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

[[nodiscard]] inline float dot(const quat &a, const quat &b) noexcept
{
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

[[nodiscard]] inline quat conjugate(const quat &q) noexcept { return {-q.x, -q.y, -q.z, q.w}; }

// 길이가 0이거나 유한하지 않으면 단위 쿼터니언을 돌려준다(손상된 키프레임이 NaN 행렬을 만들지 않게).
[[nodiscard]] inline quat normalize(const quat &q) noexcept
{
    const float lengthSquared = dot(q, q);
    if (!(lengthSquared > 1e-24f) || !std::isfinite(lengthSquared))
        return quat::identity();
    const float inverse = 1.f / std::sqrt(lengthSquared);
    return {q.x * inverse, q.y * inverse, q.z * inverse, q.w * inverse};
}

// axis는 정규화하지 않아도 된다. 길이가 0이면 단위 회전이다.
[[nodiscard]] inline quat from_axis_angle(const vec3 &axis, float angleInRadians) noexcept
{
    const float axisLength = length(axis);
    if (axisLength < 1e-12f)
        return quat::identity();
    const float s = std::sin(angleInRadians * 0.5f) / axisLength;
    return {axis.x * s, axis.y * s, axis.z * s, std::cos(angleInRadians * 0.5f)};
}

// GameObject 회전 표현(축 * 각도, 라디안)에서 만든다. SRMath::rotate와 같은 회전이다.
[[nodiscard]] inline quat from_rotation_vector(const vec3 &rotationVector) noexcept
{
    return from_axis_angle(rotationVector, length(rotationVector));
}

// 위와 반대 방향. 각도는 [0, pi]로 고른다(q와 -q는 같은 회전).
[[nodiscard]] inline vec3 to_rotation_vector(const quat &rotation) noexcept
{
    quat q = normalize(rotation);
    if (q.w < 0.f)
        q = {-q.x, -q.y, -q.z, -q.w};
    const float sinHalf = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
    if (sinHalf < 1e-7f)
        return {0.f, 0.f, 0.f};
    const float factor = 2.f * std::atan2(sinHalf, q.w) / sinHalf;
    return {q.x * factor, q.y * factor, q.z * factor};
}

// 단위 쿼터니언의 회전 행렬(열 우선, 평행이동 0). 정규화는 호출자가 보장한다.
[[nodiscard]] inline mat4 to_mat4(const quat &q) noexcept
{
    const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    mat4 result(1.f);
    result[0] = vec4(1.f - 2.f * (yy + zz), 2.f * (xy + wz), 2.f * (xz - wy), 0.f);
    result[1] = vec4(2.f * (xy - wz), 1.f - 2.f * (xx + zz), 2.f * (yz + wx), 0.f);
    result[2] = vec4(2.f * (xz + wy), 2.f * (yz - wx), 1.f - 2.f * (xx + yy), 0.f);
    return result;
}

[[nodiscard]] inline vec3 rotate(const quat &q, const vec3 &v) noexcept
{
    // v' = v + 2w(u x v) + 2u x (u x v), u = 벡터부
    const vec3 u(q.x, q.y, q.z);
    const vec3 t = cross(u, v) * 2.f;
    return v + t * q.w + cross(u, t);
}

// 정규화한 선형 보간. 짧은 쪽 호를 따른다. slerp보다 싸지만 각속도가 일정하지 않다.
[[nodiscard]] inline quat nlerp(const quat &a, const quat &b, float t) noexcept
{
    const float sign = dot(a, b) < 0.f ? -1.f : 1.f;
    const float s = 1.f - t, u = t * sign;
    return normalize({a.x * s + b.x * u, a.y * s + b.y * u, a.z * s + b.z * u, a.w * s + b.w * u});
}

// 구면 선형 보간. 짧은 쪽 호를 따르고, 두 회전이 거의 같으면(sin이 0에 가까우면) nlerp로 바꾼다.
[[nodiscard]] inline quat slerp(const quat &a, const quat &b, float t) noexcept
{
    float cosTheta = dot(a, b);
    quat target = b;
    if (cosTheta < 0.f)
    {
        cosTheta = -cosTheta;
        target = {-b.x, -b.y, -b.z, -b.w};
    }
    if (cosTheta > 0.9995f)
        return nlerp(a, target, t);
    const float theta = std::acos(cosTheta);
    const float inverseSin = 1.f / std::sin(theta);
    const float s = std::sin((1.f - t) * theta) * inverseSin;
    const float u = std::sin(t * theta) * inverseSin;
    return normalize({a.x * s + target.x * u, a.y * s + target.y * u, a.z * s + target.z * u,
                      a.w * s + target.w * u});
}
} // namespace SRMath

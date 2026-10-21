#include "Animation/AnimationSampling.h"
#include "Math/Quaternion.h"

#include <algorithm>
#include <cstddef>

namespace sr::anim
{
namespace
{
using sr::gltf::AnimationPath;
using sr::gltf::Interpolation;

std::array<float, 4> readKey(const sr::gltf::AnimationTrack &track, std::size_t key, std::size_t components,
                             std::size_t slot) noexcept
{
    // CUBICSPLINE은 키마다 (in-tangent, value, out-tangent) 세 덩어리다. slot은 그 안의 위치다.
    const std::size_t perKey = track.interpolation == Interpolation::CubicSpline ? 3 : 1;
    const std::size_t base = (key * perKey + slot) * components;
    std::array<float, 4> value{};
    for (std::size_t i = 0; i < components; ++i)
        value[i] = track.values[base + i];
    return value;
}

std::array<float, 4> normalized(const std::array<float, 4> &value) noexcept
{
    const SRMath::quat q = SRMath::normalize(SRMath::quat{value[0], value[1], value[2], value[3]});
    return {q.x, q.y, q.z, q.w};
}
} // namespace

std::array<float, 4> SampleTrack(const sr::gltf::AnimationTrack &track, float time) noexcept
{
    const std::size_t keys = track.times.size();
    if (keys == 0 || track.componentCount <= 0 || track.componentCount > 4)
        return {};
    const auto components = static_cast<std::size_t>(track.componentCount);
    const std::size_t perKey = track.interpolation == Interpolation::CubicSpline ? 3 : 1;
    if (track.values.size() < keys * perKey * components)
        return {};
    const bool rotation = track.path == AnimationPath::Rotation && components == 4;
    const std::size_t valueSlot = track.interpolation == Interpolation::CubicSpline ? 1 : 0;

    // 첫 키 이전·마지막 키 이후는 끝값으로 고정한다. NaN 시간도 첫 키로 간다.
    if (!(time > track.times.front()) || keys == 1)
    {
        const auto value = readKey(track, 0, components, valueSlot);
        return rotation ? normalized(value) : value;
    }
    if (time >= track.times.back())
    {
        const auto value = readKey(track, keys - 1, components, valueSlot);
        return rotation ? normalized(value) : value;
    }

    // times[k] <= time < times[k + 1]
    const auto upper = std::upper_bound(track.times.begin(), track.times.end(), time);
    const auto next = static_cast<std::size_t>(upper - track.times.begin());
    const std::size_t key = next - 1;
    const float t0 = track.times[key], t1 = track.times[next];
    const float span = t1 - t0;
    const float u = span > 0.f ? (time - t0) / span : 0.f;

    switch (track.interpolation)
    {
    case Interpolation::Step:
    {
        const auto value = readKey(track, key, components, 0);
        return rotation ? normalized(value) : value;
    }
    case Interpolation::Linear:
    {
        const auto a = readKey(track, key, components, 0);
        const auto b = readKey(track, next, components, 0);
        if (rotation)
        {
            const SRMath::quat q = SRMath::slerp(SRMath::normalize(SRMath::quat{a[0], a[1], a[2], a[3]}),
                                                 SRMath::normalize(SRMath::quat{b[0], b[1], b[2], b[3]}), u);
            return {q.x, q.y, q.z, q.w};
        }
        std::array<float, 4> value{};
        for (std::size_t i = 0; i < components; ++i)
            value[i] = a[i] + (b[i] - a[i]) * u;
        return value;
    }
    case Interpolation::CubicSpline:
    {
        // p(u) = (2u³-3u²+1)v0 + (u³-2u²+u)·span·b0 + (-2u³+3u²)v1 + (u³-u²)·span·a1
        // b0 = 이 키의 out-tangent, a1 = 다음 키의 in-tangent.
        const auto v0 = readKey(track, key, components, 1);
        const auto b0 = readKey(track, key, components, 2);
        const auto a1 = readKey(track, next, components, 0);
        const auto v1 = readKey(track, next, components, 1);
        const float u2 = u * u, u3 = u2 * u;
        const float h00 = 2.f * u3 - 3.f * u2 + 1.f;
        const float h10 = (u3 - 2.f * u2 + u) * span;
        const float h01 = -2.f * u3 + 3.f * u2;
        const float h11 = (u3 - u2) * span;
        std::array<float, 4> value{};
        for (std::size_t i = 0; i < components; ++i)
            value[i] = h00 * v0[i] + h10 * b0[i] + h01 * v1[i] + h11 * a1[i];
        return rotation ? normalized(value) : value;
    }
    }
    return {};
}
} // namespace sr::anim

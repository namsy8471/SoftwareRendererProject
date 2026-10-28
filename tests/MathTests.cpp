#include "Math/SRMath.h"
#include "Math/SIMD.h"
#include "Math/AABB.h"
#include "Math/Frustum.h"
#include "Utils/FixedCapacityVector.h"
#include <array>
#include <bit>
#include <iostream>
#include <random>
#include <type_traits>

#include "TestHarness.h"
#include "TestSupport.h"

using namespace sr::test;

namespace
{
void frustumSelectionTests()
{
    // SIMD 선택과 독립인 이전 scalar-if 기준이다. dot의 덧셈 순서나 평면 순서,
    // distance < 0의 NaN 보수적 결과는 바꾸지 않고 P-vertex 선택만 비교한다.
    const auto positiveVertex = [](const Plane &plane, const AABB &box)
    {
        SRMath::vec3 point = box.min;
        if (plane.normal.x >= 0.f)
            point.x = box.max.x;
        if (plane.normal.y >= 0.f)
            point.y = box.max.y;
        if (plane.normal.z >= 0.f)
            point.z = box.max.z;
        return point;
    };
    const auto scalarReference = [&](const Frustum &frustum, const AABB &box)
    {
        for (const auto &plane : frustum.planes)
            if (SRMath::dot(plane.normal, positiveVertex(plane, box)) + plane.distance < 0.f)
                return false;
        return true;
    };
    const auto compare = [&](const Frustum &frustum, const AABB &box)
    {
        const bool expected = scalarReference(frustum, box);
        check(frustum.IsAABBInFrustum(box) == expected,
              "frustum SIMD vertex selection matches old scalar ifs");
        auto dirtyFrustum = frustum;
        auto dirtyBox = box;
        dirtyBox.min.data[3] = std::bit_cast<float>(std::uint32_t{0x7FC01234});
        dirtyBox.max.data[3] = -std::numeric_limits<float>::infinity();
        for (std::size_t i = 0; i < dirtyFrustum.planes.size(); ++i)
            dirtyFrustum.planes[i].normal.data[3] = i % 2 == 0
                                                        ? std::numeric_limits<float>::infinity()
                                                        : std::bit_cast<float>(std::uint32_t{0xFFC05678});
        check(scalarReference(dirtyFrustum, dirtyBox) == expected &&
                  dirtyFrustum.IsAABBInFrustum(dirtyBox) == expected,
              "frustum comparison ignores dirty vec3 fourth lanes including NaN and infinity");
        return expected;
    };
    const auto accepting = []
    {
        Frustum frustum;
        for (auto &plane : frustum.planes)
        {
            plane.normal = {0.f, 0.f, 0.f};
            plane.distance = 1.f;
        }
        return frustum;
    };
    AABB box;
    box.min = {-1.f, -2.f, -3.f};
    box.max = {2.f, 3.f, 4.f};
    {
        struct RestoreMxcsr
        {
            unsigned int value = _mm_getcsr();
            ~RestoreMxcsr() { _mm_setcsr(value); }
        } restore;
        // unused w를 비교해도 bool은 같을 수 있다. QNaN/SNaN의 invalid 상태까지
        // 검사하되 예외 mask/DAZ/FTZ는 건드리지 않고 어떤 종료 경로든 원래 MXCSR를 복구한다.
        for (const auto bits : {std::uint32_t{0x7FC01234}, std::uint32_t{0x7F801234}})
        {
            auto dirtyFrustum = accepting();
            auto dirtyBox = box;
            for (auto &plane : dirtyFrustum.planes)
                plane.normal.data[3] = std::bit_cast<float>(bits);
            dirtyBox.min.data[3] = std::bit_cast<float>(bits);
            dirtyBox.max.data[3] = std::bit_cast<float>(bits);
            const auto clearInvalid = restore.value & ~static_cast<unsigned int>(_MM_EXCEPT_INVALID);
            _mm_setcsr(clearInvalid);
            const bool expected = scalarReference(dirtyFrustum, dirtyBox);
            const auto scalarStatus = _mm_getcsr();
            _mm_setcsr(clearInvalid);
            const bool actual = dirtyFrustum.IsAABBInFrustum(dirtyBox);
            const auto simdStatus = _mm_getcsr();
            _mm_setcsr(restore.value); // 검사/예외 처리에 들어가기 전에도 호출자의 원래 상태를 돌려준다.
            check(expected && actual == expected && (scalarStatus & _MM_EXCEPT_INVALID) == 0 &&
                      (simdStatus & _MM_EXCEPT_INVALID) == 0,
                  "unused fourth-lane NaN never changes the MXCSR invalid flag");
        }
    }
    for (std::size_t index = 0; index < 6; ++index)
    {
        auto frustum = accepting();
        auto &plane = frustum.planes[index];
        plane.normal = {2.f, -3.f, 4.f}; // public planes는 정규화되어 있다고 가정하지 않는다.
        plane.distance = -SRMath::dot(plane.normal, positiveVertex(plane, box));
        check(compare(frustum, box), "frustum exact plane contact remains visible");
        plane.distance = std::nextafter(plane.distance, -std::numeric_limits<float>::infinity());
        check(!compare(frustum, box), "frustum one-ULP outside plane rejects at every plane position");
        plane.distance = std::nextafter(plane.distance, std::numeric_limits<float>::infinity());
        plane.distance = std::nextafter(plane.distance, std::numeric_limits<float>::infinity());
        check(compare(frustum, box), "frustum one-ULP inside plane remains visible");
    }
    for (const float zero : {0.f, -0.f})
    {
        auto frustum = accepting();
        auto unusualBox = box;
        unusualBox.min.x = std::numeric_limits<float>::infinity();
        unusualBox.max.x = 1.f;
        frustum.planes[0].normal = {zero, 0.f, 0.f};
        frustum.planes[0].distance = -1.f;
        // >=0은 -0에서도 max를 택한다. sign-bit로 min을 고르면 0*Inf=NaN 때문에 오답이다.
        check(!compare(frustum, unusualBox), "both signed zeros select the maximum endpoint");
    }
    const std::array special{0.f,
                             -0.f,
                             std::numeric_limits<float>::denorm_min(),
                             -std::numeric_limits<float>::denorm_min(),
                             std::numeric_limits<float>::min(),
                             -std::numeric_limits<float>::min(),
                             std::numeric_limits<float>::max(),
                             -std::numeric_limits<float>::max(),
                             std::numeric_limits<float>::infinity(),
                             -std::numeric_limits<float>::infinity(),
                             std::numeric_limits<float>::quiet_NaN()};
    for (std::size_t index = 0; index < 6; ++index)
        for (std::size_t axis = 0; axis < 3; ++axis)
            for (const float value : special)
                for (int field = 0; field < 4; ++field)
                {
                    auto frustum = accepting();
                    auto unusualBox = box;
                    auto &plane = frustum.planes[index];
                    plane.normal = {2.f, -3.f, 4.f};
                    plane.distance = -4.f;
                    if (field == 0)
                        plane.normal[axis] = value;
                    else if (field == 1)
                        plane.distance = value;
                    else if (field == 2)
                        unusualBox.min[axis] = value;
                    else
                        unusualBox.max[axis] = value;
                    compare(frustum, unusualBox);
                }
    auto nanPlane = accepting();
    nanPlane.planes[0].normal.x = std::numeric_limits<float>::quiet_NaN();
    check(compare(nanPlane, box), "unordered plane distance conservatively keeps the box");
    nanPlane.planes[5].distance = -1.f;
    check(!compare(nanPlane, box), "later rejecting plane still rejects after an unordered plane");
    compare(accepting(), AABB{}); // 기존 공개 API의 empty/inverted bounds 동작도 유지한다.

    std::uint32_t state = 0xF2A73B19u;
    const auto coordinate = [&]
    {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(static_cast<int>((state >> 8) % 8193) - 4096) / 128.f;
    };
    std::size_t accepted = 0, rejected = 0;
    for (std::size_t iteration = 0; iteration < 2048; ++iteration)
    {
        AABB randomBox;
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            randomBox.min[axis] = coordinate();
            randomBox.max[axis] = randomBox.min[axis] + std::abs(coordinate());
        }
        Frustum frustum;
        for (std::size_t index = 0; index < frustum.planes.size(); ++index)
        {
            auto &plane = frustum.planes[index];
            for (std::size_t axis = 0; axis < 3; ++axis)
                plane.normal[axis] = coordinate();
            if (iteration % 4 == 0)
                plane.normal[iteration % 3] = iteration % 8 == 0 ? -0.f : 0.f;
            // 전부 통과/특정 순서의 평면만 기각/임의 distance를 섞어 early-exit도 편향 없이 검사한다.
            if (iteration % 3 == 0)
                plane.distance = coordinate();
            else
                plane.distance = -SRMath::dot(plane.normal, positiveVertex(plane, randomBox)) +
                                 (iteration % 3 == 1 || index != iteration % 6 ? 2.f : -2.f);
        }
        if (compare(frustum, randomBox))
            ++accepted;
        else
            ++rejected;
    }
    check(accepted != 0 && rejected != 0, "random frustum selection suite exercises both results");
}

void mathTests()
{
    using namespace SRMath;
    const mat4 transform = translate(vec3{3.f, -2.f, 1.f}) * scale(vec3{2.f, 3.f, 4.f});
    const auto inverted = inverse(transform);
    check(inverted.has_value(), "invertible matrix");
    const vec4 input{1.f, 2.f, 3.f, 1.f};
    vectorNear(*inverted * (transform * input), input);
    check(!inverse(mat4{0.f}), "singular matrix");
    const auto normalMatrix = inverse_transpose(scale(vec3{2.f, 3.f, 4.f}));
    check(normalMatrix.has_value(), "normal matrix");
    vectorNear(*normalMatrix * vec4{1.f, 1.f, 1.f, 0.f}, vec4{0.5f, 1.f / 3.f, 0.25f, 0.f});
    const auto pair = SIMD::transform_pair(transform, input, vec4{2.f, 1.f, -1.f, 1.f});
    vectorNear(pair.first, transform * input);
    vectorNear(pair.second, transform * vec4{2.f, 1.f, -1.f, 1.f});
    std::cout << "SIMD dispatch: " << (SIMD::avx_available() ? "AVX" : "SSE") << '\n';
    std::cout << "AVX2 available: " << (SIMD::avx2_available() ? "yes" : "no")
              << ", FMA available: " << (SIMD::fma_available() ? "yes" : "no") << '\n';
    if (SIMD::avx2_available())
    {
        check(SIMD::avx_available(), "AVX must be available if AVX2 is available");
    }
    if (SIMD::fma_available())
    {
        check(SIMD::avx_available(), "AVX must be available if FMA is available");
    }
    AABB box;
    box.Encapsulate(vec3{-0.5f, -0.5f, -0.5f});
    box.Encapsulate(vec3{0.5f, 0.5f, 0.5f});
    check(box.IsValid(), "valid AABB");
    Frustum frustum;
    frustum.Update(mat4::identity());
    check(frustum.IsAABBInFrustum(box), "inside frustum");
    check(!frustum.IsAABBInFrustum(box.Transform(translate(vec3{10.f, 0.f, 0.f}))), "outside frustum");
    check(!AABB{}.IsValid(), "empty bounds");
    sr::FixedCapacityVector<int, 3> fixed;
    fixed.emplace_back(1);
    fixed.emplace_back(2);
    fixed.emplace_back(3);
    bool rejected = false;
    try
    {
        fixed.emplace_back(4);
    }
    catch (const std::exception &)
    {
        rejected = true;
    }
    check(rejected && fixed.size() == 3, "fixed capacity overflow");
    fixed.clear();
    check(fixed.empty(), "fixed capacity reset");
}
} // namespace

namespace
{
// padding lane은 값과 무관해야 한다. 예전 구현은 movemask를 정확히 0x3/0x7과 비교해, padding이 같으면(보통 둘 다 0)
// 같은 벡터를 다르다고 판단했다.
void vectorEqualityTests()
{
    check(SRMath::vec2(1.f, 2.f) == SRMath::vec2(1.f, 2.f), "equal vec2");
    check(!(SRMath::vec2(1.f, 2.f) != SRMath::vec2(1.f, 2.f)), "equal vec2 is not unequal");
    check(SRMath::vec2(1.f, 2.f) != SRMath::vec2(1.f, 3.f), "different vec2 y");
    check(SRMath::vec3(1.f, 2.f, 3.f) == SRMath::vec3(1.f, 2.f, 3.f), "equal vec3");
    check(!(SRMath::vec3(1.f, 2.f, 3.f) != SRMath::vec3(1.f, 2.f, 3.f)), "equal vec3 is not unequal");
    check(SRMath::vec3(1.f, 2.f, 3.f) != SRMath::vec3(1.f, 2.f, 4.f), "different vec3 z");
    check(SRMath::vec3(1.f, 2.f, 3.f) != SRMath::vec3(0.f, 2.f, 3.f), "different vec3 x");
    // padding lane에 다른 값이 있어도 x, y, z가 같으면 같다.
    SRMath::vec3 padded(_mm_set_ps(9.f, 3.f, 2.f, 1.f));
    check(padded == SRMath::vec3(1.f, 2.f, 3.f), "vec3 ignores the padding lane");
    SRMath::vec2 padded2(_mm_set_ps(7.f, 8.f, 2.f, 1.f));
    check(padded2 == SRMath::vec2(1.f, 2.f), "vec2 ignores the padding lanes");
    check(SRMath::vec4(1.f, 2.f, 3.f, 4.f) == SRMath::vec4(1.f, 2.f, 3.f, 4.f), "equal vec4");
    check(SRMath::vec4(1.f, 2.f, 3.f, 4.f) != SRMath::vec4(1.f, 2.f, 3.f, 5.f), "vec4 compares w");
    check(SRMath::vec3(std::nanf(""), 0.f, 0.f) != SRMath::vec3(std::nanf(""), 0.f, 0.f), "NaN is never equal");
}

void vectorTrivialCopyTests()
{
    static_assert(std::is_trivially_copyable_v<SRMath::vec2> &&
                  std::is_trivially_copyable_v<SRMath::vec3> &&
                  std::is_trivially_copyable_v<SRMath::vec4>,
                  "Vector types must be trivially copyable");

    check(std::is_trivially_copyable_v<SRMath::vec2>, "vec2 is trivially copyable");
    check(std::is_trivially_copyable_v<SRMath::vec3>, "vec3 is trivially copyable");
    check(std::is_trivially_copyable_v<SRMath::vec4>, "vec4 is trivially copyable");

    // vec3 copy construction and assignment preserve components
    const SRMath::vec3 src3{1.25f, -2.5f, 3.75f};
    SRMath::vec3 copyConstructed3 = src3;
    check(copyConstructed3 == src3, "vec3 copy construction preserves value");
    check(copyConstructed3.x == 1.25f && copyConstructed3.y == -2.5f && copyConstructed3.z == 3.75f,
          "vec3 copy construction preserves exact scalar lanes");

    SRMath::vec3 copyAssigned3{0.f, 0.f, 0.f};
    copyAssigned3 = src3;
    check(copyAssigned3 == src3, "vec3 copy assignment preserves value");

    // Vector<3> from Vector<4> via _mm_insert_ps zeroes lane 3 (w)
    const SRMath::vec4 src4{10.f, 20.f, 30.f, 40.f};
    const SRMath::vec3 from4(src4);
    check(from4.x == 10.f && from4.y == 20.f && from4.z == 30.f, "vec3 from vec4 extracts x, y, z");
    check(from4.data[3] == 0.0f, "vec3 from vec4 zeroes the fourth lane");
    check(std::bit_cast<std::uint32_t>(from4.data[3]) == 0u, "vec3 fourth lane is exact +0.0f bits");

    // Vector<4> from Vector<3> sets lane 3 (w) to 1.0f
    const SRMath::vec4 from3DefaultW(src3);
    check(from3DefaultW.x == src3.x && from3DefaultW.y == src3.y && from3DefaultW.z == src3.z,
          "vec4 from vec3 preserves x, y, z");
    check(from3DefaultW.w == 1.0f, "vec4 from vec3 sets w to 1.0f");

    // Vector<4> from Vector<3> with explicit w
    const SRMath::vec4 from3CustomW(src3, 42.5f);
    check(from3CustomW.x == src3.x && from3CustomW.y == src3.y && from3CustomW.z == src3.z,
          "vec4 from vec3 with w preserves x, y, z");
    check(from3CustomW.w == 42.5f, "vec4 from vec3 with w sets custom w");

    // Bit-exact preservation across special float values
    const std::uint32_t nanBits = 0x7FC01234u;
    const float nanVal = std::bit_cast<float>(nanBits);
    const SRMath::vec3 specialVec3{nanVal, -0.0f, std::numeric_limits<float>::infinity()};
    const SRMath::vec4 specialVec4(specialVec3, -5.0f);
    check(std::bit_cast<std::uint32_t>(specialVec4.x) == nanBits, "vec4 preserves x bit pattern");
    check(std::bit_cast<std::uint32_t>(specialVec4.y) == std::bit_cast<std::uint32_t>(-0.0f), "vec4 preserves -0.0f");
    check(specialVec4.z == std::numeric_limits<float>::infinity(), "vec4 preserves infinity");
    check(specialVec4.w == -5.0f, "vec4 custom w preserved with special float inputs");

    const SRMath::vec3 roundtripVec3(specialVec4);
    check(std::bit_cast<std::uint32_t>(roundtripVec3.x) == nanBits, "vec3 from vec4 preserves x bit pattern");
    check(std::bit_cast<std::uint32_t>(roundtripVec3.y) == std::bit_cast<std::uint32_t>(-0.0f), "vec3 preserves -0.0f");
    check(roundtripVec3.z == std::numeric_limits<float>::infinity(), "vec3 preserves infinity");
    check(std::bit_cast<std::uint32_t>(roundtripVec3.data[3]) == 0u, "vec3 from special vec4 zeroes lane 3");
}

} // namespace

void sr::test::RegisterMathTests(TestRegistry &registry)
{
    registry.Add("math.core", mathTests);
    registry.Add("math.vectorEquality", vectorEqualityTests);
    registry.Add("math.vectorTrivialCopy", vectorTrivialCopyTests);
    registry.Add("math.frustumSelection", frustumSelectionTests);
}

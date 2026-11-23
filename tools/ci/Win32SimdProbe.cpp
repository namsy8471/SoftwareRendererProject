#include <array>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>
#include "Math/AABB.h"
#include "Math/Quaternion.h"

namespace {
using namespace SRMath;

bool approximatelyEqual(float a, float b, float tolerance = 1e-4f) {
    return std::abs(a - b) <= tolerance;
}

void check(bool condition, std::string_view message) {
    if (!condition) throw std::runtime_error(std::string(message));
}

void vec3Near(const SRMath::vec3 &actual, const SRMath::vec3 &expected, std::string_view what,
              float tolerance = 1e-4f)
{
    for (std::size_t i = 0; i < 3; ++i)
        check(approximatelyEqual(actual[i], expected[i], tolerance),
              std::string(what) + ": got (" + std::to_string(actual.x) + ", " + std::to_string(actual.y) + ", " +
                  std::to_string(actual.z) + ")");
}

void matrixNear(const SRMath::mat4 &a, const SRMath::mat4 &b, std::string_view what)
{
    for (std::size_t c = 0; c < 4; ++c)
        for (std::size_t r = 0; r < 4; ++r)
            check(approximatelyEqual(a[c][r], b[c][r]), what);
}


void exerciseQuaternionTest() {
    constexpr float pi = std::numbers::pi_v<float>;
    const SRMath::vec3 axes[] = {{0.f, 1.f, 0.f}, {1.f, 0.f, 0.f}, {0.3f, -0.5f, 0.8f}};
    for (const auto &axis : axes)
        for (const float angle : {0.f, 0.4f, pi / 2.f, 2.5f})
        {
            const SRMath::quat q = SRMath::from_axis_angle(axis, angle);
            const SRMath::vec3 rotationVector = SRMath::normalize(axis) * angle;
            matrixNear(SRMath::to_mat4(q), SRMath::rotate(rotationVector), "to_mat4 must match rotate()");
            const SRMath::vec3 v(0.2f, -1.f, 3.f);
            std::printf("angle %.8g: ", angle);
            vec3Near(SRMath::rotate(q, v), SRMath::vec3(SRMath::rotate(rotationVector) * SRMath::vec4(v, 0.f)),
                     "rotate(q, v)");
            vec3Near(SRMath::to_rotation_vector(q), rotationVector, "rotation vector round trip");
        }

}

volatile float inputAngle = 0.4f;
volatile float inputX = 0.2f;

__declspec(noinline) vec3 makeDirection(float x, float y, float z) noexcept {
    return vec3(x, y, z);
}

__declspec(noinline) vec3 crossDirection(const vec3& a, const vec3& b) noexcept {
    return cross(a, b);
}

__declspec(noinline) float triangleArea(const vec3& a, const vec3& b, const vec3& c) noexcept {
    return 0.5f * length(cross(b - a, c - a));
}

__declspec(noinline) AABB transformBounds(const AABB& local, const mat4& world) noexcept {
    return local.Transform(world);
}

bool vectorNear(const char* label, const vec3& actual, const vec3& expected) {
    std::printf("%s: %.8g %.8g %.8g (expected %.8g %.8g %.8g)\n", label,
                actual.x, actual.y, actual.z, expected.x, expected.y, expected.z);
    return std::abs(actual.x - expected.x) < 1e-5f
        && std::abs(actual.y - expected.y) < 1e-5f
        && std::abs(actual.z - expected.z) < 1e-5f;
}
}

int main() {
    std::printf("MSVC=%d pointer=%zu\n", _MSC_FULL_VER, sizeof(void*));
    bool exactTest = true;
    try {
        exerciseQuaternionTest();
        std::puts("original rotation test passed");
    } catch (const std::exception& error) {
        std::printf("original rotation test failed: %s\n", error.what());
        exactTest = false;
    }
    const std::array axes{vec3(0.f, 1.f, 0.f), vec3(1.f, 0.f, 0.f), vec3(0.3f, -0.5f, 0.8f)};
    const float angle = inputAngle;
    const vec3 v(inputX, -1.f, 3.f);
    bool good = exactTest;
    for (const vec3& axis : axes) {
        const quat q = from_axis_angle(axis, angle);
        const vec3 rotationVector = normalize(axis) * angle;
        good &= vectorNear("matrix/quaternion", vec3(rotate(rotationVector) * vec4(v, 0.f)), rotate(q, v));
    }
    const vec3 direction = makeDirection(v.x, v.y, v.z);
    good &= vectorNear("out-of-line return", direction, v);
    const vec3 alongX(1.f, 0.f, 0.f);
    const vec3 alongY(0.f, 1.f, 0.f);
    const vec3 alongZ(0.f, 0.f, 1.f);
    good &= vectorNear("out-of-line cross", crossDirection(alongX, alongY), alongZ);
    const float area = triangleArea(vec3(0.f), alongX, alongY);
    std::printf("triangle area = %.8g (expected 0.5)\n", area);
    good &= std::abs(area - 0.5f) < 1e-6f;
    const AABB bounds{vec3(-0.5f, -0.5f, 0.f), vec3(0.5f, 0.5f, 0.f)};
    const mat4 parentWorld = translate({10.f, 0.f, 0.f});
    const mat4 translation = translate({-10.f, 0.f, 0.f});
    const mat4 rotation = rotate({0.f, 0.f, 0.f});
    const mat4 scaling = scale({1.f, 1.f, 1.f});
    mat4 childWorld = translation * rotation * scaling;
    childWorld = parentWorld * childWorld;
    const AABB transformed = transformBounds(bounds, childWorld);
    good &= vectorNear("transformed min", transformed.min, bounds.min);
    good &= vectorNear("transformed max", transformed.max, bounds.max);
    return good ? 0 : 1;
}

#pragma once
#include <array>
#include "Math/SRMath.h"
#include "Math/AABB.h"

struct Plane
{
    SRMath::vec3 normal = {0.f, 1.f, 0.f};
    float distance = 0.0f; // 원점에서 평면까지의 signed distance

    [[nodiscard]] float SignedDistanceTo(const SRMath::vec3 &point) const noexcept
    {
        return SRMath::dot(normal, point) + distance;
    }
};

// ClassifyAABB의 결과. Inside는 상자 전체가 모든 평면 안쪽에 여유를 두고 들어 있다는 뜻이다.
enum class FrustumTest
{
    Outside,
    Intersects,
    Inside
};

class Frustum
{
  public:
    // C 배열보다 std::array가 범위 알고리즘과 크기 안전성을 제공한다.
    std::array<Plane, 6> planes{}; // Left, Right, Bottom, Top, Near, Far
    SRMath::vec3 cameraPosition{0.f, 0.f, 0.f};
    bool hasCameraPosition = false;

    void SetCameraPosition(const SRMath::vec3 &pos) noexcept
    {
        cameraPosition = pos;
        hasCameraPosition = true;
    }
    [[nodiscard]] const SRMath::vec3 &GetCameraPosition() const noexcept { return cameraPosition; }
    [[nodiscard]] bool HasCameraPosition() const noexcept { return hasCameraPosition; }

    void Update(const SRMath::mat4 &viewProjectionMatrix) noexcept;
    [[nodiscard]] bool IsAABBInFrustum(const AABB &aabb) const noexcept;
    // Outside 판정은 IsAABBInFrustum과 같은 계산이다(false ⇔ Outside). Inside면 이 상자 안에 든 더 작은
    // 상자도 IsAABBInFrustum을 통과한다. 그 보장이 부동소수점 반올림에 흔들리지 않도록 n-vertex가
    // 평면 안쪽으로 크기에 비례한 여유(insideMargin) 이상일 때만 Inside라고 한다. 애매하면 Intersects다.
    [[nodiscard]] FrustumTest ClassifyAABB(const AABB &aabb) const noexcept;
};

#pragma once

#include <memory>
#include <optional>
#include "Graphics/ViewParams.h"
#include "Math/AABB.h"
#include "Math/SRMath.h"

class Camera;
class GameObject;
class Scene;

// 화면 픽셀에서 쏜 광선과 장면 물체(AABB·메시 삼각형·스킨 메시)의 교차 판정.
struct Ray
{
    SRMath::vec3 origin{0.f, 0.f, 0.f};
    SRMath::vec3 direction{0.f, 0.f, -1.f};
};

struct PickHit
{
    std::shared_ptr<GameObject> object;
    SRMath::vec3 worldPoint{0.f, 0.f, 0.f};
    float distance = 0.f;
};

[[nodiscard]] Ray ScreenPointToRay(float screenX, float screenY, float viewportWidth, float viewportHeight,
                                   const ViewParams &view) noexcept;
[[nodiscard]] Ray ScreenPointToRay(const Camera &camera, float screenX, float screenY, int viewportWidth,
                                   int viewportHeight) noexcept;

[[nodiscard]] bool RayIntersectsAABB(const Ray &ray, const AABB &aabb, float &outTMin, float &outTMax) noexcept;
[[nodiscard]] std::optional<float> RayIntersectsAABB(const Ray &ray, const AABB &aabb) noexcept;

[[nodiscard]] bool RayIntersectsTriangle(const Ray &ray, const SRMath::vec3 &v0, const SRMath::vec3 &v1,
                                         const SRMath::vec3 &v2, float &outT) noexcept;
[[nodiscard]] std::optional<float> RayIntersectsTriangle(const Ray &ray, const SRMath::vec3 &v0,
                                                         const SRMath::vec3 &v1,
                                                         const SRMath::vec3 &v2) noexcept;

[[nodiscard]] std::optional<PickHit> PickSceneObject(const Scene &scene, const Ray &ray);

namespace sr::scene
{
using ::PickHit;
using ::PickSceneObject;
using ::Ray;
using ::RayIntersectsAABB;
using ::RayIntersectsTriangle;
using ::ScreenPointToRay;
} // namespace sr::scene

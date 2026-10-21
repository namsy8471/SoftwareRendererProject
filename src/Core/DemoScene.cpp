#include "Core/DemoScene.h"
#include "Graphics/Model.h"
#include "Graphics/ModelLoader.h"
#include "Math/AABB.h"
#include "Scene/GameObject.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <string_view>
#include <utility>

namespace
{
struct SceneObjectDesc
{
    std::string_view modelName;
    SRMath::vec3 position;
};

// 장면 구성은 이 표 하나로 정한다. 벤치마크·골든 이미지 기준이 바뀌므로 수정 시 골든을 갱신한다.
const std::array sceneObjects{SceneObjectDesc{"IronMan", {0.f, 0.f, 0.f}},
                              SceneObjectDesc{"teapot", {-10.f, 0.f, 0.f}}};
constexpr float sceneObjectScale = 0.04f;
} // namespace

namespace sr::demo
{
std::expected<Scene, AssetLoadError> LoadScene(const std::filesystem::path &assetDirectory)
{
    Scene scene;
    for (const auto &object : sceneObjects)
    {
        auto model = ModelLoader::LoadOBJ(assetDirectory / std::filesystem::path(object.modelName));
        if (!model)
            return std::unexpected(std::move(model.error()));
        // 새 루트 물체라 실패할 수 없다.
        [[maybe_unused]] const bool added = scene.AddObject(std::make_shared<GameObject>(
            object.position, SRMath::vec3(0.f, 0.f, 0.f),
            SRMath::vec3(sceneObjectScale, sceneObjectScale, sceneObjectScale), std::move(*model)));
    }
    scene.SetCamera(Camera(SRMath::vec3(0.f, 0.f, -5.f)));
    scene.AddLight(DirectionalLight());
    return scene;
}

void FitCameraToScene(Scene &scene, float aspectRatio)
{
    AABB sceneBounds;
    for (const auto &object : scene.GetRootObjects())
        sceneBounds.Encapsulate(object->GetWorldAABB());
    FitCameraToBounds(scene, sceneBounds, aspectRatio);
}

void FitCameraToBounds(Scene &scene, const AABB &sceneBounds, float aspectRatio)
{
    const auto center = sceneBounds.Center();
    const auto halfSize = (sceneBounds.max - sceneBounds.min) * 0.5f;
    const float distance =
        std::max(halfSize.y, halfSize.x / aspectRatio) / std::tan(std::numbers::pi_v<float> / 6.f) * 1.15f +
        halfSize.z;
    Camera &camera = scene.GetCamera();
    camera.SetCameraPos({center.x, center.y, center.z - distance});
    camera.SetCameraYaw(0.f);
    camera.SetCameraPitch(0.f);
}
} // namespace sr::demo

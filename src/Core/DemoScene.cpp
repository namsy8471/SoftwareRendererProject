#include "Core/DemoScene.h"
#include "Graphics/Model.h"
#include "Graphics/ModelLoader.h"
#include "Math/AABB.h"
#include "Renderer/DebugGizmos.h"
#include "Scene/GameObject.h"

#include <algorithm>
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

std::expected<void, AssetLoadError> DemoScene::Load(const std::filesystem::path &assetDirectory)
{
    std::vector<std::shared_ptr<GameObject>> loaded;
    for (const auto &object : sceneObjects)
    {
        auto model = ModelLoader::LoadOBJ(assetDirectory / std::filesystem::path(object.modelName));
        if (!model)
            return std::unexpected(std::move(model.error()));
        loaded.push_back(std::make_shared<GameObject>(
            object.position, SRMath::vec3(0.f, 0.f, 0.f),
            SRMath::vec3(sceneObjectScale, sceneObjectScale, sceneObjectScale), std::move(*model)));
    }
    m_gameObjects = std::move(loaded);
    m_camera = Camera(SRMath::vec3(0.f, 0.f, -5.f));
    m_lights.assign(1, DirectionalLight());
    return {};
}

void DemoScene::Update(float deltaTime, int width, int height, std::span<const bool, keyCount> keys)
{
    m_renderQueue.Clear();
    if (width <= 0 || height <= 0)
        return; // 최소화된 창은 유효한 종횡비가 없다.

    const float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
    m_camera.Update(deltaTime, keys, aspectRatio);

    const Frustum &frustum = m_camera.GetFrustum();
    for (const auto &gameObject : m_gameObjects)
    {
        if (gameObject)
        {
            gameObject->Update(deltaTime, m_isRotateMode);
            gameObject->SubmitToRenderQueue(m_renderQueue, frustum, m_debugFlags);
        }
    }

    if (m_debugFlags.bShowLightDirection)
        sr::debug::SubmitDirectionalLightGizmos(m_renderQueue, m_lights);
}

void DemoScene::FitCameraToScene(float aspectRatio)
{
    AABB sceneBounds;
    for (const auto &object : m_gameObjects)
        sceneBounds.Encapsulate(object->GetWorldAABB());
    const auto center = sceneBounds.Center();
    const auto halfSize = (sceneBounds.max - sceneBounds.min) * 0.5f;
    const float distance =
        std::max(halfSize.y, halfSize.x / aspectRatio) / std::tan(std::numbers::pi_v<float> / 6.f) * 1.15f +
        halfSize.z;
    m_camera.SetCameraPos({center.x, center.y, center.z - distance});
    m_camera.SetCameraYaw(0.f);
    m_camera.SetCameraPitch(0.f);
}

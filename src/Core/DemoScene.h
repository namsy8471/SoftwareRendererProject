#pragma once

#include <array>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>
#include "Graphics/Light.h"
#include "Renderer/RenderQueue.h"
#include "Scene/Camera.h"
#include "Utils/AssetLoadError.h"
#include "Utils/DebugUtils.h"

class GameObject;

// 창·입력·표시와 무관한 데모 장면이다. Win32 Framework와 headless srcli가
// 같은 모델·카메라·광원·갱신 순서를 공유하도록 장면 정의를 이곳에만 둔다.
class DemoScene
{
  public:
    static constexpr std::size_t keyCount = 256;
    using KeyState = std::array<bool, keyCount>;

    // assetDirectory/IronMan.obj, teapot.obj를 읽는다. 실패하면 장면을 바꾸지 않고 오류를 돌려준다.
    [[nodiscard]] std::expected<void, AssetLoadError> Load(const std::filesystem::path &assetDirectory);

    // 카메라·물체 transform을 갱신하고 RenderQueue를 이번 프레임 명령으로 다시 채운다.
    // 크기가 0 이하(최소화)면 queue만 비운다. keys는 Win32 virtual-key 순번이다.
    void Update(float deltaTime, int width, int height, std::span<const bool, keyCount> keys);
    void Update(float deltaTime, int width, int height) { Update(deltaTime, width, height, m_noKeys); }

    // 현재 물체 경계를 60도 수직 FOV에 맞춘 정면 카메라. 벤치마크와 골든 이미지의 기준 시점이다.
    void FitCameraToScene(float aspectRatio);

    [[nodiscard]] Camera &GetCamera() noexcept { return m_camera; }
    [[nodiscard]] const Camera &GetCamera() const noexcept { return m_camera; }
    [[nodiscard]] DebugFlags &GetDebugFlags() noexcept { return m_debugFlags; }
    [[nodiscard]] const DebugFlags &GetDebugFlags() const noexcept { return m_debugFlags; }
    [[nodiscard]] bool IsRotateMode() const noexcept { return m_isRotateMode; }
    void SetRotateMode(bool rotate) noexcept { m_isRotateMode = rotate; }
    [[nodiscard]] const RenderQueue &GetRenderQueue() const noexcept { return m_renderQueue; }
    [[nodiscard]] std::span<const DirectionalLight> GetLights() const noexcept { return m_lights; }
    [[nodiscard]] std::span<const std::shared_ptr<GameObject>> GetGameObjects() const noexcept
    {
        return m_gameObjects;
    }

  private:
    std::vector<std::shared_ptr<GameObject>> m_gameObjects;
    std::vector<DirectionalLight> m_lights;
    Camera m_camera;
    RenderQueue m_renderQueue;
    DebugFlags m_debugFlags;
    bool m_isRotateMode = true;
    KeyState m_noKeys{};
};

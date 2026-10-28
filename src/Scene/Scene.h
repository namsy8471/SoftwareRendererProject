#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>
#include "Graphics/Light.h"
#include "Scene/Camera.h"

class GameObject;
namespace sr::anim
{
class Animator;
}

// 창·입력·렌더러와 무관한 장면 데이터: 루트 물체 목록, 광원, 카메라.
// DemoScene 표, 이후의 프로젝트/씬 파일 로더가 모두 AddObject/AddLight로 같은 Scene을 채운다.
// Update와 수정은 프레임 루프 스레드에서만 한다(비동기 로드 결과도 프레임 시작에 반영).
class Scene
{
  public:
    static constexpr std::size_t keyCount = 256;
    using KeyState = std::array<bool, keyCount>;

    Scene();
    ~Scene();
    Scene(Scene &&) noexcept;
    Scene &operator=(Scene &&) noexcept;
    Scene(const Scene &) = delete;
    Scene &operator=(const Scene &) = delete;

    // parent가 null이면 루트로 추가한다. 아니면 parent->AddChild 규칙(중복·순환 거부)을 따르며
    // parent는 이미 이 장면에 있어야 한다. 실패하면 false이고 장면은 바뀌지 않는다.
    [[nodiscard]] bool AddObject(std::shared_ptr<GameObject> object,
                                 const std::shared_ptr<GameObject> &parent = nullptr);
    void AddLight(const DirectionalLight &light) { m_lights.push_back(light); }
    void AddPointLight(const PointLight &light) { m_pointLights.push_back(light); }
    void AddSpotLight(const SpotLight &light) { m_spotLights.push_back(light); }
    void SetCamera(const Camera &camera) noexcept { m_camera = camera; }
    // 재생기의 node들은 이 장면에 넣은 물체여야 한다(보통 같은 glTF root). null은 무시한다.
    void AddAnimator(std::shared_ptr<sr::anim::Animator> animator);

    // 카메라(입력·종횡비) → 애니메이션 포즈 → 루트부터 전위 순서로 물체 transform → 스킨 변형 순서로 갱신한다.
    // keys는 Win32 virtual-key 순번이다. aspectRatio는 0보다 커야 한다.
    void Update(float deltaTime, float aspectRatio, std::span<const bool, keyCount> keys);

    [[nodiscard]] Camera &GetCamera() noexcept { return m_camera; }
    [[nodiscard]] const Camera &GetCamera() const noexcept { return m_camera; }
    [[nodiscard]] std::span<const DirectionalLight> GetLights() const noexcept { return m_lights; }
    [[nodiscard]] std::span<const PointLight> GetPointLights() const noexcept { return m_pointLights; }
    [[nodiscard]] std::span<const SpotLight> GetSpotLights() const noexcept { return m_spotLights; }
    [[nodiscard]] std::span<const std::shared_ptr<GameObject>> GetRootObjects() const noexcept
    {
        return m_rootObjects;
    }
    [[nodiscard]] std::span<const std::shared_ptr<sr::anim::Animator>> GetAnimators() const noexcept
    {
        return m_animators;
    }
    // 데모의 Y축 자동 회전(초당 0.4 rad). 애니메이션 컴포넌트가 생기면 그쪽으로 옮긴다.
    [[nodiscard]] bool IsRotateMode() const noexcept { return m_isRotateMode; }
    void SetRotateMode(bool rotate) noexcept { m_isRotateMode = rotate; }

  private:
    std::vector<std::shared_ptr<GameObject>> m_rootObjects;
    std::vector<DirectionalLight> m_lights;
    std::vector<PointLight> m_pointLights;
    std::vector<SpotLight> m_spotLights;
    std::vector<std::shared_ptr<sr::anim::Animator>> m_animators;
    Camera m_camera;
    bool m_isRotateMode = true;
};

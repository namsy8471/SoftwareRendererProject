#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <span>
#include <utility>
#include "Core/DebugFlags.h"
#include "Core/SceneRenderCollector.h"
#include "Renderer/RenderQueue.h"
#include "Scene/Scene.h"

class Renderer;
class RenderTarget;

// 플랫폼과 무관한 한 프레임: [프레임 시작 훅] → Scene::Update → 가시성 수집(RenderQueue)
// → target.Clear → Renderer::RenderScene. Win32App, srcli/HeadlessRunner, 골든 테스트가
// 같은 순서를 쓴다. 표시(GDI)는 호출자 몫이다. 모든 호출은 같은 스레드에서 한다.
class FrameRunner
{
  public:
    static constexpr std::size_t keyCount = Scene::keyCount;
    // 프레임마다 Update 맨 앞에서 한 번 호출된다. 비동기 로드 완료분처럼 다른 스레드가 만든
    // 결과를 장면에 넣는 자리다. 훅 안에서 SetScene/AddObject를 해도 된다.
    using FrameStartHook = std::function<void(FrameRunner &)>;

    FrameRunner();
    ~FrameRunner();
    FrameRunner(const FrameRunner &) = delete;
    FrameRunner &operator=(const FrameRunner &) = delete;

    void SetScene(Scene scene);
    [[nodiscard]] Scene &GetScene() noexcept { return m_scene; }
    [[nodiscard]] const Scene &GetScene() const noexcept { return m_scene; }
    [[nodiscard]] DebugFlags &GetDebugFlags() noexcept { return m_debugFlags; }
    [[nodiscard]] Renderer &GetRenderer() noexcept { return *m_renderer; }
    [[nodiscard]] const Renderer &GetRenderer() const noexcept { return *m_renderer; }
    [[nodiscard]] const RenderQueue &GetRenderQueue() const noexcept { return m_renderQueue; }
    void SetFrameStartHook(FrameStartHook hook) { m_frameStartHook = std::move(hook); }

    // 장면을 갱신하고 RenderQueue를 이번 프레임 명령으로 다시 채운다.
    // 크기가 0 이하(최소화)면 queue만 비운다. keys는 Win32 virtual-key 순번이다.
    void Update(float deltaTime, int width, int height, std::span<const bool, keyCount> keys);
    void Update(float deltaTime, int width, int height) { Update(deltaTime, width, height, m_noKeys); }

    // target을 지우고 마지막 Update의 명령을 그린다. 빈 target은 no-op이다.
    void Render(RenderTarget &target);

  private:
    Scene m_scene;
    DebugFlags m_debugFlags;
    RenderQueue m_renderQueue;
    SceneRenderCollector m_collector;
    std::unique_ptr<Renderer> m_renderer;
    FrameStartHook m_frameStartHook;
    std::array<bool, keyCount> m_noKeys{};
};

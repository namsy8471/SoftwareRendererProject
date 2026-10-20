#include "Core/FrameRunner.h"
#include "Renderer/DebugGizmos.h"
#include "Renderer/Renderer.h"
#include "Renderer/RenderTarget.h"

#include <utility>

FrameRunner::FrameRunner() : m_renderer(std::make_unique<Renderer>()) {}
FrameRunner::~FrameRunner() = default;

void FrameRunner::SetScene(Scene scene)
{
    // 큐의 명령은 이전 장면 메시를 가리키므로 장면보다 먼저 비운다.
    m_renderQueue.Clear();
    m_scene = std::move(scene);
}

void FrameRunner::Update(float deltaTime, int width, int height, std::span<const bool, keyCount> keys)
{
    if (m_frameStartHook)
        m_frameStartHook(*this);

    m_renderQueue.Clear();
    if (width <= 0 || height <= 0)
        return; // 최소화된 창은 유효한 종횡비가 없다.

    const float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
    m_scene.Update(deltaTime, aspectRatio, keys);
    m_collector.Collect(m_scene.GetRootObjects(), m_scene.GetCamera().GetFrustum(), m_debugFlags, m_renderQueue);

    if (m_debugFlags.bShowLightDirection)
        sr::debug::SubmitDirectionalLightGizmos(m_renderQueue, m_scene.GetLights());
}

void FrameRunner::Render(RenderTarget &target)
{
    target.Clear();
    m_renderer->RenderScene(target, m_renderQueue, m_scene.GetCamera().GetViewParams(), m_scene.GetLights());
}

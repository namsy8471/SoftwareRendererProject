#include "Utils/PerformanceAnalyzer.h"

namespace
{
constexpr auto fpsInterval = std::chrono::seconds{1};
}

PerformanceAnalyzer::PerformanceAnalyzer() : m_prevTime(Clock::now()) {}

void PerformanceAnalyzer::Update()
{
    // --- FPS 계산 로직 ---
    const auto currentTime = Clock::now();
    m_deltaTime = currentTime - m_prevTime;
    m_prevTime = currentTime;

    ++m_frameCount;
    m_elapsedTime += m_deltaTime;

    // 1초가 지났으면 FPS를 계산하고 초기화합니다.
    if (m_elapsedTime >= fpsInterval)
    {
        m_avgFps = m_frameCount;
        m_frameCount = 0;
        m_elapsedTime -= fpsInterval;
    }
}

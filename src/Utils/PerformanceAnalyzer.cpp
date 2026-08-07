#include "Utils/PerformanceAnalyzer.h"

namespace
{
constexpr auto kFpsInterval = std::chrono::seconds{ 1 };
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
    // C++11 chrono duration 자체를 비교하면 초/틱 단위 변환을 컴파일러가
    // 처리한다. float 초로 바꿔 누적하던 방식보다 정밀도 손실이 적다.
    if (m_elapsedTime >= kFpsInterval)
    {
        m_avgfps = m_frameCount;
        m_frameCount = 0;
        m_elapsedTime -= kFpsInterval;
    }
}

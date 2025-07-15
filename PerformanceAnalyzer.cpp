#include "PerformanceAnalyzer.h"

#define INTERVAL 1.f;

PerformanceAnalyzer::PerformanceAnalyzer() : m_frequency(), m_prevTime()
{
}

PerformanceAnalyzer::~PerformanceAnalyzer()
{
}

void PerformanceAnalyzer::Initialize()
{
    // 고해상도 타이머의 주파수를 얻어옵니다.
    QueryPerformanceFrequency(&m_frequency);
    // 현재 시간을 기록합니다.
    QueryPerformanceCounter(&m_prevTime);
}

void PerformanceAnalyzer::Update()
{
    // --- FPS 계산 로직 ---
    LARGE_INTEGER currentTime;
    QueryPerformanceCounter(&currentTime);

    // 이전 프레임과의 시간차(delta time)를 계산합니다.
    float deltaTime = static_cast<float>(currentTime.QuadPart - m_prevTime.QuadPart) / m_frequency.QuadPart;
    m_prevTime = currentTime;

    m_frameCount++;
    m_elapsedTime += deltaTime;

    float interval = INTERVAL;
    // 1초가 지났으면 FPS를 계산하고 초기화합니다.
    if (m_elapsedTime > interval)
    {
        m_avgfps = m_frameCount;
        m_frameCount = 0;
        m_elapsedTime -= interval;
    }
}
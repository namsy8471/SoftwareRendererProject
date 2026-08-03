#include "Utils/PerformanceAnalyzer.h"

#define INTERVAL 1.f;

PerformanceAnalyzer::PerformanceAnalyzer() : m_frequency(), m_prevTime()
{
    // ���ػ� Ÿ�̸��� ���ļ��� ���ɴϴ�.
    QueryPerformanceFrequency(&m_frequency);
    // ���� �ð��� ����մϴ�.
    QueryPerformanceCounter(&m_prevTime);
}

PerformanceAnalyzer::~PerformanceAnalyzer()
{
}

void PerformanceAnalyzer::Update()
{
    // --- FPS ��� ���� ---
    LARGE_INTEGER currentTime;
    QueryPerformanceCounter(&currentTime);

    // ���� �����Ӱ��� �ð���(delta time)�� ����մϴ�.
    deltaTime = static_cast<float>(currentTime.QuadPart - m_prevTime.QuadPart) / m_frequency.QuadPart;
    m_prevTime = currentTime;

    m_frameCount++;
    m_elapsedTime += deltaTime;

    float interval = INTERVAL;
    // 1�ʰ� �������� FPS�� ����ϰ� �ʱ�ȭ�մϴ�.
    if (m_elapsedTime > interval)
    {
        m_avgfps = m_frameCount;
        m_frameCount = 0;
        m_elapsedTime -= interval;
    }
}
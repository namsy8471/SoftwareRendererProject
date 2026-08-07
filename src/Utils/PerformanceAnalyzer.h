#pragma once
#include <chrono>

class PerformanceAnalyzer
{
private:
	using Clock = std::chrono::steady_clock;
	Clock::time_point m_prevTime;
	Clock::duration m_elapsedTime{};
	int			  m_frameCount = 0;
	int			  m_avgfps = 0;

	Clock::duration m_deltaTime{};

public:
	PerformanceAnalyzer();
	~PerformanceAnalyzer() = default;

	void Update();
	[[nodiscard]] int GetAvgFPSForSecond() const noexcept { return m_avgfps; }
	[[nodiscard]] float GetDeltaTime() const noexcept
	{
		return std::chrono::duration<float>(m_deltaTime).count();
	}
};

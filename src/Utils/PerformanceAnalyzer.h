#pragma once
#include "Core/pch.h"

class PerformanceAnalyzer
{
private:
	LARGE_INTEGER m_frequency;
	LARGE_INTEGER m_prevTime;
	float	      m_elapsedTime = 0.0f;
	int			  m_frameCount = 0;
	int			  m_avgfps = 0;

	float deltaTime = 0; // deltaTime for game logic

	// TODO: Create CPU, GPU varibles
public:
	PerformanceAnalyzer();
	~PerformanceAnalyzer();

	void Update();
	int GetAvgFPSForSecond() const { return m_avgfps; }
	float GetDeltaTime() const { return deltaTime; }
};


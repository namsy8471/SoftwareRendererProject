#pragma once

#include "pch.h"
#include "PerformanceAnalyzer.h"
#include "Model.h"
#include <vector>

#define MAX_LOADSTRING 100

class Renderer;

class Framework
{
private:
	// Windows Variables
	HWND m_hWnd;                            // 윈도우 핸들
	HINSTANCE m_hInstance;					// 핸들 인스턴스

	WCHAR m_szTitle[MAX_LOADSTRING];        // 제목 표시줄 텍스트입니다.
	WCHAR m_szWindowClass[MAX_LOADSTRING];  // 기본 창 클래스 이름입니다.

	// Framework Variables
	std::unique_ptr<Renderer> m_pRenderer;	// It is for Rendering
	PerformanceAnalyzer m_perfAnalyzer;		// It is for counting FPS/CPU/GPU

	// Key Input Variables
	bool m_keys[256];
	bool m_isRightMouseDown = false;
	POINT m_lastMousePos;

	// Camera Variables
	SRMath::vec3 m_cameraPos = { 0.f, 0.f, 5.f };
	SRMath::vec3 m_cameraforward;
	float m_cameraYaw = 0.f;	// 좌우 회전 (Y축 기준)
	float m_cameraPitch = 0.f;	// 상하 회전 (X축 기준)

	// Model Variables
	std::vector<std::shared_ptr<Model>> m_models;
	std::shared_ptr<Model> m_model;

public:
	Framework();
	~Framework();

	bool Initialize(HINSTANCE, int);
	void Run();
	void Update();
	void Render();
	void Shutdown();

	LRESULT HandleMessage(HWND, UINT, WPARAM, LPARAM);

public:
	static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
	static INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
};

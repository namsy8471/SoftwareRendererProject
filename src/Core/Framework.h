#pragma once

#include <vector>
#include <string>
#include "Core/pch.h"
#include "Utils/PerformanceAnalyzer.h"
#include "Math/SRMath.h"
#include "Scene/Camera.h"
#include "Renderer/RenderQueue.h"
#include "Graphics/Light.h"
#include "Utils/DebugUtils.h"

#define MAX_LOADSTRING 100

class Renderer;
class GameObject;

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
	RenderQueue m_renderQueue;				// It is for Render Queue
	PerformanceAnalyzer m_perfAnalyzer;		// It is for counting FPS/CPU/GPU
	std::vector<DirectionalLight> m_lights; // It is for Directional Lights

	DebugFlags m_debugFlags;				// It is for Debug Flags

	// Key Input Variables
	bool m_keys[256];
	bool m_isRightMouseDown = false;
	POINT m_lastMousePos;

	// Model Variables
	std::shared_ptr<GameObject> m_gameobject;				// 현재 게임오브젝트
	std::vector<std::shared_ptr<GameObject>> m_gameobjects; // 게임오브젝트 리스트

	// Camera Variables
	Camera m_camera;

	// Load Gameobject
	bool initializeGameobject(const SRMath::vec3& pos, const SRMath::vec3& rotation, 
		const SRMath::vec3& scale, const std::string modelName);

public:
	Framework();
	~Framework();

	bool Initialize(HINSTANCE, int);
	void Run();
	void Update(float deltaTime);
	void Render();
	void Shutdown();

	LRESULT HandleMessage(HWND, UINT, WPARAM, LPARAM);

	void CheckMenuBox(bool isOn, const int& retFlag);

public:
	static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
	static INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
};

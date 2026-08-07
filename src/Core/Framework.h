#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>
#include <string>
#include <string_view>
#include "Platform/Win32Headers.h"
#include "Utils/PerformanceAnalyzer.h"
#include "Math/SRMath.h"
#include "Scene/Camera.h"
#include "Renderer/RenderQueue.h"
#include "Graphics/Light.h"
#include "Utils/DebugUtils.h"

class Renderer;
class GameObject;

class Framework
{
private:
	static constexpr std::size_t max_load_string = 100;
	static constexpr std::size_t key_count = 256;

	// Windows Variables
	HWND m_hWnd;                            // 윈도우 핸들
	HINSTANCE m_hInstance;					// 핸들 인스턴스

	// C++11 이전 스타일의 매크로/C 배열 대신 크기를 타입에 보존하는 array를 쓴다.
	std::array<WCHAR, max_load_string> m_szTitle{};
	std::array<WCHAR, max_load_string> m_szWindowClass{};

	// Framework Variables
	std::unique_ptr<Renderer> m_pRenderer;	// It is for Rendering
	RenderQueue m_renderQueue;				// It is for Render Queue
	PerformanceAnalyzer m_perfAnalyzer;		// It is for counting FPS/CPU/GPU
	std::vector<DirectionalLight> m_lights; // It is for Directional Lights

	DebugFlags m_debugFlags;				// It is for Debug Flags
	bool m_isRotateMode = true;		    // It is for Rotate Mode

	// Key Input Variables
	std::array<bool, key_count> m_keys{};
	bool m_isRightMouseDown = false;
	POINT m_lastMousePos;

	// Model Variables
	std::vector<std::shared_ptr<GameObject>> m_gameobjects; // 게임오브젝트 리스트

	// Camera Variables
	Camera m_camera;

	// Load Gameobject
	[[nodiscard]] bool initializeGameobject(const SRMath::vec3& pos, const SRMath::vec3& rotation,
		const SRMath::vec3& scale, std::string_view modelName);

public:
	explicit Framework(HINSTANCE hInstance, int nCmdShow);
	~Framework();

	void Run();
	void Update(float deltaTime);
	void Render();

	LRESULT HandleMessage(HWND, UINT, WPARAM, LPARAM);

	void CheckMenuBox(bool isOn, int menuId) const noexcept;

public:
	static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
	static INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
};

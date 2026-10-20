#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>
#include <string>
#include <string_view>
#include "Platform/Win32Headers.h"
#include "Platform/GdiPresenter.h"
#include "Renderer/RenderTarget.h"
#include "Utils/PerformanceAnalyzer.h"
#include "Core/DemoScene.h"

struct AssetLoadError;
class Renderer;

// Win32 창·메뉴·입력·GDI 표시만 담당한다. 장면 정의와 갱신은 DemoScene이 맡아
// headless srcli와 같은 장면을 공유한다.
class Framework
{
    friend struct FrameworkTestAccess;

  private:
    static constexpr std::size_t maxLoadString = 100;
    static constexpr std::size_t keyCount = 256;

    // Windows Variables
    HWND m_hWnd = nullptr;           // 윈도우 핸들
    HINSTANCE m_hInstance = nullptr; // 핸들 인스턴스

    std::array<WCHAR, maxLoadString> m_szTitle{};
    std::array<WCHAR, maxLoadString> m_szWindowClass{};

    // Framework Variables
    // 선언의 역순으로 파괴되어 renderer/target이 먼저 사라지고 마지막에 DIB가 해제된다.
    sr::platform::GdiPresenter m_presenter;
    RenderTarget m_renderTarget;
    std::unique_ptr<Renderer> m_renderer;
    PerformanceAnalyzer m_perfAnalyzer;
    DemoScene m_scene;

    // Key Input Variables
    std::array<bool, keyCount> m_keys{};
    bool m_isRightMouseDown = false;
    POINT m_lastMousePos{};

    void initializeWindow(HINSTANCE instance, int showCommand);
    [[nodiscard]] bool resizeRenderTarget(HWND window);
    void initializeScene();
    void runFrame();
    bool handleMenuCommand(int commandId);
    void handleMouseMove(LPARAM position);
    void handleKeyDown(WPARAM key, bool repeated);
    void updateFrameTime();
    void showAssetError(const AssetLoadError &error) const;

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

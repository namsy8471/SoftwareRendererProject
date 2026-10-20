#pragma once

#include <array>
#include <cstddef>
#include "Core/FrameRunner.h"
#include "Platform/GdiPresenter.h"
#include "Platform/Win32Headers.h"
#include "Renderer/RenderTarget.h"
#include "Utils/PerformanceAnalyzer.h"

struct AssetLoadError;

// Win32 창·메뉴·입력·GDI 표시만 담당한다. 장면 갱신과 렌더 순서는 FrameRunner가,
// 장면 구성은 DemoScene이 맡아 headless srcli와 같은 프레임을 그린다.
// 메뉴 명령은 handleMenuCommand(표 + 라디오 그룹), 창 메시지는 HandleMessage에서 받는다.
// 파일 열기·드롭 같은 비동기 작업의 결과는 FrameRunner의 프레임 시작 훅으로 장면에 넣는다.
class Win32App
{
    friend struct Win32AppTestAccess;

  public:
    explicit Win32App(HINSTANCE instance, int showCommand);
    ~Win32App();
    Win32App(const Win32App &) = delete;
    Win32App &operator=(const Win32App &) = delete;

    void Run();
    LRESULT HandleMessage(HWND, UINT, WPARAM, LPARAM);

    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    static INT_PTR CALLBACK About(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam);

  private:
    static constexpr std::size_t maxLoadString = 100;

    HWND m_hWnd = nullptr;
    HINSTANCE m_hInstance = nullptr;
    std::array<WCHAR, maxLoadString> m_szTitle{};
    std::array<WCHAR, maxLoadString> m_szWindowClass{};

    // 선언의 역순으로 파괴되어 runner/target이 먼저 사라지고 마지막에 DIB가 해제된다.
    sr::platform::GdiPresenter m_presenter;
    RenderTarget m_renderTarget;
    FrameRunner m_runner;
    PerformanceAnalyzer m_perfAnalyzer;

    std::array<bool, FrameRunner::keyCount> m_keys{};
    bool m_isRightMouseDown = false;
    // 생성자가 끝나기 전(CreateWindow/ShowWindow 중) 오는 WM_SIZE·메뉴 명령은 무시한다.
    bool m_isInitialized = false;
    POINT m_lastMousePos{};

    void initializeWindow(HINSTANCE instance, int showCommand);
    void initializeScene();
    [[nodiscard]] bool resizeRenderTarget(HWND window);
    void runFrame();
    void updateFrame(float deltaTime);
    void renderFrame();
    void presentFrame(HWND window);
    void updateFrameTime();
    bool handleMenuCommand(int commandId);
    void handleMouseMove(LPARAM position);
    void handleKeyDown(WPARAM key, bool repeated);
    void checkMenuItem(int menuId, bool checked) const noexcept;
    void showAssetError(const AssetLoadError &error) const;
};

#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <vector>
#include "Core/FrameRunner.h"
#include "IO/BackgroundLoader.h"
#include "Platform/GdiPresenter.h"
#include "Platform/Win32Headers.h"
#include "Renderer/RenderTarget.h"
#include "Utils/PerformanceAnalyzer.h"

#include "Utils/AssetLoadError.h"

class Model;
class GameObject;
struct AABB;
namespace sr::anim
{
class Animator;
}

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
    // runner보다 뒤에 선언해 먼저 파괴된다. 남은 apply(runner 장면 수정)는 실행되지 않고 버려진다.
    sr::io::BackgroundLoader m_loader;
    PerformanceAnalyzer m_perfAnalyzer;

    std::array<bool, FrameRunner::keyCount> m_keys{};
    bool m_isRightMouseDown = false;
    // 생성자가 끝나기 전(CreateWindow/ShowWindow 중) 오는 WM_SIZE·메뉴 명령은 무시한다.
    bool m_isInitialized = false;
    POINT m_lastMousePos{};
    // 프레임 시작 훅(Update 도중)에서 MessageBox를 띄우면 그 안의 메시지 루프가 WM_SIZE로 Update를
    // 다시 부른다. 로드 오류는 모아 두었다가 프레임을 다 그린 뒤 표시한다.
    std::vector<AssetLoadError> m_pendingAssetErrors;
    // 애니메이션 메뉴·단축키가 조작하는 재생기: 마지막으로 연 클립 있는 glTF. 장면이 소유한다.
    std::weak_ptr<sr::anim::Animator> m_activeAnimator;
    HMENU m_animationMenu = nullptr; // 메뉴 막대가 소유한다(창과 함께 파괴)

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
    void showPendingAssetErrors();
    // 드롭·열기 대화상자 공통 진입점. 파싱은 m_loader 작업 스레드, 장면 추가는 프레임 시작 훅에서 한다.
    void loadModelAsync(std::filesystem::path path);
    // 경계 상자 중심이 카메라 앞에 오고 가장 긴 변이 일정한 크기가 되도록 object의 위치·크기를 정해 장면에 넣는다.
    // 장면에 넣지 못하면 오류를 쌓고 false.
    bool placeLoadedObject(std::shared_ptr<GameObject> object, const AABB &bounds);
    void initializeAnimationMenu();
    // 재생/반복 체크, 클립 목록, 활성 여부를 m_activeAnimator 상태에 맞춘다.
    void refreshAnimationMenu();
    // 애니메이션 명령이면 처리하고 true.
    bool handleAnimationCommand(int commandId);
    void showOpenModelDialog();
    void handleDroppedFiles(WPARAM dropHandle); // WM_DROPFILES의 HDROP
};

#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <utility>
#include <vector>

// Win32 창이 필요한 테스트(Win32App, GdiPresenter)는 MSBuild 테스트에서만 실행한다.
// CMake 빌드는 SR_HEADLESS_ONLY를 정의하므로 Linux/클라우드에서도 나머지 전부를 돌린다.
#if defined(_WIN32) && !defined(SR_HEADLESS_ONLY)
#define SR_TEST_WIN32_APP 1
#else
#define SR_TEST_WIN32_APP 0
#endif

namespace sr::test
{
struct TestContext
{
    // 저장소 루트. assets/, tests/fixtures/, tests/golden/을 여기서 찾는다.
    std::filesystem::path root;
    // --update-golden: 골든 테스트가 비교 대신 tests/golden/*.png를 새로 쓴다.
    bool updateGolden = false;
    // 실패한 골든의 actual/diff PNG를 남기는 곳(기본 <root>/tests/out).
    std::filesystem::path outputDirectory;
};

struct TestCase
{
    // "영역.이름" 형식. --filter는 이 문자열의 부분 일치로 고른다.
    std::string name;
    std::function<void(const TestContext &)> run;
};

// 등록 순서대로 실행한다. 실패는 예외로 알리고, 실행기는 실패 후에도 나머지를 계속 돌린다.
class TestRegistry
{
  public:
    void Add(std::string name, std::function<void(const TestContext &)> run)
    {
        m_cases.push_back({std::move(name), std::move(run)});
    }
    void Add(std::string name, std::function<void()> run)
    {
        m_cases.push_back({std::move(name), [run = std::move(run)](const TestContext &) { run(); }});
    }
    [[nodiscard]] const std::vector<TestCase> &Cases() const noexcept { return m_cases; }

  private:
    std::vector<TestCase> m_cases;
};

// 영역별 파일이 정의한다. 새 테스트는 해당 파일의 Register 함수에 한 줄 추가한다.
void RegisterMathTests(TestRegistry &);
void RegisterAssetTests(TestRegistry &);
void RegisterRendererTests(TestRegistry &);
void RegisterRenderTargetTests(TestRegistry &);
void RegisterVisibilityTests(TestRegistry &);
void RegisterGoldenTests(TestRegistry &);
void RegisterIOTests(TestRegistry &);
void RegisterGltfTests(TestRegistry &);
void RegisterAnimationTests(TestRegistry &);
#if SR_TEST_WIN32_APP
void RegisterWin32AppTests(TestRegistry &);
#endif
} // namespace sr::test

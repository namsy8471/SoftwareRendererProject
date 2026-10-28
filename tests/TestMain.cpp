// RendererTests 실행기.
//   RendererTests [root] [--filter TEXT] [--list] [--update-golden] [--out DIR]
//   RendererTests [root] --benchmark ...            (Windows: 숨긴 Win32App/GDI 경로)
//   RendererTests [root] --visibility-benchmark ... (합성 가림 장면)
// root 기본값은 현재 폴더다. 출력은 테스트마다 PASS/FAIL 한 줄과 마지막 SUMMARY 한 줄이다.
#include "Graphics/AssetParsing.h"
#include "TestHarness.h"

#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

void RunVisibilityBenchmark(int argc, char **argv);
#if SR_TEST_WIN32_APP
void RunWin32AppBenchmark(int frames, int width, int height, std::string_view mode, std::string_view optimization,
                           bool stats, bool hiddenWireframe);
#endif

namespace
{
using namespace sr::test;

#if SR_TEST_WIN32_APP
// 기존 위치 인자 형식을 유지한다: --benchmark frames width height mode opt [stats|nostats] [hidden]
int runWin32AppBenchmark(int argc, char **argv)
{
    const auto integer = [&](int index, int fallback)
    {
        if (argc <= index)
            return fallback;
        const auto value = sr::asset::parseNumber<int>(argv[index]);
        if (!value)
            throw std::runtime_error("invalid benchmark integer");
        return *value;
    };
    bool stats = false, hiddenWireframe = false;
    int optional = 8;
    if (argc > optional &&
        (std::string_view(argv[optional]) == "stats" || std::string_view(argv[optional]) == "nostats"))
        stats = std::string_view(argv[optional++]) == "stats";
    if (argc > optional)
    {
        if (std::string_view(argv[optional++]) != "hidden")
            throw std::runtime_error("unknown benchmark hidden-wire option");
        hiddenWireframe = true;
    }
    if (argc > optional)
        throw std::runtime_error("unexpected benchmark trailing argument");
    RunWin32AppBenchmark(integer(3, 600), integer(4, 1280), integer(5, 720), argc > 6 ? argv[6] : "fill",
                          argc > 7 ? argv[7] : "none", stats, hiddenWireframe);
    return 0;
}
#endif
} // namespace

int main(int argc, char **argv)
{
    try
    {
        TestContext context;
        context.root = std::filesystem::current_path();
        int first = 1;
        if (argc > 1 && !std::string_view(argv[1]).starts_with("--"))
        {
            context.root = argv[1];
            first = 2;
        }
        if (argc > first && std::string_view(argv[first]) == "--visibility-benchmark")
        {
            RunVisibilityBenchmark(argc, argv);
            return 0;
        }
        if (argc > first && std::string_view(argv[first]) == "--benchmark")
        {
#if SR_TEST_WIN32_APP
            return runWin32AppBenchmark(argc, argv);
#else
            std::cerr << "--benchmark needs the Win32App build; use srcli bench instead.\n";
            return 2;
#endif
        }

        std::string filter;
        bool listOnly = false;
        for (int index = first; index < argc; ++index)
        {
            const std::string_view argument = argv[index];
            if (argument == "--filter" && index + 1 < argc)
                filter = argv[++index];
            else if (argument == "--list")
                listOnly = true;
            else if (argument == "--update-golden")
                context.updateGolden = true;
            else if (argument == "--out" && index + 1 < argc)
                context.outputDirectory = argv[++index];
            else
            {
                std::cerr << "unknown argument: " << argument << '\n';
                return 2;
            }
        }
        if (context.outputDirectory.empty())
            context.outputDirectory = context.root / "tests/out";

        TestRegistry registry;
        RegisterMathTests(registry);
        RegisterJobsTests(registry);
        RegisterAssetTests(registry);
        RegisterImageCodecTests(registry);
        RegisterRenderTargetTests(registry);
        RegisterVisibilityTests(registry);
        RegisterRendererTests(registry);
        RegisterGoldenTests(registry);
        RegisterIOTests(registry);
        RegisterGltfTests(registry);
        RegisterAnimationTests(registry);
        RegisterProjectTests(registry);
        RegisterBakerTests(registry);
#if SR_TEST_WIN32_APP
        RegisterWin32AppTests(registry);
#endif

        int passed = 0, failed = 0;
        std::vector<std::string> failures;
        for (const auto &test : registry.Cases())
        {
            if (!filter.empty() && test.name.find(filter) == std::string::npos)
                continue;
            if (listOnly)
            {
                std::cout << test.name << '\n';
                continue;
            }
            const auto start = std::chrono::steady_clock::now();
            try
            {
                test.run(context);
                const double ms =
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                std::printf("PASS %s (%.0f ms)\n", test.name.c_str(), ms);
                ++passed;
            }
            catch (const std::exception &error)
            {
                std::printf("FAIL %s: %s\n", test.name.c_str(), error.what());
                failures.push_back(test.name);
                ++failed;
            }
            std::fflush(stdout);
        }
        if (listOnly)
            return 0;
        if (passed + failed == 0)
        {
            std::cerr << "no test matches filter: " << filter << '\n';
            return 2;
        }
        std::printf("SUMMARY passed=%d failed=%d\n", passed, failed);
        for (const auto &name : failures)
            std::printf("FAILED %s\n", name.c_str());
        return failed == 0 ? 0 : 1;
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}

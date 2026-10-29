// srcli: 창 없이 데모 장면을 렌더링하는 명령행 도구.
//   srcli bench  [옵션]              프레임 시간 측정. --json이면 한 줄 JSON만 출력한다.
//   srcli render [옵션] --out a.png  마지막 프레임을 PNG로 저장한다.
//   srcli bake   --high <high_poly_mesh> --low <low_poly_mesh> --size <WxH> --out-normal <png> --out-ao <png>
// 종료 코드: 0 성공, 1 실행 실패, 2 잘못된 인자, 3 bench가 --target-fps를 못 맞춤(--require-target일 때).
#include "Core/HeadlessRunner.h"
#include "Graphics/AssetParsing.h"
#include "Graphics/GltfLoader.h"
#include "Graphics/ModelLoader.h"
#include "Math/SIMD.h"
#include "Renderer/Baker/Baker.h"
#include "Scene/GameObject.h"
#include "Utils/ImageFile.h"
#include "Utils/Jobs.h"
#include "Utils/Utils.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
using sr::headless::RunConfig;

constexpr std::string_view usage = R"(usage:
  srcli bench  [options]
  srcli render [options] --out <file.png>
  srcli bake   --high <high_poly_mesh> --low <low_poly_mesh> --size <WxH> --out-normal <png> --out-ao <png>

options:
  --size WxH          render / bake size (default 1920x1080 for render, 1024x1024 for bake)
  --frames N          measured frames (bench default 600, render default 1)
  --warmup N          unmeasured frames before measuring (default 60)
  --mode M            fill | wire | normal | aabb | fxaa (default fill)
  --opt O             none | sort | hiz | both (default none)
  --hidden-wire       hidden-line wireframe (depth prepass)
  --bilinear          enable bilinear texture filtering
  --mipmap            enable mipmap LOD sampling
  --srgb              enable sRGB gamma correction
  --shadows           enable directional & spot light shadow mapping (2x2 PCF)
  --pbr               enable physically based rendering (glTF metallic-roughness)
  --shading S         auto | phong | pbr (default auto)
  --ibl               enable image-based lighting (diffuse SH + prefiltered specular)
  --tonemap           enable ACES film tone mapping
  --dda               DDA lines instead of Bresenham
  --stats             collect renderer counters and pass timings (slower)
  --threads N         limit sr::jobs parallelism (default: all cores)
  --load-every N      bench: load teapot.obj in the background every N frames (results discarded)
  --scene S           grid | helmet | terrain | overdraw | arena | <file.gltf>
  --clip N            animation clip index for --scene (default 0)
  --assets DIR        asset folder (default: <exe dir>/assets, then repo assets/, then ./assets)
  --target-fps F      fps goal reported in output (default 100)
  --require-target    bench exits with 3 when mean fps < target
  --json              bench prints exactly one JSON line
  --out FILE          PNG path (render; optional for bench)

bake options:
  --high FILE         high-poly source mesh (OBJ / glTF)
  --low FILE          low-poly target mesh (OBJ / glTF)
  --out-normal FILE   output normal map PNG path
  --out-ao FILE       output AO map PNG path
  --samples N         AO hemisphere cosine-weighted samples (default 64)
  --cage-dist D       cage ray bidirectional distance along normal (default 0.05)
  --ao-dist D         AO maximum ray occlusion distance (default 10.0)
  --dilate N          pixel dilation filter radius (default 8)
)";

struct BakeCliOptions
{
    std::filesystem::path highPath;
    std::filesystem::path lowPath;
    int width = 1024;
    int height = 1024;
    int samples = 64;
    float cageDist = 0.05f;
    float aoDist = 10.0f;
    int dilate = 8;
    std::optional<std::filesystem::path> outNormal;
    std::optional<std::filesystem::path> outAO;
    int threads = 0;
};

struct Options
{
    std::string command;
    RunConfig config;
    BakeCliOptions bakeConfig;
    bool json = false;
    bool requireTarget = false;
    bool framesGiven = false;
    bool optGiven = false;
    double targetFps = 100.0;
    std::optional<std::filesystem::path> outPath;
};

[[noreturn]] void usageError(std::string_view message)
{
    std::cerr << "srcli: " << message << "\n\n" << usage;
    std::exit(2);
}

template <class Number> Number parseOrDie(std::string_view text, std::string_view what)
{
    const auto value = sr::asset::parseNumber<Number>(text);
    if (!value)
        usageError("invalid " + std::string(what) + ": " + std::string(text));
    return *value;
}

std::filesystem::path defaultAssetDirectory()
{
    std::error_code error;
    auto besideExe = GetExecutableDirectory() / "assets";
    if (std::filesystem::exists(besideExe / "IronMan.obj", error))
        return besideExe;
#if defined(SR_SOURCE_ASSET_DIR)
    // CMake 빌드는 자산을 복사하지 않고 저장소의 assets/를 직접 읽는다.
    if (std::filesystem::exists(std::filesystem::path(SR_SOURCE_ASSET_DIR) / "IronMan.obj", error))
        return SR_SOURCE_ASSET_DIR;
#endif
    return std::filesystem::current_path() / "assets";
}

Options parseArguments(int argc, char **argv)
{
    if (argc < 2)
        usageError("missing command");
    Options options;
    options.command = argv[1];
    if (options.command != "bench" && options.command != "render" && options.command != "bake")
        usageError("unknown command: " + options.command);
    for (int index = 2; index < argc; ++index)
    {
        const std::string_view argument = argv[index];
        const auto next = [&]() -> std::string_view
        {
            if (index + 1 >= argc)
                usageError("missing value for " + std::string(argument));
            return argv[++index];
        };
        auto &config = options.config;
        if (argument == "--size")
        {
            const auto text = next();
            const auto separator = text.find('x');
            if (separator == std::string_view::npos)
                usageError("size must be WxH");
            config.width = parseOrDie<int>(text.substr(0, separator), "width");
            config.height = parseOrDie<int>(text.substr(separator + 1), "height");
            options.bakeConfig.width = config.width;
            options.bakeConfig.height = config.height;
        }
        else if (argument == "--frames")
        {
            config.measuredFrames = parseOrDie<int>(next(), "frames");
            options.framesGiven = true;
        }
        else if (argument == "--warmup")
            config.warmupFrames = parseOrDie<int>(next(), "warmup");
        else if (argument == "--mode")
        {
            const auto text = next();
            const auto mode = sr::headless::ParseViewMode(text);
            if (!mode)
                usageError("unknown mode: " + std::string(text));
            config.mode = *mode;
        }
        else if (argument == "--opt")
        {
            options.optGiven = true;
            const auto text = next();
            if (text != "none" && text != "sort" && text != "hiz" && text != "both")
                usageError("unknown opt: " + std::string(text));
            config.options.frontToBack = text == "sort" || text == "both";
            config.options.hiZ = text == "hiz" || text == "both";
        }
        else if (argument == "--hidden-wire")
            config.options.hiddenWireframe = true;
        else if (argument == "--bilinear")
            config.options.bilinearFiltering = true;
        else if (argument == "--mipmap")
            config.options.mipmapping = true;
        else if (argument == "--srgb")
            config.options.srgbGamma = true;
        else if (argument == "--shadows")
            config.options.shadows = true;
        else if (argument == "--pbr")
            config.options.pbr = true;
        else if (argument == "--shading")
        {
            const auto text = next();
            if (text == "phong")
                config.options.shadingModel = EShadingModel::Phong;
            else if (text == "pbr")
                config.options.shadingModel = EShadingModel::PBR;
            else if (text == "auto")
                config.options.shadingModel = EShadingModel::Auto;
            else
                usageError("unknown shading model: " + std::string(text));
        }
        else if (argument == "--ibl")
            config.options.ibl = true;
        else if (argument == "--tonemap")
            config.options.tonemap = true;
        else if (argument == "--dda")
            config.lineAlgorithm = ELineAlgorithm::DDA;
        else if (argument == "--stats")
            config.options.collectStats = true;
        else if (argument == "--threads")
        {
            config.threads = parseOrDie<int>(next(), "threads");
            options.bakeConfig.threads = config.threads;
        }
        else if (argument == "--load-every")
            config.backgroundLoadInterval = parseOrDie<int>(next(), "load interval");
        else if (argument == "--scene")
            config.scenePath = std::filesystem::path(next());
        else if (argument == "--clip")
            config.clip = parseOrDie<int>(next(), "clip");
        else if (argument == "--assets")
            config.assetDirectory = next();
        else if (argument == "--target-fps")
            options.targetFps = parseOrDie<double>(next(), "target fps");
        else if (argument == "--require-target")
            options.requireTarget = true;
        else if (argument == "--json")
            options.json = true;
        else if (argument == "--out")
            options.outPath = std::filesystem::path(next());
        else if (argument == "--high")
            options.bakeConfig.highPath = std::filesystem::path(next());
        else if (argument == "--low")
            options.bakeConfig.lowPath = std::filesystem::path(next());
        else if (argument == "--out-normal")
            options.bakeConfig.outNormal = std::filesystem::path(next());
        else if (argument == "--out-ao")
            options.bakeConfig.outAO = std::filesystem::path(next());
        else if (argument == "--samples")
            options.bakeConfig.samples = parseOrDie<int>(next(), "samples");
        else if (argument == "--cage-dist")
            options.bakeConfig.cageDist = parseOrDie<float>(next(), "cage distance");
        else if (argument == "--ao-dist")
            options.bakeConfig.aoDist = parseOrDie<float>(next(), "ao distance");
        else if (argument == "--dilate")
            options.bakeConfig.dilate = parseOrDie<int>(next(), "dilation radius");
        else
            usageError("unknown option: " + std::string(argument));
    }
    if (options.command == "render")
    {
        if (!options.outPath)
            usageError("render needs --out <file.png>");
        if (!options.framesGiven)
            options.config.measuredFrames = 1;
    }
    else if (options.command == "bake")
    {
        if (options.bakeConfig.highPath.empty())
            usageError("bake needs --high <high_poly_mesh>");
        if (options.bakeConfig.lowPath.empty())
            usageError("bake needs --low <low_poly_mesh>");
        if (!options.bakeConfig.outNormal && !options.bakeConfig.outAO)
            usageError("bake needs at least one of --out-normal <png> or --out-ao <png>");
    }
    if (options.command == "bench" && !options.optGiven)
        options.config.options.hiZ = true;
    if (options.config.assetDirectory.empty())
        options.config.assetDirectory = defaultAssetDirectory();
    return options;
}

std::string_view optimizationName(const RenderOptions &options)
{
    if (options.frontToBack && options.hiZ)
        return "both";
    if (options.frontToBack)
        return "sort";
    if (options.hiZ)
        return "hiz";
    return "none";
}

std::string_view buildType()
{
#if defined(NDEBUG)
    return "release";
#else
    return "debug";
#endif
}

std::string_view compilerName()
{
#if defined(_MSC_VER) && !defined(__clang__)
    return "msvc";
#elif defined(__clang__)
    return "clang";
#elif defined(__GNUC__)
    return "gcc";
#else
    return "unknown";
#endif
}

std::string jsonEscape(std::string_view text)
{
    std::string escaped;
    for (const char character : text)
    {
        if (character == '"' || character == '\\')
            escaped += '\\';
        escaped += character;
    }
    return escaped;
}

std::string toJson(const Options &options, const sr::headless::RunResult &result)
{
    const auto &config = options.config;
    const auto &timing = result.timings;
    const double fpsMean = 1000.0 / timing.meanMs;
    std::ostringstream out;
    out.precision(4);
    out << std::fixed;
    out << "{\"tool\":\"srcli\",\"command\":\"" << options.command << "\""
        << ",\"build\":\"" << buildType() << "\",\"compiler\":\"" << compilerName() << "\""
        << ",\"simd\":\"" << (SRMath::SIMD::avx_available() ? "avx" : "sse") << "\""
        << ",\"threads\":" << result.threads << ",\"width\":" << config.width << ",\"height\":" << config.height
        << ",\"mode\":\"" << sr::headless::ToString(config.mode) << "\""
        << ",\"opt\":\"" << optimizationName(config.options) << "\""
        << ",\"hidden_wire\":" << (config.options.hiddenWireframe ? "true" : "false")
        << ",\"line\":\"" << (config.lineAlgorithm == ELineAlgorithm::DDA ? "dda" : "bresenham") << "\""
        << ",\"warmup\":" << config.warmupFrames << ",\"frames\":" << config.measuredFrames
        << ",\"mean_ms\":" << timing.meanMs << ",\"median_ms\":" << timing.medianMs
        << ",\"p95_ms\":" << timing.p95Ms << ",\"min_ms\":" << timing.minMs << ",\"max_ms\":" << timing.maxMs
        << ",\"update_ms\":" << timing.updateMeanMs << ",\"render_ms\":" << timing.renderMeanMs
        << ",\"fps_mean\":" << fpsMean << ",\"fps_p95\":" << 1000.0 / timing.p95Ms
        << ",\"target_fps\":" << options.targetFps
        << ",\"meets_target\":" << (fpsMean >= options.targetFps ? "true" : "false")
        << ",\"pixel_hash\":\"" << result.pixelHash << "\"";
    if (config.backgroundLoadInterval > 0)
        out << ",\"load_every\":" << config.backgroundLoadInterval
            << ",\"loads_submitted\":" << result.backgroundLoadsSubmitted
            << ",\"loads_applied\":" << result.backgroundLoadsApplied;
    if (!config.scenePath.empty())
        out << ",\"scene\":\"" << jsonEscape(config.scenePath.generic_string()) << "\""
            << ",\"clip\":\"" << jsonEscape(result.clipName) << "\""
            << ",\"skinned_vertices\":" << result.skinnedVertices << ",\"joints\":" << result.skinJoints
            << ",\"anim_ms\":" << timing.animationMeanMs;
    if (options.outPath)
        out << ",\"png\":\"" << jsonEscape(options.outPath->generic_string()) << "\"";
    if (config.options.collectStats)
    {
        const auto &stats = result.stats;
        out << ",\"stats\":{\"prepared_triangles\":" << stats.preparedTriangles
            << ",\"bin_references\":" << stats.binReferences << ",\"coverage_passed\":" << stats.coveragePassed
            << ",\"depth_rejected\":" << stats.depthRejected << ",\"shaded_pixels\":" << stats.shadedPixels
            << ",\"final_filled_pixels\":" << stats.finalFilledPixels << ",\"hiz_tests\":" << stats.hiZTests
            << ",\"hiz_rejected\":" << stats.hiZRejected << ",\"line_depth_rejected\":" << stats.lineDepthRejected
            << ",\"prepare_ms\":" << stats.prepareMs << ",\"bin_ms\":" << stats.binMs
            << ",\"raster_ms\":" << stats.rasterMs << ",\"line_ms\":" << stats.lineMs
            << ",\"post_ms\":" << stats.postMs << "}";
    }
    out << "}";
    return out.str();
}

int runBake(const Options &options)
{
    const auto &opt = options.bakeConfig;
    std::optional<sr::jobs::ConcurrencyLimit> threadLimit;
    if (opt.threads > 0)
        threadLimit.emplace(static_cast<std::size_t>(opt.threads));

    const auto loadModel = [](const std::filesystem::path &path) -> std::unique_ptr<Model> {
        const auto ext = path.extension().string();
        if (ext == ".gltf" || ext == ".glb")
        {
            sr::gltf::SceneLoadOptions gltfOpts;
            auto gltfScene = sr::gltf::LoadScene(path, gltfOpts);
            if (gltfScene)
            {
                std::vector<Mesh> meshes;
                for (const auto &node : gltfScene->nodes)
                {
                    if (node.object && node.object->GetModel())
                    {
                        for (const auto &m : node.object->GetModel()->GetMeshes())
                        {
                            Mesh copy;
                            copy.vertices = m.vertices;
                            copy.indices = m.indices;
                            copy.localAABB = m.localAABB;
                            meshes.push_back(std::move(copy));
                        }
                    }
                }
                if (!meshes.empty())
                    return ModelLoader::FromMeshes(std::move(meshes));
            }
        }
        auto obj = ModelLoader::LoadOBJ(path);
        if (obj)
            return std::move(*obj);
        return nullptr;
    };

    auto highModel = loadModel(opt.highPath);
    if (!highModel)
    {
        std::cerr << "srcli bake: failed to load high-poly mesh: " << opt.highPath.string() << '\n';
        return 1;
    }

    auto lowModel = loadModel(opt.lowPath);
    if (!lowModel)
    {
        std::cerr << "srcli bake: failed to load low-poly mesh: " << opt.lowPath.string() << '\n';
        return 1;
    }

    sr::baker::BakeOptions bakeOptions;
    bakeOptions.width = opt.width;
    bakeOptions.height = opt.height;
    bakeOptions.aoSamples = opt.samples;
    bakeOptions.cageDistance = opt.cageDist;
    bakeOptions.aoMaxDistance = opt.aoDist;
    bakeOptions.dilationRadius = opt.dilate;
    bakeOptions.bakeNormal = opt.outNormal.has_value();
    bakeOptions.bakeAO = opt.outAO.has_value();

    const auto start = std::chrono::steady_clock::now();
    const auto result = sr::baker::Bake(*highModel, *lowModel, bakeOptions);
    const double elapsedMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

    if (opt.outNormal)
    {
        if (const auto written = sr::image::WritePng(*opt.outNormal, result.normalMap); !written)
        {
            std::cerr << "srcli bake: " << written.error() << '\n';
            return 1;
        }
        std::printf("wrote normal map: %s (%dx%d)\n", opt.outNormal->string().c_str(), opt.width, opt.height);
    }

    if (opt.outAO)
    {
        if (const auto written = sr::image::WritePng(*opt.outAO, result.aoMap); !written)
        {
            std::cerr << "srcli bake: " << written.error() << '\n';
            return 1;
        }
        std::printf("wrote AO map: %s (%dx%d)\n", opt.outAO->string().c_str(), opt.width, opt.height);
    }

    std::printf("bake completed in %.1f ms\n", elapsedMs);
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    const Options options = parseArguments(argc, argv);
    if (options.command == "bake")
    {
        return runBake(options);
    }

    const auto result = sr::headless::Run(options.config);
    if (!result)
    {
        std::cerr << "srcli: " << result.error() << '\n';
        return 1;
    }
    if (options.outPath)
    {
        if (const auto written = sr::image::WritePng(*options.outPath, result->lastFrame); !written)
        {
            std::cerr << "srcli: " << written.error() << '\n';
            return 1;
        }
    }

    const double fpsMean = 1000.0 / result->timings.meanMs;
    if (options.json)
        std::cout << toJson(options, *result) << '\n';
    else if (options.command == "bench")
    {
        std::printf("%s %dx%d %s: mean %.3f ms (%.1f fps), p95 %.3f ms, update %.3f ms, render %.3f ms, "
                    "target %.0f fps %s\n",
                    std::string(sr::headless::ToString(options.config.mode)).c_str(), options.config.width,
                    options.config.height, std::string(optimizationName(options.config.options)).c_str(),
                    result->timings.meanMs, fpsMean, result->timings.p95Ms, result->timings.updateMeanMs,
                    result->timings.renderMeanMs, options.targetFps,
                    fpsMean >= options.targetFps ? "MET" : "NOT MET");
        if (options.outPath)
            std::printf("wrote %s\n", options.outPath->string().c_str());
    }
    else
        std::printf("wrote %s (%dx%d, hash %llu)\n", options.outPath->string().c_str(), options.config.width,
                    options.config.height, static_cast<unsigned long long>(result->pixelHash));

    if (options.command == "bench" && options.requireTarget && fpsMean < options.targetFps)
        return 3;
    return 0;
}

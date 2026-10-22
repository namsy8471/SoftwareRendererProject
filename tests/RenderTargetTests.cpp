#include "Renderer/RenderTarget.h"
#include "Renderer/FXAA.h"
#include "Renderer/Rasterizer.h"
#include "Renderer/RenderCommand.h"
#include "TestHarness.h"
#if SR_TEST_WIN32_APP
#include "Platform/GdiPresenter.h"
#endif

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "Utils/Jobs.h"

namespace
{
// Release에서도 생략되지 않는 검사다. Renderer나 장면 자산 없이 버퍼 계약만 검증한다.
void require(bool condition, std::string_view message)
{
    if (!condition)
        throw std::runtime_error(std::string(message));
}

void requireEmpty(const RenderTarget &target)
{
    require(target.Empty() && target.Width() == 0 && target.Height() == 0, "empty target dimensions");
    require(target.Surface().pixels.empty() && target.Surface().stridePixels == 0, "empty target surface");
    require(target.Depth().empty() && target.PrimitiveIds().empty(), "empty target depth and owners");
}

void attachmentValidation()
{
    RenderTarget target;
    requireEmpty(target);
    target.Clear();
    std::array<unsigned int, 16> storage{};

    const auto failsClosed = [&](PixelSurfaceView invalid, RenderTargetError expectedError)
    {
        storage.fill(0x00234567u);
        require(target.Attach({storage, 4, 4, 4}).has_value(), "attach before failure");
        target.Depth()[0] = 12.0f;
        target.PrimitiveIds()[0] = 9;
        const auto result = target.Attach(invalid);
        require(!result && result.error() == expectedError, "attachment returns expected error");
        requireEmpty(target);
        target.Clear();
        require(std::ranges::all_of(storage, [](auto pixel) { return pixel == 0x00234567u; }),
                "failed attachment must not write old color storage");
    };

    failsClosed({storage, -1, 4, 4}, RenderTargetError::InvalidDimensions);
    failsClosed({storage, 4, -1, 4}, RenderTargetError::InvalidDimensions);
    failsClosed({storage, 0, -1, 0}, RenderTargetError::InvalidDimensions);
    failsClosed({storage, 4, 4, 3}, RenderTargetError::InvalidStride);
    failsClosed({storage, 1, 1, 0}, RenderTargetError::InvalidStride);
    failsClosed({std::span<unsigned int>{storage}.first(15), 4, 4, 4},
                 RenderTargetError::InsufficientStorage);
    failsClosed({{}, 1, 1, 1}, RenderTargetError::InsufficientStorage);

    // 실제 메모리를 대량 할당하지 않고 stride 덧셈/곱셈과 각 소유 버퍼의 상한을 검사한다.
    constexpr auto maximum = std::numeric_limits<std::size_t>::max();
    failsClosed({{}, 1, 3, maximum}, RenderTargetError::SizeOverflow);
    failsClosed({{}, 1, 2, maximum / sizeof(unsigned int) + 1}, RenderTargetError::SizeOverflow);
    constexpr int maximumDimension = std::numeric_limits<int>::max();
    failsClosed({{}, maximumDimension, maximumDimension, static_cast<std::size_t>(maximumDimension)},
                 RenderTargetError::SizeOverflow);

    require(target.Attach({storage, 0, 7, 0}).has_value(), "zero width is an empty target");
    requireEmpty(target);
    require(target.Attach({storage, 7, 0, 0}).has_value(), "zero height is an empty target");
    requireEmpty(target);
    require(target.Attach({}).has_value(), "zero by zero target");
    requireEmpty(target);
    // bad_alloc 강제 주입은 공용 allocator를 바꾸므로 이 테스트에서는 의도적으로 하지 않는다.
}

void clearAndStride()
{
    constexpr unsigned int sentinel = 0xDEADBEEFu;
    std::array<unsigned int, 15> storage;
    storage.fill(sentinel);
    RenderTarget target;
    require(target.Attach({storage, 3, 3, 5}).has_value(), "padded target attachment");
    require(target.Surface().pixels.data() == storage.data(), "target borrows original color storage");
    require(target.Depth().size() == 9 && target.PrimitiveIds().size() == 9, "depth and owners are tight");
    require(std::ranges::all_of(target.Depth(), [](auto value) { return value == RenderTarget::emptyDepth; }),
            "new depth starts empty");
    require(std::ranges::all_of(target.PrimitiveIds(), [](auto value) { return value == RenderTarget::emptyOwner; }),
            "new owners start empty");

    std::ranges::fill(target.Depth(), 1.25f);
    std::ranges::fill(target.PrimitiveIds(), std::uint64_t{17});
    target.Clear();
    for (std::size_t y = 0; y < 3; ++y)
        for (std::size_t x = 0; x < 5; ++x)
            require(storage[y * 5 + x] == (x < 3 ? 0u : sentinel), "clear preserves color row padding");
    require(std::ranges::all_of(target.Depth(), [](auto value) { return value == RenderTarget::emptyDepth; }),
            "clear resets all depth pixels");
    require(std::ranges::all_of(target.PrimitiveIds(), [](auto value) { return value == RenderTarget::emptyOwner; }),
            "clear resets all primitive owners");

    const RenderTarget &readOnly = target;
    require(readOnly.Surface().pixels.data() == storage.data() && readOnly.Surface().stridePixels == 5,
            "const surface retains original layout");
    require(readOnly.Depth().data() == target.Depth().data() &&
                readOnly.PrimitiveIds().data() == target.PrimitiveIds().data(),
            "const depth and owner views share storage");

    // 마지막 행 뒤 padding은 접근하지 않으므로 저장소 길이에 포함될 필요가 없다.
    target.Detach();
    storage.fill(sentinel);
    require(target.Attach({std::span<unsigned int>{storage}.first(13), 3, 3, 5}).has_value(),
            "last row needs only its active pixels");
    target.Clear();
    require(storage[12] == 0 && storage[13] == sentinel && storage[14] == sentinel,
            "minimal final row storage bounds");

    for (int iteration = 0; iteration < 100; ++iteration)
    {
        target.Detach();
        target.Detach();
        requireEmpty(target);
        const int width = iteration % 4 + 1;
        require(target.Attach({storage, width, 3, 5}).has_value(), "repeated attachment");
        target.Clear();
        require(target.Depth().size() == static_cast<std::size_t>(width * 3), "repeated attachment size");
    }
    target.Detach();
    requireEmpty(target);
}

void fxaaStrides()
{
    constexpr int width = 9, height = 7;
    constexpr std::size_t inputStride = 13, outputStride = 15;
    constexpr unsigned int sentinel = 0xDEADBEEFu;
    constexpr std::array<unsigned int, 6> palette = {
        0x00000000u, 0x00FFFFFFu, 0x00FF0000u, 0x000000FFu, 0x0000A020u, 0x008010D0u};
    std::vector<unsigned int> tightInput(static_cast<std::size_t>(width * height));
    std::vector<unsigned int> tightOutput(tightInput.size(), sentinel);
    // 입력은 최소 길이만, 출력은 마지막 padding까지 확보해 서로 다른 stride를 검증한다.
    std::vector<unsigned int> paddedInput((height - 1) * inputStride + width, sentinel);
    std::vector<unsigned int> paddedOutput(height * outputStride, sentinel);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            const auto color = palette[static_cast<std::size_t>(x * x + y * 3 + y * y) % palette.size()];
            tightInput[static_cast<std::size_t>(y * width + x)] = color;
            paddedInput[static_cast<std::size_t>(y) * inputStride + x] = color;
        }
    const auto originalInput = paddedInput;
    sr::fxaa::Apply(tightInput, tightOutput, width, height);
    sr::fxaa::Apply(ConstPixelSurfaceView{paddedInput, width, height, inputStride},
                     PixelSurfaceView{paddedOutput, width, height, outputStride});
    for (int y = 0; y < height; ++y)
        for (std::size_t x = 0; x < outputStride; ++x)
            require(paddedOutput[static_cast<std::size_t>(y) * outputStride + x] ==
                        (x < static_cast<std::size_t>(width)
                             ? tightOutput[static_cast<std::size_t>(y * width) + x]
                             : sentinel),
                    "FXAA matches tight pixels and preserves output padding");
    require(paddedInput == originalInput, "FXAA does not mutate input or input padding");

    std::ranges::fill(paddedOutput, sentinel);
    // 출력 마지막 행의 padding이 span 밖이어도 활성 픽셀은 모두 기록된다.
    sr::fxaa::Apply(ConstPixelSurfaceView{paddedInput, width, height, inputStride},
                     PixelSurfaceView{std::span<unsigned int>{paddedOutput}.first((height - 1) * outputStride + width),
                                        width, height, outputStride});
    require(paddedOutput[(height - 1) * outputStride + width - 1] == tightOutput.back(),
            "FXAA minimal final output row");
    require(paddedOutput.back() == sentinel, "FXAA final padding untouched");

    std::ranges::fill(paddedOutput, sentinel);
    const auto invalidDoesNotWrite = [&](ConstPixelSurfaceView input, PixelSurfaceView output)
    {
        sr::fxaa::Apply(input, output);
        require(std::ranges::all_of(paddedOutput, [](auto value) { return value == sentinel; }),
                "FXAA invalid view does not write output");
    };
    const ConstPixelSurfaceView input{paddedInput, width, height, inputStride};
    const PixelSurfaceView output{paddedOutput, width, height, outputStride};
    invalidDoesNotWrite(input, {paddedOutput, width - 1, height, outputStride});
    invalidDoesNotWrite({paddedInput, width, height, width - 1}, output);
    invalidDoesNotWrite(input, {std::span<unsigned int>{paddedOutput}.first(5), width, height, outputStride});
    invalidDoesNotWrite({{}, 1, 3, std::numeric_limits<std::size_t>::max()}, {{}, 1, 3, 1});
    invalidDoesNotWrite({}, {});

    // 작은 이미지의 복사 경로 역시 행 padding을 건드리지 않는다.
    std::array<unsigned int, 6> tinyInput{0x00FF0000u, 0x000000FFu, sentinel,
                                           0x0000FF00u, 0x00123456u, sentinel};
    std::array<unsigned int, 8> tinyOutput;
    tinyOutput.fill(sentinel);
    sr::fxaa::Apply(ConstPixelSurfaceView{tinyInput, 2, 2, 3}, PixelSurfaceView{tinyOutput, 2, 2, 4});
    require(tinyOutput[0] == tinyInput[0] && tinyOutput[1] == tinyInput[1] && tinyOutput[4] == tinyInput[3] &&
                tinyOutput[5] == tinyInput[4],
            "tiny FXAA copies active pixels with different strides");
    require(tinyOutput[2] == sentinel && tinyOutput[3] == sentinel && tinyOutput[6] == sentinel &&
                tinyOutput[7] == sentinel,
            "tiny FXAA preserves padding");
}

void nativePixelAndLegacyFxaaColors()
{
    // Windows DIB의 BGRX 저장 규약: red/blue 숫자값과 실제 바이트 순서를 고정한다.
    static_assert(std::endian::native == std::endian::little);
    constexpr auto redBytes = std::bit_cast<std::array<unsigned char, 4>>(0x00FF0000u);
    constexpr auto blueBytes = std::bit_cast<std::array<unsigned char, 4>>(0x000000FFu);
    require(redBytes == std::array<unsigned char, 4>{0, 0, 255, 0}, "native BGRX red bytes");
    require(blueBytes == std::array<unsigned char, 4>{255, 0, 0, 0}, "native BGRX blue bytes");

    // 구조 분리 전 FXAA의 red/blue 휘도 가중치 불일치도 고정한다. intensity=32에서
    // 실제 red는 기존 0.114 가중치로 최소 threshold 아래, blue는 0.299로 위에 놓인다.
    constexpr int width = 7, height = 7;
    std::vector<unsigned int> redInput(width * height, 0u), blueInput(width * height, 0u);
    for (int y = 0; y < height; ++y)
        for (int x = 3; x < width; ++x)
        {
            redInput[static_cast<std::size_t>(y * width + x)] = 0x00200000u;
            blueInput[static_cast<std::size_t>(y * width + x)] = 0x00000020u;
        }
    std::vector<unsigned int> redOutput(redInput.size()), blueOutput(blueInput.size());
    sr::fxaa::Apply(redInput, redOutput, width, height);
    sr::fxaa::Apply(blueInput, blueOutput, width, height);
    require(redOutput == redInput, "legacy FXAA preserves subthreshold native red edge");
    require(blueOutput != blueInput, "legacy FXAA filters native blue edge above threshold");
    require(std::ranges::all_of(blueOutput, [](auto value) { return (value & 0xFFFFFF00u) == 0; }),
            "legacy FXAA does not exchange output color channels");
}

void lineRowBands()
{
    constexpr int width = 37, height = 35;
    constexpr std::size_t stride = 43;
    constexpr unsigned int padding = 0xDEADBEEFu;
    const RenderFrameContext frame{SRMath::mat4::identity(), {0.f, 0.f, 5.f}, {}, 3, 3};
    struct Image
    {
        std::vector<unsigned int> pixels;
        RenderTarget target;
        Image() : pixels(stride * height, 0xDEADBEEFu)
        {
            require(target.Attach({pixels, width, height, stride}).has_value(), "line-band target");
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x)
                {
                    const auto index = static_cast<std::size_t>(y * width + x);
                    pixels[static_cast<std::size_t>(y) * stride + x] = 0x00030405u;
                    target.Depth()[index] = .65f + .008f * static_cast<float>(x) +
                                            .004f * static_cast<float>(y) + ((x + y) % 5 == 0 ? .8f : 0.f);
                    target.PrimitiveIds()[index] = 16 + index;
                }
        }
    };

    // 선 경로의 끝점은 원래 래스터 순서 그대로 둔다. 음수 소수의 int 절삭,
    // 15/16/31/32 행 경계, 화면 밖 긴 선과 퇴화 선도 band 경계를 새 끝점으로 삼으면 안 된다.
    const auto wire = [](SRMath::vec2 a, SRMath::vec2 b, SRMath::vec2 c, float qa, float qb, float qc)
    {
        PreparedTriangle result{};
        result.rv0.screenPos = a;
        result.rv1.screenPos = b;
        result.rv2.screenPos = c;
        result.rv0.oneOverW = qa;
        result.rv1.oneOverW = qb;
        result.rv2.oneOverW = qc;
        result.minX = static_cast<int>(std::min({a.x, b.x, c.x}));
        result.maxX = static_cast<int>(std::max({a.x, b.x, c.x}));
        result.minY = static_cast<int>(std::min({a.y, b.y, c.y}));
        result.maxY = static_cast<int>(std::max({a.y, b.y, c.y}));
        return result;
    };
    const std::array triangles = {
        wire({-120.f, -35.f}, {160.f, 70.f}, {7.25f, 16.25f}, .45f, 1.6f, .9f),
        wire({1.25f, 1.75f}, {35.6f, 33.8f}, {3.4f, 31.2f}, .8f, 1.4f, .6f),
        wire({35.6f, 33.8f}, {1.25f, 1.75f}, {3.4f, 31.2f}, 1.4f, .8f, .6f),
        wire({0.f, 15.99f}, {36.f, 15.99f}, {18.f, 15.99f}, 1.f, 1.f, 1.f),
        wire({0.f, 16.01f}, {36.f, 16.01f}, {18.f, 16.01f}, 1.f, 1.f, 1.f),
        wire({0.f, 31.99f}, {36.f, 31.99f}, {18.f, 31.99f}, 1.f, 1.f, 1.f),
        wire({0.f, 32.01f}, {36.f, 32.01f}, {18.f, 32.01f}, 1.f, 1.f, 1.f),
        wire({9.8f, 16.9f}, {9.8f, 16.9f}, {9.8f, 16.9f}, .25f, .25f, .25f),
        wire({12.2f, 31.7f}, {12.2f, 31.7f}, {12.2f, 31.7f}, 2.f, 2.f, 2.f),
        wire({-10.f, -.4f}, {45.f, -.4f}, {17.f, -.4f}, 1.f, 1.f, 1.f),
        wire({-.4f, -20.f}, {-.4f, 60.f}, {-.4f, 17.f}, .4f, 1.8f, 1.f),
        wire({0.f, 34.9f}, {36.f, 34.9f}, {18.f, 34.9f}, 1.f, 1.f, 1.f),
        wire({0.f, -10.f}, {36.f, -10.f}, {18.f, -10.f}, .5f, .5f, .5f),
        wire({0.f, 40.f}, {36.f, 40.f}, {18.f, 40.f}, .5f, .5f, .5f)};

    DebugPrimitiveCommand overlay;
    overlay.worldTransform = SRMath::mat4::identity();
    overlay.depthPolicy = DebugDepthPolicy::Overlay;
    const auto vertex = [](float x, float y, SRMath::vec4 color)
    { return DebugVertex{{x / width * 2.f - 1.f, 1.f - y / height * 2.f, 0.f}, color}; };
    overlay.vertices = {vertex(-80.f, 4.25f, {1.f, 0.f, 0.f, 1.f}),
                        vertex(100.f, 30.75f, {1.f, 0.f, 0.f, 1.f}),
                        vertex(18.5f, -90.f, {0.f, 1.f, 0.f, 1.f}),
                        vertex(18.5f, 100.f, {0.f, 1.f, 0.f, 1.f})};
    auto depthLines = overlay;
    depthLines.depthPolicy = DebugDepthPolicy::DepthTested;

    const auto countersEqual = [](const RenderStats &a, const RenderStats &b)
    {
        return a.coveragePassed == b.coveragePassed && a.depthRejected == b.depthRejected &&
               a.shadedPixels == b.shadedPixels && a.hiZTests == b.hiZTests && a.hiZRejected == b.hiZRejected &&
               a.lineDepthRejected == b.lineDepthRejected && a.preparedTriangles == b.preparedTriangles &&
               a.binReferences == b.binReferences && a.finalFilledPixels == b.finalFilledPixels &&
               a.liveTriangleBytes == b.liveTriangleBytes && a.retainedTriangleBytes == b.retainedTriangleBytes &&
               a.retainedBinBytes == b.retainedBinBytes && a.prepareMs == b.prepareMs && a.binMs == b.binMs &&
               a.rasterMs == b.rasterMs && a.lineMs == b.lineMs && a.postMs == b.postMs;
    };
    for (const auto algorithm : {ELineAlgorithm::Bresenham, ELineAlgorithm::DDA})
        for (const bool depthTest : {false, true})
        {
            const Image untouched;
            Image reference;
            RenderStats referenceStats;
            const auto draw = [&](Rasterizer &rasterizer)
            {
                for (const auto &triangle : triangles)
                    rasterizer.DrawWireTriangle(triangle, algorithm, depthTest);
                rasterizer.DrawDebugPrimitive(depthLines, algorithm);
                rasterizer.DrawDebugPrimitive(overlay, algorithm);
            };
            // BeginLineRows를 생략한 기존 호출은 화면 전체를 쓰는 기준 경로다.
            Rasterizer full(reference.target, frame, &referenceStats);
            draw(full);
            require(std::ranges::equal(reference.target.Depth(), untouched.target.Depth()) &&
                        std::ranges::equal(reference.target.PrimitiveIds(), untouched.target.PrimitiveIds()),
                    "sequential lines preserve depth and primitive owners");
            require(referenceStats.lineDepthRejected > 0, "line-band reference exercises depth rejection");

            for (const int bandCount : {1, 2, 3, 7, height, height + 4})
            {
                Image parallel;
                std::vector<RenderStats> bandStats(static_cast<std::size_t>(bandCount));
                sr::jobs::ParallelFor(0, bandCount,
                                      [&](int first, int last)
                                      {
                                          for (int band = first; band != last; ++band)
                                          {
                                              Rasterizer rasterizer(parallel.target, frame, &bandStats[band]);
                                              rasterizer.BeginLineRows(height * band / bandCount,
                                                                       height * (band + 1) / bandCount);
                                              draw(rasterizer);
                                          }
                                      });
                RenderStats merged;
                for (const auto &stats : bandStats)
                    merged.MergeCounters(stats);
                require(parallel.pixels == reference.pixels, "parallel row bands preserve every pixel and padding");
                require(std::ranges::equal(parallel.target.Depth(), untouched.target.Depth()) &&
                            std::ranges::equal(parallel.target.PrimitiveIds(), untouched.target.PrimitiveIds()),
                        "parallel row bands do not modify depth or owners");
                require(countersEqual(merged, referenceStats), "row-band statistics match sequential lines");
            }

            Image clamped;
            RenderStats clampedStats;
            Rasterizer clampedRasterizer(clamped.target, frame, &clampedStats);
            clampedRasterizer.BeginLineRows(-100, height + 100);
            draw(clampedRasterizer);
            require(clamped.pixels == reference.pixels && countersEqual(clampedStats, referenceStats),
                    "oversized line-row interval clamps to screen");

            for (const auto bounds : {std::array{0, 0}, std::array{16, 16}, std::array{-9, -1},
                                      std::array{height + 1, height + 9}, std::array{22, 7}})
            {
                Image empty;
                RenderStats stats;
                Rasterizer rasterizer(empty.target, frame, &stats);
                rasterizer.BeginLineRows(bounds[0], bounds[1]);
                draw(rasterizer);
                require(empty.pixels == untouched.pixels && countersEqual(stats, RenderStats{}),
                        "empty out-of-screen or reversed row bands are no-ops");
            }

            // 한 행 band는 이웃 행에 전혀 쓰지 않고 경계 행의 원래 endpoint 규칙을 보존한다.
            for (const int row : {0, 15, 16, 31, 32, height - 1})
            {
                Image singleRow;
                Rasterizer rasterizer(singleRow.target, frame);
                rasterizer.BeginLineRows(row, row + 1);
                draw(rasterizer);
                for (int y = 0; y < height; ++y)
                    for (std::size_t x = 0; x < stride; ++x)
                    {
                        const auto index = static_cast<std::size_t>(y) * stride + x;
                        require(singleRow.pixels[index] == (y == row ? reference.pixels[index] : untouched.pixels[index]),
                                "single-row band preserves adjacent rows and padding");
                    }
            }
            for (int y = 0; y < height; ++y)
                for (std::size_t x = width; x < stride; ++x)
                    require(reference.pixels[static_cast<std::size_t>(y) * stride + x] == padding,
                            "line reference preserves row padding");
        }
}

void preparedDebugLineRowBands()
{
    constexpr int width = 48, height = 40;
    constexpr std::size_t stride = 53;
    const RenderFrameContext frame{SRMath::mat4::identity(), {0.f, 0.f, 5.f}, {}, 3, 3};
    struct Image
    {
        std::vector<unsigned int> pixels;
        RenderTarget target;
        Image() : pixels(stride * height, 0xDEADBEEFu)
        {
            require(target.Attach({pixels, width, height, stride}).has_value(), "prepared-debug target");
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x)
                {
                    const auto index = static_cast<std::size_t>(y * width + x);
                    pixels[static_cast<std::size_t>(y) * stride + x] = 0x00030405u;
                    target.Depth()[index] = (x + y) % 7 == 0 ? 1.7f : .4f + .003f * static_cast<float>(x);
                    target.PrimitiveIds()[index] = 1000 + index;
                }
            target.Depth()[static_cast<std::size_t>((height / 2) * width + width / 2)] = .4f;
        }
    };
    const auto vertex = [](float x, float y, float z, SRMath::vec4 color = {1.f, .5f, .25f, 1.f})
    { return DebugVertex{{x, y, z}, color}; };
    const auto command = []
    {
        DebugPrimitiveCommand result;
        result.worldTransform = SRMath::mat4::identity();
        result.depthPolicy = DebugDepthPolicy::DepthTested;
        return result;
    };
    std::vector<DebugPrimitiveCommand> commands;
    // 고정 seed로 모든 clip plane의 안/밖과 서로 다른 w를 반복한다.
    // 표준 분포 구현에 기대지 않아 플랫폼마다 같은 입력 순서를 유지한다.
    std::uint32_t randomState = 0x62B7A901u;
    const auto randomCoordinate = [&]
    {
        randomState = randomState * 1664525u + 1013904223u;
        return static_cast<float>((randomState >> 8) & 0xFFFFu) / 65535.f * 6.f - 3.f;
    };
    for (const bool varyingW : {false, true})
    {
        auto input = command();
        if (varyingW)
            input.worldTransform[2][3] = 1.f; // clip.w = z + 1; near plane은 z = -0.5다.
        for (int pair = 0; pair < 180; ++pair)
        {
            const SRMath::vec4 color{static_cast<float>(pair % 3 == 0),
                                     static_cast<float>(pair % 3 == 1),
                                     static_cast<float>(pair % 3 == 2), 1.f};
            for (int endpoint = 0; endpoint < 2; ++endpoint)
            {
                const float x = randomCoordinate(), y = randomCoordinate(), z = randomCoordinate();
                input.vertices.push_back(vertex(x, y, z, color));
            }
        }
        input.vertices.insert(input.vertices.end(), {
            vertex(-.4f, -.4f, -.5001f), vertex(.4f, .4f, -.4999f),
            vertex(-2.f, 0.f, -1.f), vertex(1.f, 0.f, 1.f),
            vertex(0.f, 0.f, 0.f), vertex(0.f, 0.f, 0.f),
            vertex(-1.f, 1.f, 0.f), vertex(1.f, -1.f, 0.f)});
        // 홀수 마지막 vertex는 기존 command 계약처럼 무시한다.
        input.vertices.push_back(vertex(0.f, 0.f, 0.f));
        commands.push_back(std::move(input));
    }

    auto invalid = command();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float infinity = std::numeric_limits<float>::infinity();
    invalid.vertices = {vertex(nan, 0.f, 0.f), vertex(0.f, 0.f, 0.f),
                        vertex(0.f, 0.f, 0.f), vertex(0.f, infinity, 0.f),
                        vertex(0.f, 0.f, -infinity), vertex(0.f, 0.f, 0.f),
                        vertex(4.f, 0.f, 0.f), vertex(5.f, 0.f, 0.f)};
    commands.push_back(invalid);
    std::vector<DebugPrimitiveCommand> invalidOnly{invalid};
    for (const float w : {0.f, -1.f, std::numeric_limits<float>::denorm_min()})
    {
        auto badW = command();
        badW.worldTransform = SRMath::mat4{0.f};
        badW.worldTransform[3][3] = w;
        badW.vertices = {vertex(0.f, 0.f, 0.f), vertex(0.f, 0.f, 0.f)};
        // 극소 양수 w의 q=Inf는 화면 좌표도 nonfinite이므로 준비되지 않아야 한다.
        require(!Rasterizer::PrepareDepthTestedLine(badW.vertices[0], badW.vertices[1],
                                                    badW.worldTransform, width, height),
                "prepared debug rejects zero negative and nonfinite reciprocal w");
        commands.push_back(badW);
        invalidOnly.push_back(std::move(badW));
    }
    const auto origin = vertex(0.f, 0.f, 0.f);
    for (const auto dimensions : {std::array{0, height}, std::array{width, 0},
                                   std::array{0, 0}, std::array{-1, height}, std::array{width, -1}})
        require(!Rasterizer::PrepareDepthTestedLine(origin, origin, frame.viewProjection,
                                                    dimensions[0], dimensions[1]),
                "prepared debug rejects empty or negative viewport");

    auto crossing = command();
    crossing.vertices = {vertex(-1.f, 0.f, 0.f, {1.f, 0.f, 0.f, 1.f}), vertex(1.f, 0.f, 0.f),
                         vertex(0.f, -1.f, 0.f, {0.f, 1.f, 0.f, 1.f}), vertex(0.f, 1.f, 0.f),
                         vertex(-1.f, 1.f, 0.f, {0.f, 0.f, 1.f, 1.f}), vertex(1.f, -1.f, 0.f)};
    commands.push_back(std::move(crossing));
    const auto countersEqual = [](const RenderStats &a, const RenderStats &b)
    {
        return a.coveragePassed == b.coveragePassed && a.depthRejected == b.depthRejected &&
               a.shadedPixels == b.shadedPixels && a.hiZTests == b.hiZTests && a.hiZRejected == b.hiZRejected &&
               a.lineDepthRejected == b.lineDepthRejected && a.preparedTriangles == b.preparedTriangles &&
               a.binReferences == b.binReferences && a.finalFilledPixels == b.finalFilledPixels &&
               a.liveTriangleBytes == b.liveTriangleBytes && a.retainedTriangleBytes == b.retainedTriangleBytes &&
               a.retainedBinBytes == b.retainedBinBytes && a.prepareMs == b.prepareMs && a.binMs == b.binMs &&
               a.rasterMs == b.rasterMs && a.lineMs == b.lineMs && a.postMs == b.postMs;
    };
    const std::array scenes{commands, invalidOnly, std::vector<DebugPrimitiveCommand>{}};
    for (std::size_t sceneIndex = 0; sceneIndex < scenes.size(); ++sceneIndex)
    {
        const auto &scene = scenes[sceneIndex];
        std::vector<Rasterizer::PreparedDebugLine> prepared;
        std::size_t candidateCount = 0;
        for (const auto &input : scene)
        {
            const SRMath::mat4 mvp = frame.viewProjection * input.worldTransform;
            candidateCount += input.vertices.size() / 2;
            for (std::size_t i = 0; i + 1 < input.vertices.size(); i += 2)
                if (auto line = Rasterizer::PrepareDepthTestedLine(input.vertices[i], input.vertices[i + 1],
                                                                  mvp, width, height))
                    prepared.push_back(*line);
        }
        require(prepared.size() <= candidateCount, "prepared debug has at most one result per line pair");
        require(sceneIndex == 0 ? !prepared.empty() : prepared.empty(),
                "prepared debug compacts rejected lines and handles empty arrays");
        for (const auto algorithm : {ELineAlgorithm::Bresenham, ELineAlgorithm::DDA})
            for (const bool instrumented : {false, true})
            {
                const Image untouched;
                Image reference;
                RenderStats referenceStats;
                Rasterizer original(reference.target, frame, instrumented ? &referenceStats : nullptr);
                // 이 경로는 준비 API를 호출하지 않는 독립 순차 기준으로 남긴다.
                for (const auto &input : scene)
                    original.DrawDebugPrimitive(input, algorithm);
                if (sceneIndex == 0)
                {
                    require(reference.pixels[static_cast<std::size_t>(height / 2) * stride + width / 2] ==
                                0x000000FFu,
                            "colored crossing debug lines retain submission order");
                    require(!instrumented || referenceStats.lineDepthRejected > 0,
                            "prepared debug fixture exercises depth rejection");
                }
                require(std::ranges::equal(reference.target.Depth(), untouched.target.Depth()) &&
                            std::ranges::equal(reference.target.PrimitiveIds(), untouched.target.PrimitiveIds()),
                        "original debug lines never modify depth or owners");
                for (const int bandCount : {1, 2, 3, 7, 32, height + 3})
                {
                    Image actual;
                    std::vector<RenderStats> bandStats(static_cast<std::size_t>(bandCount));
                    // 준비 배열은 join까지 읽기 전용이며 각 worker는 원래 제출 순서를 보존한다.
                    sr::jobs::ParallelFor(0, bandCount,
                                          [&](int first, int last)
                                          {
                                              for (int band = first; band != last; ++band)
                                              {
                                                  Rasterizer rasterizer(actual.target, frame,
                                                                        instrumented ? &bandStats[band]
                                                                                     : nullptr);
                                                  rasterizer.BeginLineRows(height * band / bandCount,
                                                                           height * (band + 1) / bandCount);
                                                  for (const auto &line : prepared)
                                                      rasterizer.DrawPreparedDebugLine(line, algorithm);
                                              }
                                          });
                    RenderStats merged;
                    for (const auto &stats : bandStats)
                        merged.MergeCounters(stats);
                    require(actual.pixels == reference.pixels, "prepared debug bands match pixels and padding");
                    require(std::ranges::equal(actual.target.Depth(), untouched.target.Depth()) &&
                                std::ranges::equal(actual.target.PrimitiveIds(), untouched.target.PrimitiveIds()),
                            "prepared debug bands preserve exact depth and owners");
                    require(countersEqual(merged, referenceStats), "prepared debug band stats match original path");
                }
                if (prepared.empty())
                {
                    Image fallback;
                    RenderStats fallbackStats;
                    Rasterizer rasterizer(fallback.target, frame, instrumented ? &fallbackStats : nullptr);
                    for (const auto &input : scene)
                        rasterizer.DrawDebugPrimitive(input, algorithm);
                    require(fallback.pixels == untouched.pixels && countersEqual(fallbackStats, RenderStats{}),
                            "empty prepared array sequential fallback remains a no-op");
                }
                for (int y = 0; y < height; ++y)
                    for (std::size_t x = width; x < stride; ++x)
                        require(reference.pixels[static_cast<std::size_t>(y) * stride + x] == 0xDEADBEEFu,
                                "prepared debug reference preserves stride padding");
            }
    }
}

#if SR_TEST_WIN32_APP
class HiddenWindow
{
  public:
    HiddenWindow()
        : handle(CreateWindowExW(0, L"STATIC", L"Render target tests", WS_POPUP, 0, 0, 32, 24, nullptr,
                                 nullptr, GetModuleHandleW(nullptr), nullptr))
    {
        require(handle != nullptr, "hidden presenter test window creation");
    }
    ~HiddenWindow() { DestroyWindow(handle); }
    HiddenWindow(const HiddenWindow &) = delete;
    HiddenWindow &operator=(const HiddenWindow &) = delete;
    HWND handle;
};

void presenterLifecycle()
{
    HiddenWindow window;
    sr::platform::GdiPresenter presenter;
    RenderTarget target;
    require(presenter.Surface().pixels.empty(), "new presenter is empty");
    presenter.Present(nullptr);

    for (int iteration = 0; iteration < 8; ++iteration)
    {
        // 대상의 뷰를 무효화한 뒤 DIB를 교체한다. 해제된 예전 뷰는 다시 읽지 않는다.
        target.Detach();
        const int width = 17 + iteration, height = 9 + iteration;
        require(presenter.Resize(window.handle, width, height).has_value(), "presenter creates DIB");
        const auto surface = presenter.Surface();
        require(surface.width == width && surface.height == height &&
                    surface.stridePixels == static_cast<std::size_t>(width),
                "DIB dimensions and tight BGRX stride");
        require(target.Attach(surface).has_value(), "target borrows presenter DIB");
        target.Clear();
        target.Surface().pixels.front() = 0x00FF0000u;
        target.Surface().pixels.back() = 0x000000FFu;
        const auto &readOnlyPresenter = presenter;
        require(readOnlyPresenter.Surface().pixels.front() == 0x00FF0000u &&
                    readOnlyPresenter.Surface().pixels.back() == 0x000000FFu,
                "target writes directly into presenter without color copy");
    }

    target.Detach();
    auto result = presenter.Resize(window.handle, -1, 4);
    require(!result && result.error() == sr::platform::PresentationError::InvalidDimensions,
            "presenter invalid dimensions error");
    require(presenter.Surface().pixels.empty(), "presenter fails closed");
    require(presenter.Resize(window.handle, 0, 8).has_value(), "presenter zero width suspends output");
    require(presenter.Surface().pixels.empty(), "zero width presenter empty");
    require(presenter.Resize(window.handle, 8, 0).has_value(), "presenter zero height suspends output");
    require(presenter.Surface().pixels.empty(), "zero height presenter empty");
    presenter.Reset();
    presenter.Reset();
    presenter.Present(nullptr);
}
#endif
} // namespace

void sr::test::RegisterRenderTargetTests(TestRegistry &registry)
{
    registry.Add("target.attachmentValidation", attachmentValidation);
    registry.Add("target.clearAndStride", clearAndStride);
    registry.Add("target.fxaaStrides", fxaaStrides);
    registry.Add("target.nativePixelAndLegacyFxaaColors", nativePixelAndLegacyFxaaColors);
    registry.Add("target.lineRowBands", lineRowBands);
    registry.Add("target.preparedDebugLineRowBands", preparedDebugLineRowBands);
#if SR_TEST_WIN32_APP
    registry.Add("platform.presenterLifecycle", presenterLifecycle);
#endif
}

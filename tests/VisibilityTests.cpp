#include "Renderer/Renderer.h"
#include "Renderer/RenderQueue.h"
#include "Scene/Camera.h"
#include "Graphics/Light.h"
#include "Graphics/AssetParsing.h"
#include "Utils/Jobs.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <numbers>
#include <random>
#include <limits>
#include <stdexcept>
#include "TestHarness.h"

// Rasterizer의 별도 friend 이름을 이 번역 단위에서만 정의한다.
// RendererCoreTests.cpp의 RendererTestAccess 정의와 중복되지 않는다.
struct RasterizerTestAccess
{
    static void ScreenLine(Rasterizer &rasterizer, SRMath::vec2 a, SRMath::vec2 b, float qa, float qb,
                           std::uint32_t color, ELineAlgorithm algorithm, bool depth, float slopeX,
                           float slopeY)
    {
        rasterizer.DrawScreenLine(a, b, qa, qb, color, algorithm, depth, slopeX, slopeY);
    }
};

namespace
{
void require(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

std::vector<PreparedDirectionalLight> prepareLights(std::span<const DirectionalLight> lights)
{
    std::vector<PreparedDirectionalLight> result;
    result.reserve(lights.size());
    for (const auto &light : lights)
        result.push_back(PreparedDirectionalLight::From(light));
    return result;
}

struct MemoryTarget
{
    std::vector<unsigned int> storage;
    RenderTarget target;
    MemoryTarget(int width, int height) : storage(static_cast<std::size_t>(width) * height)
    {
        require(target.Attach({storage, width, height, static_cast<std::size_t>(width)}).has_value(),
                "memory visibility target");
        target.Clear();
    }
};
struct Snapshot
{
    std::vector<unsigned int> colors;
    std::vector<float> depths;
    std::vector<std::uint64_t> owners;
    bool operator==(const Snapshot &) const = default;
};
Snapshot snapshot(const RenderTarget &target)
{
    const auto surface = target.Surface();
    Snapshot result;
    for (int y = 0; y < surface.height; ++y)
        result.colors.insert(result.colors.end(), surface.pixels.begin() + y * surface.stridePixels,
                             surface.pixels.begin() + y * surface.stridePixels + surface.width);
    result.depths.assign(target.Depth().begin(), target.Depth().end());
    result.owners.assign(target.PrimitiveIds().begin(), target.PrimitiveIds().end());
    return result;
}
ShadedVertex clipVertex(float x, float y, float inverseW)
{
    ShadedVertex vertex{};
    const float w = 1.f / inverseW;
    vertex.posClip = {x * w, y * w, 0.f, w};
    vertex.normalWorld = {0.f, 0.f, 1.f};
    return vertex;
}
std::vector<PreparedTriangle> rectangle(float left, float top, float right, float bottom, float q,
                                        std::uint64_t id, int width, int height,
                                        const MeshRenderCommand &command)
{
    // 입력은 픽셀 경계다. 픽셀 중심으로 coverage를 판정하므로 정수 경계 사이의 구멍도 표현된다.
    const auto vertex = [&](float x, float y)
    { return clipVertex(x / width * 2.f - 1.f, 1.f - y / height * 2.f, q); };
    const auto a = vertex(left, bottom), b = vertex(left, top), c = vertex(right, top),
               d = vertex(right, bottom);
    const auto first = Rasterizer::Prepare(a, b, c, width, height, id, &command);
    const auto second = Rasterizer::Prepare(a, c, d, width, height, id + 1, &command);
    require(first && second, "rectangle preparation");
    return {*first, *second};
}
void drawPrepared(RenderTarget &target, const RenderFrameContext &frame,
                  std::span<const PreparedTriangle> triangles, bool hiZ, RenderStats *stats = nullptr,
                  RasterPass pass = RasterPass::Shaded)
{
    for (int y = 0; y < target.Height(); y += tileSize)
        for (int x = 0; x < target.Width(); x += tileSize)
        {
            Rasterizer rasterizer(target, frame, stats);
            rasterizer.BeginTile(
                {x, y, std::min(x + tileSize, target.Width()), std::min(y + tileSize, target.Height())}, hiZ);
            for (const auto &triangle : triangles)
                rasterizer.DrawTriangle(triangle, pass);
            if (pass == RasterPass::DepthOwner)
                rasterizer.ShadeOwners();
        }
}
void depthAndHiZTests()
{
    MemoryTarget image(17, 19);
    Material red, blue;
    red.ambient = {1.f, 0.f, 0.f};
    blue.ambient = {0.f, 0.f, 1.f};
    MeshRenderCommand redCommand{}, blueCommand{};
    redCommand.material = &red;
    blueCommand.material = &blue;
    const RenderFrameContext frame{SRMath::mat4::identity(), {0.f, 0.f, 5.f}, {}, 2, 2};
    auto near = rectangle(0, 0, 17, 19, 1.f, 16, 17, 19, redCommand);
    auto far = rectangle(0, 0, 17, 19, 0.5f, 32, 17, 19, blueCommand);
    std::vector<PreparedTriangle> geometry = near;
    geometry.insert(geometry.end(), far.begin(), far.end());
    drawPrepared(image.target, frame, geometry, false);
    const auto reference = snapshot(image.target);
    image.target.Clear();
    RenderStats stats;
    drawPrepared(image.target, frame, geometry, true, &stats);
    require(snapshot(image.target) == reference, "Hi-Z partial-tile equivalence");
    require(stats.hiZRejected > 0, "Hi-Z actually rejects hidden geometry");

    // 같은 깊이의 작은 ID는 삽입 순서가 뒤여도 이긴다. Hi-Z도 동률을 버리면 안 된다.
    auto tie = rectangle(0, 0, 17, 19, 1.f, 0, 17, 19, blueCommand);
    geometry = near;
    geometry.insert(geometry.end(), tie.begin(), tie.end());
    image.target.Clear();
    drawPrepared(image.target, frame, geometry, true);
    const auto tieReference = snapshot(image.target);
    std::ranges::reverse(geometry);
    image.target.Clear();
    drawPrepared(image.target, frame, geometry, false);
    require(snapshot(image.target) == tieReference, "exact-depth primitive tie order");
    require(image.storage[9 * 17 + 8] == 0x0000FFu, "smaller primitive owns coincident surface");

    // 전면 4개 사각형 사이에 실제 한 픽셀 구멍을 만든다. 구멍 뒤의 면은 살아야 한다.
    geometry.clear();
    std::uint64_t next = 0;
    const auto append = [&](float l, float t, float r, float b)
    {
        auto part = rectangle(l, t, r, b, 1.f, next, 17, 19, redCommand);
        next += 2;
        geometry.insert(geometry.end(), part.begin(), part.end());
    };
    append(0, 0, 17, 9);
    append(0, 10, 17, 19);
    append(0, 9, 8, 10);
    append(9, 9, 17, 10);
    geometry.insert(geometry.end(), far.begin(), far.end());
    image.target.Clear();
    drawPrepared(image.target, frame, geometry, false);
    const auto holeReference = snapshot(image.target);
    require(image.storage[9 * 17 + 8] == 0x0000FFu, "one-pixel hole remains visible");
    image.target.Clear();
    drawPrepared(image.target, frame, geometry, true);
    require(snapshot(image.target) == holeReference, "Hi-Z cannot fill a coverage hole");
}

void variableDepthHiZTests()
{
    // 전역 vertex 최대 q로 기각하는 경로를 다양한 실제 보간값과 대조한다.
    // 고정 seed이므로 빌드 구성/실행 횟수와 무관하게 실패 장면을 재현할 수 있다.
    constexpr int width = 33, height = 35;
    MemoryTarget image(width, height);
    Material red, blue;
    red.ambient = {1.f, .125f, 0.f};
    blue.ambient = {0.f, .25f, 1.f};
    MeshRenderCommand redCommand{}, blueCommand{}, bareCommand{};
    redCommand.material = &red;
    blueCommand.material = &blue;
    // 재질 없는 삼각형은 깊이만 이기고 색은 쓰지 않는다. 그 아래 색이 남아야 한다.
    const RenderFrameContext frame{SRMath::mat4::identity(), {0.f, 0.f, 5.f}, {}, 3, 3};
    std::mt19937 random(0x48A17u);
    std::uniform_real_distribution<float> position(-1.f, 1.f), factor(.125f, 1.875f);
    std::uniform_int_distribution<int> exponent(-110, 110);
    std::uint64_t rejected = 0;

    const auto checkOrders = [&](std::vector<PreparedTriangle> geometry)
    {
        for (int order = 0; order < 3; ++order)
        {
            if (order == 1)
                std::ranges::reverse(geometry);
            else if (order == 2)
                std::ranges::sort(geometry,
                                  [](const auto &a, const auto &b)
                                  {
                                      return a.maximumInverseW != b.maximumInverseW
                                                 ? a.maximumInverseW > b.maximumInverseW
                                                 : a.primitiveId < b.primitiveId;
                                  });
            for (const bool prepass : {false, true})
            {
                const auto draw = [&](bool hiZ, RenderStats *stats)
                {
                    drawPrepared(image.target, frame, geometry, hiZ, stats,
                                 prepass ? RasterPass::DepthOnly : RasterPass::Shaded);
                    if (prepass)
                        drawPrepared(image.target, frame, geometry, hiZ, stats, RasterPass::ShadeVisible);
                };
                image.target.Clear();
                draw(false, nullptr);
                const auto reference = snapshot(image.target);
                require(std::ranges::any_of(reference.owners,
                                            [](auto id) { return id != RenderTarget::emptyOwner; }),
                        "variable-q scene has coverage");
                image.target.Clear();
                RenderStats stats;
                draw(true, &stats);
                require(snapshot(image.target) == reference,
                        "variable-q Hi-Z preserves exact color/depth/owner across orders and passes");
                rejected += stats.hiZRejected;
            }
            // 깊이 먼저, 주인 삼각형으로 픽셀당 한 번 셰이딩하는 패스는 Shaded 패스와 색/깊이/ID가 같아야
            // 한다.
            image.target.Clear();
            drawPrepared(image.target, frame, geometry, false, nullptr, RasterPass::Shaded);
            const auto shaded = snapshot(image.target);
            for (const bool hiZ : {false, true})
            {
                image.target.Clear();
                drawPrepared(image.target, frame, geometry, hiZ, nullptr, RasterPass::DepthOwner);
                require(snapshot(image.target) == shaded,
                        "owner pass shades each pixel once with the same result as the shaded pass");
            }
        }
    };

    for (int scene = 0; scene < 64; ++scene)
    {
        const float base = std::ldexp(.75f, exponent(random));
        auto geometry = rectangle(0, 0, width, height, base, 1000, width, height, redCommand);
        // 완전히 가린 후보도 매번 포함해 단순한 Hi-Z 비활성화로 테스트가 통과하지 않게 한다.
        auto hidden = rectangle(0, 0, width, height, base * .0625f, 2000, width, height, blueCommand);
        geometry.insert(geometry.end(), hidden.begin(), hidden.end());
        for (std::uint64_t triangle = 0; triangle < 12; ++triangle)
        {
            std::array<float, 3> q{};
            for (auto &value : q)
                value = base * factor(random);
            if (triangle < 3)
            {
                // 1 ULP 차이와 같은 정점 깊이가 섞이면 최대 q와 픽셀 q의
                // 반올림 관계가 중요하다. 작은 ID가 이기는 경우도 포함한다.
                q = {std::nextafter(base, 0.f), base,
                     std::nextafter(base, std::numeric_limits<float>::infinity())};
                std::rotate(q.begin(), q.begin() + static_cast<std::ptrdiff_t>(triangle), q.end());
            }
            std::array<SRMath::vec2, 3> points{};
            for (auto &point : points)
                point = {position(random), position(random)};
            const float area = (points[1].x - points[0].x) * (points[2].y - points[0].y) -
                               (points[1].y - points[0].y) * (points[2].x - points[0].x);
            if (area > 0.f)
                std::swap(points[1], points[2]);
            const auto prepared = Rasterizer::Prepare(
                clipVertex(points[0].x, points[0].y, q[0]), clipVertex(points[1].x, points[1].y, q[1]),
                clipVertex(points[2].x, points[2].y, q[2]), width, height, triangle,
                triangle % 4 == 3 ? &bareCommand : &blueCommand);
            require(prepared.has_value(), "finite positive random-q triangle prepares");
            geometry.push_back(*prepared);
        }
        checkOrders(std::move(geometry));
    }
    require(rejected > 0, "variable-q suite exercises actual Hi-Z rejection");

    const float minimum = std::numeric_limits<float>::min();
    const float maximum = std::numeric_limits<float>::max();
    const std::array<std::array<float, 3>, 5> extremes{{{minimum * .5f, minimum * .75f, minimum},
                                                        {minimum, minimum * 2.f, minimum * 4.f},
                                                        {maximum * .25f, maximum * .5f, maximum * .75f},
                                                        {1e-30f, 1.f, 1e30f},
                                                        {1e-38f, 2e-38f, 3e-38f}}};
    std::size_t acceptedExtremeCases = 0;
    for (const auto &q : extremes)
    {
        auto geometry = rectangle(0, 0, width, height, 1.f, 1000, width, height, redCommand);
        const auto prepared = Rasterizer::Prepare(clipVertex(-.9f, -.9f, q[0]), clipVertex(-.9f, .9f, q[1]),
                                                  clipVertex(.9f, .9f, q[2]), width, height, 0, &blueCommand);
        // FTZ/DAZ 환경에서는 일부 subnormal 입력이 Prepare에서 기각될 수 있다.
        // 수락된 극단값만 렌더링하되 정상적인 유한 극단값의 수락은 별도로 요구한다.
        if (!prepared)
            continue;
        ++acceptedExtremeCases;
        geometry.push_back(*prepared);
        checkOrders(std::move(geometry));
        const bool hasSubnormal = std::fpclassify(prepared->rv0.oneOverW) == FP_SUBNORMAL ||
                                  std::fpclassify(prepared->rv1.oneOverW) == FP_SUBNORMAL ||
                                  std::fpclassify(prepared->rv2.oneOverW) == FP_SUBNORMAL;
        if (hasSubnormal)
        {
            image.target.Clear();
            const auto cover = rectangle(0, 0, width, height, 1.f, 1000, width, height, redCommand);
            drawPrepared(image.target, frame, cover, false);
            RenderStats stats;
            drawPrepared(image.target, frame, std::span<const PreparedTriangle>{&*prepared, 1}, true, &stats);
            require(stats.hiZTests > 0 && stats.hiZRejected == 0,
                    "subnormal vertex q disables Hi-Z rejection");
        }
    }
    require(acceptedExtremeCases >= 2, "finite extreme-q cases exercise rasterization");
}
void hiddenLineTests()
{
    MemoryTarget image(64, 64);
    Material material;
    MeshRenderCommand command{};
    command.material = &material;
    auto cover = rectangle(0, 0, 64, 64, 1.f, 0, 64, 64, command);
    // 이 테스트 전용 투영: clip.w = world.z, clip.z = 0.
    SRMath::mat4 projection(0.f);
    projection[0][0] = projection[1][1] = projection[2][3] = 1.f;
    const RenderFrameContext frame{projection, {0.f, 0.f, 0.f}, {}, 4, 4};
    for (auto algorithm : {ELineAlgorithm::Bresenham, ELineAlgorithm::DDA})
    {
        image.target.Clear();
        drawPrepared(image.target, frame, cover, false, nullptr, RasterPass::DepthOnly);
        const auto depths = snapshot(image.target);
        DebugPrimitiveCommand line;
        line.worldTransform = SRMath::mat4::identity();
        line.type = DebugPrimitiveType::Line;
        line.depthPolicy = DebugDepthPolicy::DepthTested;
        line.vertices = {{{-.3f, 0.f, 2.f}, {0.f, 1.f, 0.f, 1.f}}, {{.3f, 0.f, 2.f}, {0.f, 1.f, 0.f, 1.f}}};
        Rasterizer rasterizer(image.target, frame);
        rasterizer.DrawDebugPrimitive(line, algorithm);
        require(std::ranges::all_of(image.storage, [](auto pixel) { return pixel == 0; }),
                "hidden normal line rejected");
        line.depthPolicy = DebugDepthPolicy::Overlay;
        rasterizer.DrawDebugPrimitive(line, algorithm);
        require(std::ranges::any_of(image.storage, [](auto pixel) { return pixel == 0x00FF00; }),
                "overlay ignores depth");
        require(snapshot(image.target).depths == depths.depths &&
                    snapshot(image.target).owners == depths.owners,
                "lines never write depth or owner");
        std::ranges::fill(image.storage, 0u);
        auto hiddenWire = rectangle(8, 8, 56, 56, .5f, 10, 64, 64, command);
        for (const auto &triangle : hiddenWire)
            rasterizer.DrawWireTriangle(triangle, algorithm);
        require(std::ranges::all_of(image.storage, [](auto pixel) { return pixel == 0; }),
                "hidden wire rejected");
        for (const auto &triangle : cover)
            rasterizer.DrawWireTriangle(triangle, algorithm);
        require(std::ranges::any_of(image.storage, [](auto pixel) { return pixel != 0; }),
                "surface wire remains visible");
        // 계측 ON은 동일색 생략을 하지 않는 기준 경로다. 이미 같은 색인 픽셀,
        // 가린 선, 겹친 선 모두 색/깊이/owner가 빠른 경로와 정확히 같아야 한다.
        Snapshot reference;
        for (const bool instrumented : {true, false})
        {
            image.target.Clear();
            drawPrepared(image.target, frame, cover, false, nullptr, RasterPass::DepthOnly);
            for (std::size_t i = 0; i < image.storage.size(); ++i)
                image.storage[i] = i % 3 == 0 ? 0xFFFFFFu : (i % 3 == 1 ? 0x00FF00u : 0u);
            RenderStats stats;
            Rasterizer tested(image.target, frame, instrumented ? &stats : nullptr);
            line.depthPolicy = DebugDepthPolicy::DepthTested;
            for (int repetition = 0; repetition < 4; ++repetition)
            {
                tested.DrawDebugPrimitive(line, algorithm);
                for (const auto &triangle : hiddenWire)
                    tested.DrawWireTriangle(triangle, algorithm);
                for (const auto &triangle : cover)
                    tested.DrawWireTriangle(triangle, algorithm);
            }
            if (instrumented)
            {
                require(stats.lineDepthRejected > 0, "instrumented lines retain rejection counters");
                reference = snapshot(image.target);
            }
            else
                require(snapshot(image.target) == reference, "same-color line fastpath is exact");
        }
    }
}

struct SyntheticScene
{
    std::vector<Mesh> meshes;
    RenderQueue queue;
    Camera camera{{0.f, 0.f, 5.f}};
    std::array<DirectionalLight, 1> lights{DirectionalLight{{0.f, -1.f, -1.f}, {1.f, 1.f, 1.f}}};

    SyntheticScene(int width, int height, bool occluded, bool wire = false, bool normals = false)
    {
        camera.SetCameraYaw(std::numbers::pi_v<float>);
        std::array<bool, 256> keys{};
        camera.Update(0.f, keys, static_cast<float>(width) / height);
        meshes.reserve(97);
        const auto addQuad = [&](float x, float y, float z, float radius)
        {
            Mesh mesh;
            mesh.vertices = {{{x - radius, y - radius, z}, {0.f, 0.f}, {0.f, 0.f, 1.f}},
                             {{x + radius, y - radius, z}, {1.f, 0.f}, {0.f, 0.f, 1.f}},
                             {{x + radius, y + radius, z}, {1.f, 1.f}, {0.f, 0.f, 1.f}},
                             {{x - radius, y + radius, z}, {0.f, 1.f}, {0.f, 0.f, 1.f}}};
            // 카메라는 -Z를 바라보며 이 렌더러의 앞면 규약은 NDC의 CW다.
            // 좌하→우하→우상 순서는 CCW라 전부 backface 컬링된다.
            mesh.indices = {0, 2, 1, 0, 3, 2};
            mesh.material.ambient = {.05f, .05f, .05f};
            mesh.material.diffuse = {.2f + static_cast<float>(meshes.size() % 4) * .1f, .4f, .6f};
            meshes.push_back(std::move(mesh));
        };
        if (occluded)
            addQuad(0.f, 0.f, 1.f, 3.f);
        for (int i = 0; i < 96; ++i)
            if (occluded)
                addQuad(0.f, 0.f, -static_cast<float>(i) * .03f, 2.f);
            else
                addQuad((i % 12 - 5.5f) * .45f, (i / 12 - 3.5f) * .45f, 0.f, .18f);
        for (const auto &mesh : meshes)
        {
            queue.Submit(MeshRenderCommand{&mesh, mesh.indices, SRMath::mat4::identity(), &mesh.material,
                                           wire ? ERasterizeMode::Wireframe : ERasterizeMode::Fill});
            if (normals)
            {
                DebugPrimitiveCommand lines;
                lines.worldTransform = SRMath::mat4::identity();
                lines.type = DebugPrimitiveType::Line;
                lines.depthPolicy = DebugDepthPolicy::DepthTested;
                for (const auto &vertex : mesh.vertices)
                {
                    lines.vertices.push_back({vertex.position, {1.f, 1.f, 0.f, 1.f}});
                    lines.vertices.push_back({vertex.position + vertex.normal * .1f, {1.f, 1.f, 0.f, 1.f}});
                }
                queue.Submit(std::move(lines));
            }
        }
    }
};
void determinismTests()
{
    MemoryTarget image(65, 49);
    SyntheticScene scene(65, 49, true);
    Renderer renderer;
    Snapshot expected;
    for (bool single : {true, false})
    {
        sr::jobs::ConcurrencyLimit limit(single ? 1 : sr::jobs::MaxConcurrency());
        for (int iteration = 0; iteration < 100; ++iteration)
        {
            renderer.SetOptions({(iteration & 1) != 0, (iteration & 2) != 0, iteration == 99});
            image.target.Clear();
            renderer.RenderScene(image.target, scene.queue, scene.camera.GetViewParams(), scene.lights);
            const auto result = snapshot(image.target);
            if (expected.colors.empty())
                expected = result;
            require(result == expected, "100-run worker/sort/Hi-Z determinism");
        }
    }
    require(std::ranges::any_of(expected.colors, [](auto pixel) { return pixel != 0; }),
            "nonempty deterministic scene");
    // mixed fill/wire: 가까운 wire 면은 뒤의 채움 색을 가려야 한다.
    MeshRenderCommand wire = scene.queue.GetRenderCommands().front();
    wire.rasterizeMode = ERasterizeMode::Wireframe;
    RenderQueue mixed;
    mixed.Submit(wire);
    for (const auto &cmd : scene.queue.GetRenderCommands().subspan(1))
        mixed.Submit(cmd);
    renderer.SetOptions({false, false, false, true});
    image.target.Clear();
    renderer.RenderScene(image.target, mixed, scene.camera.GetViewParams(), scene.lights);
    require(std::ranges::all_of(image.storage, [](auto pixel) { return pixel == 0 || pixel == 0xFFFFFF; }),
            "mixed wire occludes filled colors");
    renderer.SetOptions({});
    image.target.Clear();
    renderer.RenderScene(image.target, mixed, scene.camera.GetViewParams(), scene.lights);
    require(std::ranges::any_of(image.storage, [](auto pixel) { return pixel != 0 && pixel != 0xFFFFFF; }),
            "default mixed X-ray wire does not occlude filled colors");
}

void tileMergeSubmissionTests()
{
    // Rasterizer 단독 동률 검사와 별도로 실제 command 분배/worker bin 병합을 거친다.
    // 작업 순서에 따라 달라지는 shaded/depthRejected/Hi-Z 횟수는 계약에 포함하지 않는다.
    const auto quad = [](float x, float z, float radius)
    {
        Mesh mesh;
        mesh.vertices = {{{x - radius, -radius, z}, {}, {0.f, 0.f, 1.f}},
                         {{x + radius, -radius, z}, {}, {0.f, 0.f, 1.f}},
                         {{x + radius, radius, z}, {}, {0.f, 0.f, 1.f}},
                         {{x - radius, radius, z}, {}, {0.f, 0.f, 1.f}}};
        mesh.indices = {0, 2, 1, 0, 3, 2};
        return mesh;
    };
    std::array<Mesh, 4> meshes{quad(0.f, 0.f, 1.5f), quad(0.f, -1.f, 1.5f), quad(.8f, 1.f, .25f),
                               quad(-.8f, 1.5f, .3f)};
    std::array<Material, 3> materials;
    materials[0].ambient = {1.f, 0.f, 0.f};
    materials[1].ambient = {0.f, 0.f, 1.f};
    materials[2].ambient = {0.f, 1.f, 0.f};
    const auto makeQueue = [&](bool reverseTie, bool mixed)
    {
        RenderQueue queue;
        for (int i = 0; i < 96; ++i)
        {
            const auto &mesh = meshes[i < 2 ? 0 : 1];
            queue.Submit(MeshRenderCommand{&mesh, mesh.indices, SRMath::mat4::identity(),
                                           &materials[(i + static_cast<int>(reverseTie)) % 2]});
        }
        queue.Submit(
            MeshRenderCommand{&meshes[2], meshes[2].indices, SRMath::mat4::identity(), &materials[2]});
        if (mixed)
            queue.Submit(MeshRenderCommand{&meshes[3], meshes[3].indices, SRMath::mat4::identity(),
                                           &materials[1], ERasterizeMode::Wireframe});
        return queue;
    };
    Renderer reused, independent;
    const auto defaultWorkers = sr::jobs::MaxConcurrency();
    // 1 tile, 동일한 8 tiles지만 서로 다른 행 stride, 큰→작은→재확장을 포함한다.
    for (const auto size : {std::array{16, 16}, std::array{32, 64}, std::array{64, 32}, std::array{65, 49},
                            std::array{33, 67}, std::array{65, 49}})
    {
        const int width = size[0], height = size[1];
        Camera camera({0.f, 0.f, 5.f});
        camera.SetCameraYaw(std::numbers::pi_v<float>);
        std::array<bool, 256> keys{};
        camera.Update(0.f, keys, static_cast<float>(width) / height);
        for (const bool mixed : {false, true})
            for (const bool hidden : {false, true})
            {
                const auto queue = makeQueue(false, mixed), alternate = makeQueue(true, mixed);
                MemoryTarget image(width, height), other(width, height);
                Snapshot expected, otherExpected;
                {
                    sr::jobs::ConcurrencyLimit single(1);
                    Renderer reference;
                    reference.SetOptions({false, false, false, hidden});
                    reference.RenderScene(image.target, queue, camera.GetViewParams(), {});
                    expected = snapshot(image.target);
                    reference.RenderScene(other.target, alternate, camera.GetViewParams(), {});
                    otherExpected = snapshot(other.target);
                }
                const auto center = static_cast<std::size_t>((height / 2) * width + width / 2);
                require(expected.colors[center] == 0xFF0000u && otherExpected.colors[center] == 0x0000FFu,
                        "command-local exact-depth tie keeps first material across merge");
                require(expected.owners[center] < 32 && expected.depths[center] > 0.f,
                        "tie fixture center belongs to a triangle of the first command");
                require(std::ranges::find(expected.colors, 0x00FF00u) != expected.colors.end(),
                        "tile merge fixture contains visible nearer material");
                require(expected.depths == otherExpected.depths && expected.owners == otherExpected.owners,
                        "material-only queue change leaves depth and stable primitive IDs unchanged");
                for (const bool single : {true, false})
                {
                    sr::jobs::ConcurrencyLimit limit(single ? 1 : defaultWorkers);
                    for (int options = 0; options < 8; ++options)
                    {
                        const bool stats = (options & 4) != 0;
                        reused.SetOptions({(options & 1) != 0, (options & 2) != 0, stats, hidden});
                        independent.SetOptions({(options & 2) != 0, (options & 1) != 0, stats, hidden});
                        image.target.Clear();
                        other.target.Clear();
                        // 서로 다른 renderer/target의 프레임은 같은 worker들이 동시에 그려도 독립이어야 한다.
                        sr::jobs::ParallelFor<sr::jobs::Split::Fixed>(
                            0, 2, 1,
                            [&](int first, int last)
                            {
                                for (int side = first; side != last; ++side)
                                    if (side == 0)
                                        reused.RenderScene(image.target, queue, camera.GetViewParams(), {});
                                    else
                                        independent.RenderScene(other.target, alternate,
                                                                camera.GetViewParams(), {});
                            });
                        require(
                            snapshot(image.target) == expected && snapshot(other.target) == otherExpected,
                            "private tile merge preserves pixels depth owners across workers and options");
                        if (stats)
                            require(reused.GetStats().preparedTriangles > 0 &&
                                        reused.GetStats().binReferences > 0,
                                    "tile merge fixture actually prepares and bins geometry");
                    }
                    // 가득 찬 프레임 뒤 worker scratch/병합 벡터에 남은 포인터가 재사용되면 안 된다.
                    const RenderQueue empty;
                    image.target.Clear();
                    const auto cleared = snapshot(image.target);
                    reused.SetOptions({true, true, true, hidden});
                    reused.RenderScene(image.target, empty, camera.GetViewParams(), {});
                    require(snapshot(image.target) == cleared && reused.GetStats().preparedTriangles == 0 &&
                                reused.GetStats().binReferences == 0,
                            "empty queue clears worker and merged tile submissions");
                    require(snapshot(other.target) == otherExpected,
                            "clearing another renderer does not mutate independent output");

                    RenderQueue xrayOnly;
                    for (auto command : queue.GetRenderCommands())
                    {
                        command.rasterizeMode = ERasterizeMode::Wireframe;
                        xrayOnly.Submit(command);
                    }
                    image.target.Clear();
                    reused.SetOptions({false, false, true, false});
                    reused.RenderScene(image.target, xrayOnly, camera.GetViewParams(), {});
                    require(reused.GetStats().binReferences == 0 && reused.GetStats().coveragePassed == 0 &&
                                std::ranges::all_of(image.target.PrimitiveIds(),
                                                    [](auto id) { return id == RenderTarget::emptyOwner; }),
                            "X-ray-only frame does not reuse filled tile references");
                    require(std::ranges::any_of(image.storage, [](auto pixel) { return pixel == 0xFFFFFFu; }),
                            "X-ray transition fixture draws actual wire pixels");
                    image.target.Clear();
                    reused.SetOptions({true, true, true, hidden});
                    reused.RenderScene(image.target, queue, camera.GetViewParams(), {});
                    require(snapshot(image.target) == expected,
                            "filled tile counts recover after an X-ray-only frame");
                }
            }
    }
}

void wireOptionTests()
{
    // 실제 renderer의 64행 단위 분배도 여러 band를 지나도록 한다.
    // 작은 단일 band만으로는 wireBins 등록/프레임 초기화 오류를 놓친다.
    MemoryTarget image(129, 193);
    SyntheticScene scene(129, 193, true, true);
    Renderer renderer;
    require(!renderer.GetOptions().hiddenWireframe, "renderer default is fast X-ray wire");
    RenderQueue frontOnly;
    frontOnly.Submit(scene.queue.GetRenderCommands().front());
    std::array<Snapshot, 2> expected;
    for (bool hidden : {false, true})
    {
        const auto index = static_cast<std::size_t>(hidden);
        renderer.SetOptions({false, false, true, hidden});
        image.target.Clear();
        renderer.RenderScene(image.target, frontOnly, scene.camera.GetViewParams(), scene.lights);
        const auto front = snapshot(image.target);
        image.target.Clear();
        renderer.RenderScene(image.target, scene.queue, scene.camera.GetViewParams(), scene.lights);
        expected[index] = snapshot(image.target);
        if (hidden)
        {
            require(expected[index] == front, "hidden-wire option removes rear edges behind front face");
            require(std::ranges::any_of(expected[index].owners,
                                        [](auto id) { return id != RenderTarget::emptyOwner; }),
                    "hidden wire populates face depth");
        }
        else
        {
            require(expected[index].colors != front.colors, "default X-ray wire retains rear edges");
            require(std::ranges::all_of(expected[index].owners,
                                        [](auto id) { return id == RenderTarget::emptyOwner; }),
                    "default wire skips face depth ownership");
            require(renderer.GetStats().binReferences == 0 && renderer.GetStats().coveragePassed == 0,
                    "default wire skips triangle binning and face rasterization");
        }
        for (bool single : {true, false})
        {
            sr::jobs::ConcurrencyLimit limit(single ? 1 : sr::jobs::MaxConcurrency());
            for (int iteration = 0; iteration < 100; ++iteration)
            {
                renderer.SetOptions({(iteration & 1) != 0, (iteration & 2) != 0, iteration == 99, hidden});
                image.target.Clear();
                renderer.RenderScene(image.target, scene.queue, scene.camera.GetViewParams(), scene.lights);
                require(snapshot(image.target) == expected[index],
                        "wire option 100-run worker/sort/Hi-Z determinism");
            }
        }
    }
    require(expected[0].colors != expected[1].colors, "wire visibility toggle changes rear-edge visibility");

    // 앞면이 채움이고 뒷면이 와이어인 혼합 큐도 X-ray/hidden 정책을 따른다.
    auto fillCommand = scene.queue.GetRenderCommands().front();
    fillCommand.rasterizeMode = ERasterizeMode::Fill;
    RenderQueue filledFront, mixed;
    filledFront.Submit(fillCommand);
    mixed.Submit(fillCommand);
    for (const auto &command : scene.queue.GetRenderCommands().subspan(1))
        mixed.Submit(command);
    renderer.SetOptions({});
    image.target.Clear();
    renderer.RenderScene(image.target, filledFront, scene.camera.GetViewParams(), scene.lights);
    const auto fillReference = snapshot(image.target);
    image.target.Clear();
    renderer.RenderScene(image.target, mixed, scene.camera.GetViewParams(), scene.lights);
    const auto xrayMixed = snapshot(image.target);
    require(xrayMixed.colors != fillReference.colors && xrayMixed.depths == fillReference.depths &&
                xrayMixed.owners == fillReference.owners,
            "mixed X-ray rear wires overlay filled face without altering depth");
    renderer.SetOptions({false, false, false, true});
    image.target.Clear();
    renderer.RenderScene(image.target, mixed, scene.camera.GetViewParams(), scene.lights);
    require(snapshot(image.target) == fillReference, "mixed hidden rear wires stay behind filled face");
}

void parallelDebugSubmissionTests()
{
    // 새 준비/분배 경로와 독립적인 기존 순차 DrawDebugPrimitive를 비교한다.
    // fallback 경계, 색상 덮어쓰기 순서, 큰 프레임 뒤 빈 프레임과 resize를 함께 검사한다.
    Renderer reused;
    for (const auto size : {std::array{129, 193}, std::array{173, 131}, std::array{129, 193}})
    {
        const int width = size[0], height = size[1];
        SyntheticScene scene(width, height, true);
        RenderQueue geometry;
        geometry.Submit(scene.queue.GetRenderCommands().front());
        auto wire = scene.queue.GetRenderCommands()[1];
        wire.rasterizeMode = ERasterizeMode::Wireframe;
        geometry.Submit(wire);
        const auto preparedLights = prepareLights(scene.lights);
        const RenderFrameContext frame{scene.camera.GetProjectionMatrix() * scene.camera.GetViewMatrix(),
                                       scene.camera.GetCameraPos(), preparedLights, (width - 1) / tileSize + 1,
                                       (height - 1) / tileSize + 1};
        for (const auto algorithm : {ELineAlgorithm::Bresenham, ELineAlgorithm::DDA})
            for (const bool single : {true, false})
            {
                sr::jobs::ConcurrencyLimit limit(single ? 1 : sr::jobs::MaxConcurrency());
                for (const int lineCount : {255, 256, 769, 0, 1, 257, 0})
                {
                    RenderQueue queue = geometry;
                    // overlay를 큐 앞에 넣어도 모든 depth 선 뒤에 그리는 정책은 동일해야 한다.
                    DebugPrimitiveCommand overlay;
                    overlay.type = DebugPrimitiveType::Line;
                    overlay.depthPolicy = DebugDepthPolicy::Overlay;
                    overlay.worldTransform = SRMath::mat4::identity();
                    overlay.vertices = {{{-1.f, 0.f, 2.f}, {1.f, 0.f, 1.f, 1.f}},
                                        {{1.f, 0.f, 2.f}, {1.f, 0.f, 1.f, 1.f}}};
                    queue.Submit(overlay);
                    for (int batch = 0; batch < 3; ++batch)
                    {
                        DebugPrimitiveCommand command;
                        command.type = DebugPrimitiveType::Line;
                        command.depthPolicy = DebugDepthPolicy::DepthTested;
                        command.worldTransform = SRMath::mat4::identity();
                        command.worldTransform[3][0] = static_cast<float>(batch - 1) * .03f;
                        for (int i = batch; i < lineCount; i += 3)
                        {
                            const float y = static_cast<float>(i % 29 - 14) * .12f;
                            const float z = i % 4 == 0 ? -1.f : 2.f;
                            SRMath::vec4 color{static_cast<float>(i % 3 == 0), static_cast<float>(i % 3 == 1),
                                               static_cast<float>(i % 3 == 2), 1.f};
                            SRMath::vec3 a{-2.f, y, z}, b{2.f, -y, z};
                            if (i % 13 == 0)
                                a.z = 5.5f; // 카메라/near 평면을 교차한다.
                            if (i % 31 == 0)
                                b = a; // 한 점 선도 기존 끝점 규칙을 따른다.
                            if (i % 47 == 0)
                                a.x = std::numeric_limits<float>::quiet_NaN();
                            // 첫 명령의 두 번째 chunk에도 실제 보이는 고유색 선을 둔다.
                            // 가려진 선만 있으면 chunk 누락 버그가 픽셀 비교를 통과할 수 있다.
                            if (i == 768)
                            {
                                a = {-.8f, .7f, 2.f};
                                b = {.8f, .7f, 2.f};
                                color = {1.f, 1.f, 0.f, 1.f};
                            }
                            command.vertices.push_back({a, color});
                            command.vertices.push_back({b, color});
                        }
                        // 홀수 꼬리 정점은 명령 간에 합쳐지면 안 된다.
                        command.vertices.push_back({{0.f, 0.f, 2.f}, {1.f, 1.f, 1.f, 1.f}});
                        queue.Submit(std::move(command));
                    }
                    MemoryTarget reference(width, height), tested(width, height);
                    Renderer serialGeometry;
                    serialGeometry.SetLineAlgorithm(algorithm);
                    serialGeometry.SetOptions({false, false, false, lineCount == 769});
                    serialGeometry.RenderScene(reference.target, geometry, scene.camera.GetViewParams(), scene.lights);
                    RenderStats expectedStats;
                    Rasterizer serialLines(reference.target, frame, &expectedStats);
                    for (const auto policy : {DebugDepthPolicy::DepthTested, DebugDepthPolicy::Overlay})
                        for (const auto &command : queue.GetDebugCommands())
                            if (command.depthPolicy == policy)
                                serialLines.DrawDebugPrimitive(command, algorithm);
                    const auto expected = snapshot(reference.target);
                    if (lineCount == 769)
                        require(std::ranges::find(expected.colors, 0x00ffff00u) != expected.colors.end(),
                                "second debug preparation chunk contains a visible yellow sentinel");
                    for (const bool stats : {true, false})
                    {
                        reused.SetLineAlgorithm(algorithm);
                        reused.SetOptions({false, false, stats, lineCount == 769});
                        tested.target.Clear();
                        reused.RenderScene(tested.target, queue, scene.camera.GetViewParams(), scene.lights);
                        require(snapshot(tested.target) == expected,
                                "prepared debug bands preserve queue/color order, resize and empty frames");
                        if (stats)
                            require((reused.GetStats().liveDebugLineBytes != 0) == (lineCount >= 256),
                                    "debug preparation uses the documented 256-line threshold");
                        // 와이어 깊이 기각은 별도 계측이므로 hidden 모드에서는 색/깊이/owner만 비교한다.
                        if (stats && lineCount != 769)
                            require(reused.GetStats().lineDepthRejected == expectedStats.lineDepthRejected,
                                    "parallel normal rejection statistics match sequential reference");
                    }
                }
                // odd-tail 명령도 없는 완전히 빈 debug queue로 scratch 초기화를 확인한다.
                MemoryTarget emptyReference(width, height), emptyTested(width, height);
                Renderer geometryOnly;
                geometryOnly.SetLineAlgorithm(algorithm);
                geometryOnly.RenderScene(emptyReference.target, geometry, scene.camera.GetViewParams(), scene.lights);
                reused.SetOptions({false, false, true, false});
                reused.RenderScene(emptyTested.target, geometry, scene.camera.GetViewParams(), scene.lights);
                require(snapshot(emptyTested.target) == snapshot(emptyReference.target) &&
                            reused.GetStats().liveDebugLineBytes == 0,
                        "empty debug queue clears prepared line state after a large frame");
            }
    }
}

void preparedDebugSlotFreshnessTests()
{
    constexpr int width = 97, height = 131;
    SyntheticScene scene(width, height, true);
    RenderQueue geometry;
    geometry.Submit(scene.queue.GetRenderCommands().front());
    const auto preparedLights = prepareLights(scene.lights);
    const RenderFrameContext frame{scene.camera.GetProjectionMatrix() * scene.camera.GetViewMatrix(),
                                   scene.camera.GetCameraPos(), preparedLights, (width - 1) / tileSize + 1,
                                   (height - 1) / tileSize + 1};
    const auto defaultWorkers = sr::jobs::MaxConcurrency();
    for (const auto algorithm : {ELineAlgorithm::Bresenham, ELineAlgorithm::DDA})
        for (const bool single : {true, false})
            for (const bool stats : {false, true})
            {
                sr::jobs::ConcurrencyLimit limit(single ? 1 : defaultWorkers);
                Renderer reused;
                reused.SetLineAlgorithm(algorithm);
                reused.SetOptions({false, false, stats, false});
                MemoryTarget tested(width, height);
                // 각 크기에서 valid→전부 NaN→다른 색/MVP valid→전부 clip reject→valid.
                // 같은 size의 optional도 nullopt 결과를 매번 대입해야 옛 engaged 슬롯이 사라진다.
                for (const int lineCount : {769, 256, 257, 255, 0, 769})
                    for (int phase = 0; phase < 5; ++phase)
                    {
                        RenderQueue queue = geometry;
                        DebugPrimitiveCommand overlay;
                        overlay.worldTransform = SRMath::mat4::identity();
                        overlay.depthPolicy = DebugDepthPolicy::Overlay;
                        overlay.vertices = {{{-.8f, -.9f, 2.f}, {1.f, 0.f, 1.f, 1.f}},
                                            {{.8f, -.9f, 2.f}, {1.f, 0.f, 1.f, 1.f}}};
                        queue.Submit(overlay); // 제출 위치와 관계없이 depth 선 뒤에 표시한다.
                        auto ignoredOverlay = overlay;
                        ignoredOverlay.type = DebugPrimitiveType::Sphere;
                        queue.Submit(std::move(ignoredOverlay));
                        DebugPrimitiveCommand ignored = overlay;
                        ignored.type = DebugPrimitiveType::Triangle;
                        ignored.depthPolicy = DebugDepthPolicy::DepthTested;
                        ignored.vertices.resize(520, {{0.f, 0.f, 2.f}, {1.f, 1.f, 1.f, 1.f}});
                        queue.Submit(std::move(ignored)); // non-Line 정점은 threshold에 포함되지 않는다.
                        const bool invalid = phase == 1 || phase == 3;
                        const SRMath::vec4 color = phase == 0   ? SRMath::vec4{1.f, 0.f, 0.f, 1.f}
                                                   : phase == 2 ? SRMath::vec4{0.f, 1.f, 0.f, 1.f}
                                                                : SRMath::vec4{0.f, 0.f, 1.f, 1.f};
                        const unsigned int expectedColor = phase == 0   ? 0xFF0000u
                                                           : phase == 2 ? 0x00FF00u
                                                                        : 0x0000FFu;
                        for (int batch = 0; batch < 3; ++batch)
                        {
                            DebugPrimitiveCommand command;
                            command.worldTransform = SRMath::mat4::identity();
                            command.worldTransform[3][0] = phase * .025f;
                            command.worldTransform[3][1] = batch * .015f - phase * .02f;
                            command.depthPolicy = DebugDepthPolicy::DepthTested;
                            for (int i = batch; i < lineCount; i += 3)
                            {
                                const float y = static_cast<float>(i % 31 - 15) * .04f;
                                const float z = i % 7 == 0 ? -1.f : 2.f;
                                SRMath::vec3 a{-.8f, y, z}, b{.8f, y, z};
                                if (phase == 1)
                                    a.x = std::numeric_limits<float>::quiet_NaN();
                                if (phase == 3)
                                    a.x = b.x = 10000.f;
                                command.vertices.push_back({a, color});
                                command.vertices.push_back({b, color});
                            }
                            command.vertices.push_back({{0.f, 0.f, 2.f}, {1.f, 1.f, 1.f, 1.f}});
                            queue.Submit(std::move(command));
                        }
                        // 준비 API를 쓰지 않는 기존 순차 DrawDebugPrimitive가 매 프레임 기준이다.
                        MemoryTarget reference(width, height);
                        Renderer geometryOnly;
                        geometryOnly.RenderScene(reference.target, geometry, scene.camera.GetViewParams(), scene.lights);
                        RenderStats expectedStats;
                        Rasterizer original(reference.target, frame, &expectedStats);
                        for (const auto policy : {DebugDepthPolicy::DepthTested, DebugDepthPolicy::Overlay})
                            for (const auto &command : queue.GetDebugCommands())
                                if (command.depthPolicy == policy)
                                    original.DrawDebugPrimitive(command, algorithm);
                        const auto expected = snapshot(reference.target);
                        if (lineCount != 0 && !invalid)
                            require(std::ranges::find(expected.colors, expectedColor) !=
                                        expected.colors.end(),
                                    "freshness valid frame contains its new primary line color");
                        tested.target.Clear();
                        reused.RenderScene(tested.target, queue, scene.camera.GetViewParams(), scene.lights);
                        require(snapshot(tested.target) == expected,
                                "reused optional debug slots overwrite invalid results and never read shrunk "
                                "tails");
                        if (stats)
                        {
                            require((reused.GetStats().liveDebugLineBytes != 0) == (lineCount >= 256),
                                    "small empty and non-Line frames have zero live prepared line bytes");
                            require(reused.GetStats().lineDepthRejected == expectedStats.lineDepthRejected,
                                    "debug slot freshness preserves sequential depth rejection counts");
                            if (invalid || lineCount == 0)
                                require(reused.GetStats().lineDepthRejected == 0,
                                        "invalid prepared slots produce no stale depth tests");
                        }
                    }
                const RenderQueue empty;
                tested.target.Clear();
                const auto cleared = snapshot(tested.target);
                reused.RenderScene(tested.target, empty, scene.camera.GetViewParams(), scene.lights);
                require(snapshot(tested.target) == cleared && reused.GetStats().liveDebugLineBytes == 0,
                        "completely empty frame releases live slots after a reused large frame");
            }
}

void independentLineTraversalTests()
{
    constexpr int width = 37, height = 29;
    constexpr std::size_t stride = 43;
    constexpr unsigned int lineColor = 0x00ABCDEFu;
    struct Line
    {
        SRMath::vec2 a, b;
        bool analyticYMajor = false;
        int xAtZero = 0, xAboveZero = 0;
    };
    std::vector<Line> cases;
    const auto add = [&](SRMath::vec2 a, SRMath::vec2 b)
    {
        cases.push_back({a, b});
        cases.push_back({b, a});
    };
    for (const auto delta :
         {std::array{13, 5}, std::array{5, 13}, std::array{-5, 13}, std::array{-13, 5}, std::array{-13, -5},
          std::array{-5, -13}, std::array{5, -13}, std::array{13, -5}, std::array{12, 6}, std::array{6, 12},
          std::array{12, 0}, std::array{0, 12}, std::array{0, 0}})
        add({18.25f, 14.75f}, {18.25f + delta[0], 14.75f + delta[1]});
    add({-12.75f, -4.25f}, {48.5f, 33.75f});
    add({-.75f, -.25f}, {.75f, .25f}); // 정수 끝점은 같지만 원래 float 투영 위치는 다르다.
    add({-8.5f, 8.f}, {46.75f, 8.f});
    add({18.f, -9.75f}, {18.f, 39.5f});
    add({0.f, 0.f}, {24.f, 12.f});
    add({0.f, 0.f}, {12.f, 24.f});
    add({5.f, 17.f}, {31.f, 17.f});
    add({5.f, 28.f}, {31.f, 28.f});
    add({-1536.75f, -1024.25f}, {2048.5f, 1537.75f});
    // 거대한 좌표도 순회 길이는 0~8로 제한한다. billion-step oracle은 실행하지 않는다.
    add({100000000.f, 10.f}, {100000008.f, 10.f});
    add({-100000000.f, 10.f}, {-100000008.f, 10.f});
    add({std::numeric_limits<float>::max(), 10.f}, {0.f, 10.f});
    add({std::numeric_limits<float>::quiet_NaN(), 10.f}, {0.f, 10.f});
    std::uint32_t state = 0x67BC194Du;
    const auto coordinate = [&]
    {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>((state >> 9) % 321) * .25f - 22.75f;
    };
    for (int i = 0; i < 64; ++i)
    {
        const float ax = coordinate(), ay = coordinate(), bx = coordinate(), by = coordinate();
        add({ax, ay}, {bx, by});
    }
    // dy=2L, |dx|=1의 원래 Bresenham 재귀는 L번째 step의 y=0에서 X를 바꾼다.
    // 역방향은 같은 tie를 반대쪽으로 택한다. 보이는 29개 점만 해석적으로 생성해
    // int32 doubled-error 범위를 넘는 입력도 거대한 기준 순회 없이 검사한다.
    constexpr float largeY = 536870880.f;
    cases.push_back({{12.f, -largeY}, {13.f, largeY}, true, 13, 13});
    cases.push_back({{13.f, largeY}, {12.f, -largeY}, true, 12, 13});
    cases.push_back({{13.f, -largeY}, {12.f, largeY}, true, 12, 12});
    cases.push_back({{12.f, largeY}, {13.f, -largeY}, true, 13, 12});
    const RenderFrameContext frame{SRMath::mat4::identity(), {}, {}, 3, 2};
    struct Image
    {
        std::vector<unsigned int> pixels;
        RenderTarget target;
        Image() : pixels(stride * height, 0xDEADBEEFu)
        {
            require(target.Attach({pixels, width, height, stride}).has_value(), "independent line target");
            const float boundary = .75f + 8.f * std::numeric_limits<float>::epsilon() * .75f;
            const std::array depths{
                0.f, .2f, .75f, boundary, std::nextafter(boundary, std::numeric_limits<float>::infinity()),
                1.5f};
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x)
                {
                    const auto index = static_cast<std::size_t>(y * width + x);
                    pixels[static_cast<std::size_t>(y) * stride + x] =
                        index % 11 == 0 ? lineColor : 0x00112233u;
                    target.Depth()[index] = depths[index % depths.size()];
                    target.PrimitiveIds()[index] = 100 + index;
                }
        }
    };
    const std::array bands{std::array{0, height},
                           std::array{0, 0},
                           std::array{-8, 0},
                           std::array{height, height + 7},
                           std::array{12, 12},
                           std::array{8, 17},
                           std::array{17, 8},
                           std::array{-5, 4},
                           std::array{height - 3, height + 5}};
    for (const auto algorithm : {ELineAlgorithm::DDA, ELineAlgorithm::Bresenham})
        for (std::size_t caseIndex = 0; caseIndex < cases.size(); ++caseIndex)
        {
            const auto line = cases[caseIndex];
            if (line.analyticYMajor && algorithm == ELineAlgorithm::DDA)
                continue; // 변경되지 않은 DDA에는 billion-step 입력을 실행하지 않는다.
            // 원래의 전체 정수 순회 자체가 oracle이다. band로 시작점/오차를 재계산하지 않는다.
            std::vector<std::array<int, 2>> points;
            constexpr double limit = static_cast<double>(std::numeric_limits<int>::max()) / 4.;
            const auto valid = [&](float v)
            { return std::isfinite(v) && std::abs(static_cast<double>(v)) <= limit; };
            if (line.analyticYMajor)
            {
                for (int y = 0; y < height; ++y)
                    points.push_back({y == 0 ? line.xAtZero : line.xAboveZero, y});
            }
            else if (valid(line.a.x) && valid(line.a.y) && valid(line.b.x) && valid(line.b.y))
            {
                int x = static_cast<int>(line.a.x), y = static_cast<int>(line.a.y);
                const int endX = static_cast<int>(line.b.x), endY = static_cast<int>(line.b.y);
                if (algorithm == ELineAlgorithm::DDA)
                {
                    const int dx = endX - x, dy = endY - y;
                    const int steps = std::max(std::abs(dx), std::abs(dy));
                    if (steps == 0)
                        points.push_back({x, y});
                    else
                    {
                        const double incrementX = dx / static_cast<double>(steps);
                        const double incrementY = dy / static_cast<double>(steps);
                        double currentX = x, currentY = y;
                        for (int i = 0; i <= steps; ++i)
                        {
                            points.push_back({static_cast<int>(std::round(currentX)),
                                              static_cast<int>(std::round(currentY))});
                            currentX += incrementX;
                            currentY += incrementY;
                        }
                    }
                }
                else
                {
                    const int dx = std::abs(endX - x), sx = x < endX ? 1 : -1;
                    const int dy = -std::abs(endY - y), sy = y < endY ? 1 : -1;
                    int error = dx + dy;
                    for (;;)
                    {
                        points.push_back({x, y});
                        if (x == endX && y == endY)
                            break;
                        const int twice = 2 * error;
                        if (twice >= dy)
                        {
                            error += dy;
                            x += sx;
                        }
                        if (twice <= dx)
                        {
                            error += dx;
                            y += sy;
                        }
                    }
                }
            }
            for (int mode = 0; mode < 4; ++mode)
                for (const bool instrumented : {false, true})
                    for (const auto band : bands)
                    {
                        const bool depthTest = mode != 0;
                        float qa = .75f, qb = mode >= 2 ? 1.8f : qa;
                        if (caseIndex % 19 == 0)
                            qa = qb = 0.f;
                        else if (caseIndex % 23 == 0)
                            qa = qb = std::numeric_limits<float>::quiet_NaN();
                        else if (caseIndex % 29 == 0)
                            qa = qb = std::numeric_limits<float>::infinity();
                        const float slopeX = mode == 3 ? .015625f : 0.f;
                        const float slopeY = mode == 3 ? -.03125f : 0.f;
                        const int minY = std::clamp(band[0], 0, height);
                        const int maxY = std::clamp(band[1], minY, height);
                        Image expected, actual;
                        const std::vector<float> untouchedDepth(actual.target.Depth().begin(),
                                                                actual.target.Depth().end());
                        const std::vector<std::uint64_t> untouchedOwners(actual.target.PrimitiveIds().begin(),
                                                                         actual.target.PrimitiveIds().end());
                        std::uint64_t rejected = 0;
                        for (const auto point : points)
                        {
                            const int x = point[0], y = point[1];
                            if (x < 0 || x >= width || y < minY || y >= maxY)
                                continue;
                            if (depthTest)
                            {
                                const double vx = static_cast<double>(line.b.x) - line.a.x;
                                const double vy = static_cast<double>(line.b.y) - line.a.y;
                                const double lengthSquared = vx * vx + vy * vy;
                                double amount = 0.;
                                if (slopeX != 0.f || slopeY != 0.f || qa != qb)
                                    amount = lengthSquared > 0. ? std::clamp(((x + .5 - line.a.x) * vx +
                                                                              (y + .5 - line.a.y) * vy) /
                                                                                 lengthSquared,
                                                                             0., 1.)
                                                                : 0.;
                                const double q = qa == qb ? qa : qa + (static_cast<double>(qb) - qa) * amount;
                                double slopeBias = 0.;
                                if (slopeX != 0.f || slopeY != 0.f)
                                {
                                    const double lineX = line.a.x + vx * amount,
                                                 lineY = line.a.y + vy * amount;
                                    slopeBias = std::abs(slopeX * (x + .5 - lineX)) +
                                                std::abs(slopeY * (y + .5 - lineY));
                                }
                                const double bias =
                                    slopeBias +
                                    8. * std::numeric_limits<float>::epsilon() *
                                        std::max(std::abs(q),
                                                 static_cast<double>(std::numeric_limits<float>::min()));
                                if (!std::isfinite(q) || !(q > 0.) ||
                                    q + bias <
                                        expected.target.Depth()[static_cast<std::size_t>(y * width + x)])
                                {
                                    if (instrumented)
                                        ++rejected;
                                    continue;
                                }
                            }
                            expected.pixels[static_cast<std::size_t>(y) * stride + x] = lineColor;
                        }
                        RenderStats counters;
                        Rasterizer rasterizer(actual.target, frame, instrumented ? &counters : nullptr);
                        rasterizer.BeginLineRows(band[0], band[1]);
                        RasterizerTestAccess::ScreenLine(rasterizer, line.a, line.b, qa, qb, lineColor,
                                                         algorithm, depthTest, slopeX, slopeY);
                        require(actual.pixels == expected.pixels,
                                "banded line matches independent original integer traversal and depth plot");
                        require(std::ranges::equal(actual.target.Depth(), untouchedDepth) &&
                                    std::ranges::equal(actual.target.PrimitiveIds(), untouchedOwners),
                                "banded line oracle preserves exact depth owners and padded rows");
                        require(counters.lineDepthRejected == rejected,
                                "banded depth rejection counts match independent original traversal");
                    }
        }
}

void nearPlaneTests()
{
    MemoryTarget image(65, 49);
    Renderer renderer;
    Camera camera({0.f, 0.f, 5.f});
    camera.SetCameraYaw(std::numbers::pi_v<float>);
    std::array<bool, 256> keys{};
    camera.Update(0.f, keys, 65.f / 49.f);
    for (float z : {5.f, 5.2f})
    {
        Mesh mesh;
        mesh.vertices = {{{-.3f, -.3f, z}, {0.f, 0.f}, {0.f, 0.f, 1.f}},
                         {{.7f, -.3f, 4.f}, {1.f, 0.f}, {0.f, 0.f, 1.f}},
                         {{0.f, .7f, 4.f}, {0.f, 1.f}, {0.f, 0.f, 1.f}}};
        // near-plane clipping 뒤 남는 fan도 렌더러의 CW 앞면 규약을 따른다.
        mesh.indices = {0, 2, 1};
        mesh.material.ambient = {1.f, .5f, .25f};
        RenderQueue queue;
        queue.Submit(MeshRenderCommand{&mesh, mesh.indices, SRMath::mat4::identity(), &mesh.material});
        image.target.Clear();
        renderer.RenderScene(image.target, queue, camera.GetViewParams(), {});
        require(std::ranges::any_of(image.storage, [](auto pixel) { return pixel != 0; }),
                "triangle crossing camera/near plane survives clipping");
        require(std::ranges::all_of(image.target.Depth(), [](float q) { return std::isfinite(q); }),
                "near plane cannot introduce nonfinite depth");
    }
}
} // namespace

void sr::test::RegisterVisibilityTests(TestRegistry &registry)
{
    registry.Add("visibility.depthAndHiZ", depthAndHiZTests);
    registry.Add("visibility.variableDepthHiZ", variableDepthHiZTests);
    registry.Add("visibility.hiddenLine", hiddenLineTests);
    registry.Add("visibility.determinism", determinismTests);
    registry.Add("visibility.tileMergeSubmission", tileMergeSubmissionTests);
    registry.Add("visibility.wireOption", wireOptionTests);
    registry.Add("visibility.parallelDebugSubmission", parallelDebugSubmissionTests);
    registry.Add("visibility.preparedDebugSlotFreshness", preparedDebugSlotFreshnessTests);
    registry.Add("visibility.independentLineTraversal", independentLineTraversalTests);
    registry.Add("visibility.nearPlane", nearPlaneTests);
}

void RunVisibilityBenchmark(int argc, char **argv)
{
    const auto integer = [&](int index, int fallback)
    {
        if (argc <= index)
            return fallback;
        const auto parsed = sr::asset::parseNumber<int>(argv[index]);
        require(parsed.has_value(), "visibility benchmark integer");
        return *parsed;
    };
    const int frames = integer(3, 600), width = integer(4, 1280), height = integer(5, 720);
    const std::string_view mode = argc > 6 ? argv[6] : "fill";
    const std::string_view option = argc > 7 ? argv[7] : "none";
    const std::string_view scenario = argc > 8 ? argv[8] : "occluded";
    bool stats = false, hiddenWireframe = false;
    int optional = 9;
    if (argc > optional &&
        (std::string_view(argv[optional]) == "stats" || std::string_view(argv[optional]) == "nostats"))
        stats = std::string_view(argv[optional++]) == "stats";
    if (argc > optional)
    {
        require(std::string_view(argv[optional++]) == "hidden",
                "unknown visibility benchmark hidden-wire option");
        hiddenWireframe = true;
    }
    require(argc <= optional, "unexpected visibility benchmark trailing argument");
    require(frames > 0 && frames <= 100000 && width > 0 && width <= 4096 && height > 0 && height <= 2160,
            "visibility benchmark dimensions");
    require(mode == "fill" || mode == "normal" || mode == "wire" || mode == "fxaa",
            "visibility benchmark mode");
    require(option == "none" || option == "sort" || option == "hiz" || option == "both",
            "visibility benchmark option");
    require(scenario == "occluded" || scenario == "open", "visibility benchmark scenario");
    MemoryTarget image(width, height);
    SyntheticScene scene(width, height, scenario == "occluded", mode == "wire", mode == "normal");
    Renderer renderer;
    renderer.SetOptions(
        {option == "sort" || option == "both", option == "hiz" || option == "both", stats, hiddenWireframe});
    renderer.SetAAAlgorithm(mode == "fxaa" ? EAAAlgorithm::FXAA : EAAAlgorithm::None);
    std::vector<double> times;
    for (int i = -60; i < frames; ++i)
    {
        const auto start = std::chrono::steady_clock::now();
        image.target.Clear();
        renderer.RenderScene(image.target, scene.queue, scene.camera.GetViewParams(), scene.lights);
        if (i >= 0)
            times.push_back(
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    }
    double total = 0;
    for (double time : times)
        total += time;
    std::ranges::sort(times);
    std::uint64_t hash = 14695981039346656037ull;
    for (auto pixel : image.storage)
        hash = (hash ^ pixel) * 1099511628211ull;
    const auto &counters = renderer.GetStats();
    std::cout << "VISBENCH scenario=" << scenario << " mode=" << mode << " options=" << option
              << " hidden_wire=" << hiddenWireframe << " size=" << width << 'x' << height
              << " frames=" << frames << " mean_ms=" << total / frames
              << " p95_ms=" << times[static_cast<std::size_t>(frames) * 95 / 100] << " pixel_hash=" << hash
              << " coverage=" << counters.coveragePassed << " shaded=" << counters.shadedPixels
              << " hiz_rejected=" << counters.hiZRejected << " prepared=" << counters.preparedTriangles
              << " retained_tri_bytes=" << counters.retainedTriangleBytes << '\n';
}

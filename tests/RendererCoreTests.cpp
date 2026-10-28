#include "Renderer/Renderer.h"
#include "Renderer/RenderQueue.h"
#include "Renderer/DebugGizmos.h"
#include "Renderer/FXAA.h"
#include "Graphics/Model.h"
#include "Graphics/ModelLoader.h"
#include "Graphics/TextureLoader.h"
#include "Graphics/Texture.h"
#include "Graphics/Light.h"
#include "Graphics/AssetParsing.h"
#include "Scene/GameObject.h"
#include "Core/DebugFlags.h"
#include "Core/SceneRenderCollector.h"
#include "Scene/Camera.h"
#include "Math/SIMD.h"
#include "Math/Frustum.h"
#include "Utils/Jobs.h"
#include <array>
#include <algorithm>
#include <bit>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <numbers>
#include <random>
#include <stdexcept>

#include "TestHarness.h"
#include "TestSupport.h"

using namespace sr::test;


// 실행 앱에 테스트용 공개 API를 추가하지 않고 내부 계산의 계약을 확인한다.
struct RendererTestAccess
{
    static void clippingAndViewport()
    {
        ShadedVertex a{}, b{}, c{};
        a.posClip = {-0.5f, -0.5f, 0.f, 1.f};
        b.posClip = {0.f, 0.5f, 0.f, 1.f};
        c.posClip = {0.5f, -0.5f, 0.f, 1.f};
        Renderer::ClipBuffer result, first, second;
        Renderer::clipTriangle(result, a, b, c, first, second);
        check(result.size() == 3, "inside triangle");
        vectorNear(result[0].posClip, a.posClip);
        vectorNear(result[1].posClip, b.posClip);
        vectorNear(result[2].posClip, c.posClip);
        for (int axis = 0; axis < 3; ++axis)
        {
            for (float sign : {-1.f, 1.f})
            {
                auto outside = a;
                outside.posClip[axis] = sign * 2.f;
                const auto interpolated = Renderer::interpolate(c, outside, 0.5f);
                check(approximatelyEqual(interpolated.posClip.w, 1.f),
                      "clip interpolation preserves equal w");
                first.clear();
                first.emplace_back(outside);
                first.emplace_back(b);
                first.emplace_back(c);
                vectorNear(first[0].posClip, outside.posClip);
                vectorNear(first[1].posClip, b.posClip);
                vectorNear(first[2].posClip, c.posClip);
                Renderer::clipPolygonAgainstPlane(second, first, _mm_set_ps(1.f, 0.f, 0.f, 1.f));
                // clip 입력은 다음 평면에서도 사용되므로 보간이 원본을 바꾸면 안 된다.
                vectorNear(first[0].posClip, outside.posClip);
                vectorNear(first[1].posClip, b.posClip);
                vectorNear(first[2].posClip, c.posClip);
                for (const auto &vertex : second)
                {
                    if (!approximatelyEqual(vertex.posClip.w, 1.f))
                        std::cerr << "single-plane output w=" << vertex.posClip.w
                                  << ", x=" << vertex.posClip.x << '\n';
                    check(approximatelyEqual(vertex.posClip.w, 1.f), "single clip plane preserves equal w");
                }
                Renderer::clipTriangle(result, outside, b, c, first, second);
                check(result.size() >= 3 && result.size() <= result.capacity(), "plane clipping size");
                for (const auto &vertex : result)
                    for (std::size_t coordinate = 0; coordinate < 3; ++coordinate)
                    {
                        const bool inside = std::abs(vertex.posClip[coordinate]) <= vertex.posClip.w + 1e-5f;
                        if (!inside)
                            std::cerr << "clip plane: axis=" << axis << ", sign=" << sign
                                      << ", component=" << coordinate << ", position=(" << vertex.posClip.x
                                      << ", " << vertex.posClip.y << ", " << vertex.posClip.z << ", "
                                      << vertex.posClip.w << ")\n";
                        check(inside, "clip boundary");
                    }
            }
        }
        a.posClip.x = b.posClip.x = c.posClip.x = 3.f;
        Renderer::clipTriangle(result, a, b, c, first, second);
        check(result.empty(), "rejected triangle must clear previous output");

        // 클립 공간 선분의 t는 선형 매개변수다. w가 다른 경우도 UV 중점은 0.5다.
        a.posClip = {0.f, 0.f, 0.f, 1.f};
        b.posClip = {0.f, 0.f, 0.f, 3.f};
        a.texcoord = {0.f, 0.f};
        b.texcoord = {1.f, 1.f};
        const auto middle = Renderer::interpolate(a, b, 0.5f);
        check(approximatelyEqual(middle.texcoord.x, 0.5f) && approximatelyEqual(middle.posClip.w, 2.f),
              "linear clip attributes");
        const auto screen = Rasterizer::ToRasterizerVertex(middle, 160, 120);
        check(approximatelyEqual(screen.screenPos.x, 80.f) && approximatelyEqual(screen.screenPos.y, 60.f),
              "viewport center");
        check(approximatelyEqual(screen.texcoordOverW.x, 0.25f), "perspective preparation");

        std::mt19937 random(42);
        std::uniform_real_distribution<float> coordinate(-4.f, 4.f);
        for (int iteration = 0; iteration < 1000; ++iteration)
        {
            for (auto *vertex : {&a, &b, &c})
                vertex->posClip = {coordinate(random), coordinate(random), coordinate(random), 1.f};
            Renderer::clipTriangle(result, a, b, c, first, second);
            check(result.size() <= 9, "triangle/six-plane capacity bound");
        }
    }
    static void workerIsolation()
    {
        Renderer::WorkerScratch first, second;
        first.BeginCommand();
        first.triangles.emplace_back(TriangleRef{});
        first.vertexTags[0] = {first.commandStamp, 0};
        check(first.IsCached(0) && !first.IsCached(Renderer::WorkerScratch::vertexCacheSize),
              "a slot is valid only for the vertex index stored in its tag");
        first.BeginFrame();
        check(first.triangles.empty(), "frame must release triangle references");
        first.BeginCommand();
        check(!first.IsCached(0), "stale command stamp");
        check(first.vertexCache.size() == Renderer::WorkerScratch::vertexCacheSize &&
                  first.vertexTags.size() == Renderer::WorkerScratch::vertexCacheSize,
              "fixed-size vertex cache");
        check(second.commandStamp == 0, "worker isolation");
        second.BeginCommand();
        check(!second.IsCached(0), "a new worker's first command starts with an empty cache");
        first.commandStamp = std::numeric_limits<std::uint64_t>::max();
        first.vertexTags[0] = {1, 0};
        first.BeginCommand();
        check(first.commandStamp == 1 && first.vertexTags[0].stamp == 0, "stamp wrap");

        Renderer a, b;
        a.m_workerScratch.Local().triangles.emplace_back(TriangleRef{});
        b.m_workerScratch.Local().triangles.emplace_back(TriangleRef{});
        a.prepareFrame();
        check(a.m_workerScratch.Local().triangles.empty(), "first renderer reset");
        check(b.m_workerScratch.Local().triangles.size() == 1, "renderer instances isolated");
    }
    static void commandTransformCache()
    {
        Mesh mesh;
        mesh.vertices = {{{-.4f, -.3f, .1f}, {.1f, .2f}, {1.f, 2.f, 3.f}},
                         {{0.f, .4f, .2f}, {.3f, .4f}, {2.f, 1.f, 3.f}},
                         {{.4f, -.3f, .3f}, {.5f, .6f}, {3.f, 2.f, 1.f}},
                         {{.2f, .2f, .4f}, {.7f, .8f}, {1.f, 3.f, 2.f}}};
        mesh.indices = {0, 1, 2};
        Material alternateMaterial;
        const auto identity = SRMath::mat4::identity();
        MeshRenderCommand command{&mesh, mesh.indices, identity, &mesh.material};
        const auto sameFloat = [](float a, float b)
        { return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b); };
        const auto vertexComponents = [](const ShadedVertex &v)
        {
            return std::array{v.posWorld.x,    v.posWorld.y,    v.posWorld.z, v.posClip.x,
                              v.posClip.y,     v.posClip.z,     v.posClip.w,  v.normalWorld.x,
                              v.normalWorld.y, v.normalWorld.z, v.texcoord.x, v.texcoord.y};
        };
        const auto verify =
            [&](auto &scratch, const MeshRenderCommand &cmd, const SRMath::mat4 &vp, int size = 64)
        {
            const auto mvp = vp * cmd.worldTransform;
            const auto normal = cmd.rasterizeMode == ERasterizeMode::Fill
                                    ? SRMath::inverse_transpose(cmd.worldTransform).value_or(identity)
                                    : identity;
            for (int column = 0; column < 4; ++column)
                for (int row = 0; row < 4; ++row)
                    check(sameFloat(scratch.modelViewProjection[column][row], mvp[column][row]) &&
                              sameFloat(scratch.normalMatrix[column][row], normal[column][row]),
                          "cached MVP and normal matrix match direct computation");
            for (std::size_t i = 0; i < cmd.sourceMesh->vertices.size(); ++i)
            {
                ShadedVertex expected{};
                if (cmd.rasterizeMode == ERasterizeMode::Fill)
                    expected =
                        Renderer::shadeVertex(cmd.sourceMesh->vertices[i], cmd.worldTransform, mvp, normal);
                else
                    expected.posClip = mvp * SRMath::vec4(cmd.sourceMesh->vertices[i].position, 1.f);
                const auto &cached =
                    Renderer::cachedVertex(static_cast<std::uint32_t>(i), cmd, scratch.modelViewProjection,
                                           scratch.normalMatrix, scratch, size, size);
                check(std::ranges::equal(vertexComponents(cached.shaded), vertexComponents(expected),
                                         sameFloat),
                      "reused vertex cache is bitwise equal to direct shading");
                check(scratch.IsCached(static_cast<std::uint32_t>(i)),
                      "cache slot carries current transform stamp");
                // 같은 칸에 setup도 함께 채워진다. 직접 계산과 비트까지 같아야 한다.
                const auto &setup = cached.setup;
                const auto raster = Rasterizer::ToRasterizerVertex(expected, size, size);
                check(setup.outcode == Renderer::computeOutcode(expected.posClip) &&
                          sameFloat(setup.raster.screenPos.x, raster.screenPos.x) &&
                          sameFloat(setup.raster.screenPos.y, raster.screenPos.y) &&
                          sameFloat(setup.raster.oneOverW, raster.oneOverW) &&
                          sameFloat(setup.ndc.x, (SRMath::vec3(expected.posClip) / expected.posClip.w).x),
                      "cached vertex setup matches direct clip/raster setup");
            }
        };
        Renderer::WorkerScratch scratch, independent;
        const auto begin = [&](const MeshRenderCommand &cmd, const SRMath::mat4 &vp, bool expectedHit)
        {
            const auto oldStamp = scratch.commandStamp;
            check(scratch.BeginCachedCommand(cmd, vp) == expectedHit, "transform key hit/miss policy");
            check(scratch.transformCacheValid && ((scratch.commandStamp == oldStamp) == expectedHit),
                  "only transform cache hits retain the command stamp");
            verify(scratch, cmd, vp);
        };
        begin(command, identity, false);
        begin(command, identity, true);
        // 값이 우연히 같다는 비교뿐 아니라 기존 슬롯을 실제로 읽는지 직접 증명한다.
        const auto saved = scratch.vertexCache[0];
        scratch.vertexCache[0].shaded.texcoord.x = .9375f;
        check(scratch.BeginCachedCommand(command, identity), "same transform key really hits");
        check(Renderer::cachedVertex(0, command, scratch.modelViewProjection, scratch.normalMatrix, scratch,
                                     64, 64)
                      .shaded.texcoord.x == .9375f,
              "cache hit does not recompute an already stamped vertex");
        scratch.vertexCache[0] = saved;
        const std::array<unsigned int, 3> subset{1, 3, 2};
        command.material = &alternateMaterial;
        command.indicesToDraw = subset;
        begin(command, identity, true); // 재질/인덱스 부분집합은 변환 키가 아니다.

        command.worldTransform[3][0] = .25f;
        begin(command, identity, false);
        command.worldTransform[0][0] = 2.f;
        command.worldTransform[1][1] = .5f;
        command.worldTransform[2][2] = 1.5f;
        begin(command, identity, false);
        command.worldTransform[0][0] = 0.f; // singular normal matrix의 기존 identity fallback.
        begin(command, identity, false);
        command.worldTransform = identity;
        begin(command, identity, false);
        command.worldTransform[0][1] = -0.f;
        begin(command, identity, false);
        command.worldTransform[0][1] = 0.f;
        begin(command, identity, false);
        auto vp = identity;
        vp[3][1] = .125f;
        begin(command, vp, false);
        Camera camera({0.f, 0.f, 5.f});
        camera.SetCameraYaw(std::numbers::pi_v<float>);
        std::array<bool, 256> keys{};
        camera.Update(0.f, keys, 1.f);
        begin(command, camera.GetProjectionMatrix() * camera.GetViewMatrix(), false);
        camera.SetCameraPos({.5f, 0.f, 4.f});
        camera.Update(0.f, keys, 1.5f);
        begin(command, camera.GetProjectionMatrix() * camera.GetViewMatrix(), false);
        begin(command, identity, false);

        Mesh otherMesh;
        otherMesh.vertices = mesh.vertices;
        otherMesh.indices = mesh.indices;
        command.sourceMesh = &otherMesh;
        begin(command, identity, false);
        command.sourceMesh = &mesh;
        begin(command, identity, false);
        const auto *oldData = mesh.vertices.data();
        mesh.vertices.reserve(mesh.vertices.capacity() + 16);
        check(mesh.vertices.data() != oldData, "mesh storage change fixture reallocates");
        begin(command, identity, false);
        oldData = mesh.vertices.data();
        mesh.vertices.push_back(mesh.vertices.front());
        check(mesh.vertices.data() == oldData, "mesh size change fixture keeps storage address");
        begin(command, identity, false);
        command.rasterizeMode = ERasterizeMode::Wireframe;
        begin(command, identity, false);
        begin(command, identity, true);
        command.rasterizeMode = ERasterizeMode::Fill;
        begin(command, identity, false);

        check(!independent.BeginCachedCommand(command, identity),
              "different worker starts with a cache miss");
        verify(independent, command, identity);
        const auto independentStamp = independent.commandStamp;
        scratch.BeginFrame();
        check(!scratch.transformCacheValid && independent.transformCacheValid,
              "frame reset invalidates only its worker transform cache");
        // 주소/크기가 같아도 프레임 사이에 내용이 바뀌면 반드시 다시 셰이딩한다.
        mesh.vertices[0].position.x += .125f;
        mesh.vertices[0].normal = {2.f, 4.f, 1.f};
        mesh.vertices[0].texcoord.y = .875f;
        begin(command, identity, false);
        check(independent.commandStamp == independentStamp, "independent worker stamp remains unchanged");
        scratch.BeginCommand();
        check(!scratch.transformCacheValid, "legacy BeginCommand invalidates transform reuse");
        begin(command, identity, false);
        scratch.commandStamp = std::numeric_limits<std::uint64_t>::max();
        for (auto &tag : scratch.vertexTags)
            tag.stamp = 1;
        command.worldTransform[3][2] = .1f;
        check(!scratch.BeginCachedCommand(command, identity) && scratch.commandStamp == 1 &&
                  std::ranges::all_of(scratch.vertexTags, [](const auto &tag) { return tag.stamp == 0; }),
              "cache miss stamp wrap invalidates every old vertex slot before reuse");
        verify(scratch, command, identity);
        // 행렬의 NaN payload도 비교 연산이 아닌 bit key로 취급하며 finite 기각은 그대로다.
        vp = identity;
        vp[0][0] = std::bit_cast<float>(std::uint32_t{0x7FC00001});
        check(!scratch.BeginCachedCommand(command, vp) && scratch.BeginCachedCommand(command, vp),
              "identical NaN matrix bits form a stable cache key");
        vp[0][0] = std::bit_cast<float>(std::uint32_t{0x7FC00002});
        check(!scratch.BeginCachedCommand(command, vp), "different NaN payload invalidates cache key");

        // 실제 command 처리도 새 subset 정점만 채우고 material 변경에 stamp를 재발급하지 않는다.
        Renderer renderer;
        renderer.ensureViewport(32, 32);
        auto &actualWorker = renderer.m_workerScratch.Local();
        const Renderer::FrameContext frame{identity, {}, {}, 2, 2};
        command.worldTransform = identity;
        command.indicesToDraw = mesh.indices;
        const auto firstCommand = command;
        renderer.processCommand(firstCommand, frame, actualWorker, 0);
        const auto stamp = actualWorker.commandStamp;
        check(!actualWorker.IsCached(3), "first subset leaves unused vertex unshaded");
        command.indicesToDraw = subset;
        command.material = &mesh.material;
        const auto secondCommand = command;
        renderer.processCommand(secondCommand, frame, actualWorker, 1);
        check(actualWorker.commandStamp == stamp && actualWorker.IsCached(3),
              "processCommand reuses transform cache and lazily shades newly referenced vertices");
        verify(actualWorker, command, identity, 32); // renderer 뷰포트 크기로 채운 setup이다.
        check(actualWorker.triangles.size() == 2 &&
                  actualWorker.triangles[0].sourceCommand == &firstCommand &&
                  actualWorker.triangles[1].sourceCommand == &secondCommand &&
                  actualWorker.triangles[0].primitiveId == 0 && actualWorker.triangles[1].primitiveId == 16,
              "cache hit does not reuse previous command material ownership or primitive identity");

        // direct-mapped 칸 충돌: 번호가 vertexCacheSize만큼 다른 정점은 같은 칸을 쓴다. 한 삼각형의
        // 세 꼭짓점이 모두 같은 칸이어도, 칸을 나눠 쓰는 경우와 결과가 비트까지 같아야 한다.
        constexpr std::uint32_t slots = Renderer::WorkerScratch::vertexCacheSize;
        Mesh wide;
        wide.vertices.resize(2 * slots + 3, mesh.vertices[3]);
        wide.vertices[0] = mesh.vertices[0];
        wide.vertices[slots] = mesh.vertices[1];
        wide.vertices[2 * slots] = mesh.vertices[2];
        wide.vertices[2 * slots + 1] = mesh.vertices[0];
        wide.vertices[2 * slots + 2] = mesh.vertices[1];
        wide.vertices[3] = mesh.vertices[2];
        const std::array<unsigned int, 6> colliding{0, slots, 2 * slots, 2 * slots + 1, 2 * slots + 2, 3};
        MeshRenderCommand wideCommand{&wide, colliding, identity, &mesh.material};
        Renderer collider;
        collider.ensureViewport(32, 32);
        auto &collidingWorker = collider.m_workerScratch.Local();
        collider.processCommand(wideCommand, frame, collidingWorker, 0);
        const auto expectedTriangle = Rasterizer::Prepare(
            Renderer::shadeVertex(mesh.vertices[0], identity, identity, identity),
            Renderer::shadeVertex(mesh.vertices[1], identity, identity, identity),
            Renderer::shadeVertex(mesh.vertices[2], identity, identity, identity), 32, 32, 0, &wideCommand);
        const auto sameTriangle = [&](const TriangleRef &actual, std::uint64_t id)
        {
            return expectedTriangle && actual.primitiveId == id &&
                   std::bit_cast<std::uint32_t>(actual.rv0.screenPos.x) ==
                       std::bit_cast<std::uint32_t>(expectedTriangle->rv0.screenPos.x) &&
                   std::bit_cast<std::uint32_t>(actual.rv1.screenPos.y) ==
                       std::bit_cast<std::uint32_t>(expectedTriangle->rv1.screenPos.y) &&
                   std::bit_cast<std::uint32_t>(actual.rv2.oneOverW) ==
                       std::bit_cast<std::uint32_t>(expectedTriangle->rv2.oneOverW) &&
                   actual.dx01Fixed == expectedTriangle->dx01Fixed &&
                   actual.dy20Fixed == expectedTriangle->dy20Fixed;
        };
        check(collidingWorker.triangles.size() == 2 && sameTriangle(collidingWorker.triangles[0], 0) &&
                  sameTriangle(collidingWorker.triangles[1], 16),
              "triangles whose corners share one cache slot match the non-colliding result");
    }
    static void flatTileReferenceLayout()
    {
        Renderer renderer;
        std::array<Renderer::WorkerScratch, 2> workers;
        std::array<TriangleRef, 7> triangles{};
        for (std::size_t i = 0; i < triangles.size(); ++i)
            triangles[i].primitiveId = i;
        const auto append = [](auto &worker, std::size_t tile, TriangleRef *triangle)
        {
            if (worker.tileScatter[tile].count == 0)
                worker.touchedTileIds.push_back(tile);
            worker.tileReferences.push_back({tile, triangle});
            ++worker.tileScatter[tile].count;
        };
        // 같은 tile 수라도 tilesX가 바뀌며, 빈 tile을 사이에 둔 prefix 구간을 검사한다.
        for (const auto size : {std::array{16, 16}, std::array{32, 64}, std::array{64, 32},
                                std::array{80, 48}, std::array{16, 16}})
        {
            renderer.ensureViewport(size[0], size[1]);
            const auto tileCount = static_cast<std::size_t>((size[0] - 1) / tileSize + 1) *
                                   static_cast<std::size_t>((size[1] - 1) / tileSize + 1);
            for (auto &worker : workers)
            {
                worker.BeginFrame();
                worker.tileScatter.resize(tileCount);
            }
            const std::array<std::size_t, 7> expectedTiles{0,
                                                           std::min(std::size_t{3}, tileCount - 1),
                                                           0,
                                                           tileCount - 1,
                                                           0,
                                                           tileCount - 1,
                                                           std::min(std::size_t{3}, tileCount - 1)};
            for (std::size_t i = 0; i < triangles.size(); ++i)
                append(workers[i < 4 ? 0 : 1], expectedTiles[i], &triangles[i]);
            renderer.m_activeTileWorkers = {&workers[0], &workers[1]};
            renderer.prepareTileReferences();
            check(renderer.m_tileReferenceCount == triangles.size() &&
                      workers[0].sortedTileReferences.size() == 4 && workers[1].sortedTileReferences.size() == 3,
                  "per-worker tile sort total equals worker reference cardinality");
            std::array<bool, 7> seen{};
            std::vector<TriangleRef *> gathered;
            for (std::size_t tile = 0; tile < tileCount; ++tile)
            {
                renderer.gatherTileReferences(tile, gathered);
                const auto expectedCount = static_cast<std::size_t>(std::ranges::count(expectedTiles, tile));
                check(gathered.size() == expectedCount, "gathered tile ranges include empty tiles");
                std::vector<TriangleRef *> expectedOrder;
                for (const auto &worker : workers)
                    for (const auto &reference : worker.tileReferences)
                        if (reference.tileIndex == tile)
                            expectedOrder.push_back(reference.triangle);
                for (std::size_t index = 0; index < gathered.size(); ++index)
                {
                    const auto *triangle = gathered[index];
                    check(triangle == expectedOrder[index],
                          "scatter preserves each worker's original reference order inside its range");
                    // 역참조 전에 알려진 pointer 집합인지 확인해 미기록 scatter 슬롯도 검출한다.
                    const auto found = std::ranges::find_if(triangles, [&](const auto &value)
                                                            { return &value == triangle; });
                    check(found != triangles.end(), "scatter writes every destination slot");
                    const auto id = static_cast<std::size_t>(found - triangles.begin());
                    check(!seen[id] && expectedTiles[id] == tile,
                          "scatter writes each worker reference exactly once into its tile range");
                    seen[id] = true;
                }
            }
            check(std::ranges::all_of(seen, [](bool value) { return value; }), "scatter loses no references");
            for (auto &worker : workers)
            {
                worker.BeginFrame();
                check(worker.tileReferences.empty() && worker.touchedTileIds.empty(),
                      "worker frame reset removes flat references and touched IDs");
                check(std::ranges::all_of(worker.tileScatter,
                                          [](const auto &state) { return state.count == 0; }),
                      "touched reset clears all previously counted tile references");
            }
            renderer.m_activeTileWorkers.clear();
            renderer.prepareTileReferences();
            check(renderer.m_tileReferenceCount == 0,
                  "empty prefix scatter discards stale ranges after populated frame");
        }

        // 거대한 실제 할당 없이 count 메타데이터만 과대하게 만들어 checked prefix를 통과시킨다.
        renderer.ensureViewport(16, 16);
        workers[0].tileScatter.resize(1);
        workers[0].tileScatter[0].count = workers[0].sortedTileReferences.max_size() + 1;
        workers[0].touchedTileIds = {0};
        renderer.m_activeTileWorkers = {&workers[0]};
        bool overflowRejected = false;
        try
        {
            renderer.prepareTileReferences();
        }
        catch (const std::length_error &)
        {
            overflowRejected = true;
        }
        check(overflowRejected, "flat tile prefix rejects impossible count before allocation");
        workers[0].BeginFrame();
        renderer.m_activeTileWorkers.clear();
        renderer.prepareTileReferences();
        check(renderer.m_tileReferenceCount == 0, "empty preparation recovers after rejected prefix metadata");

        // 실제 ETS 소유 배열의 capacity 하한을 더한다. allocator/RSS나 worker 수는 고정하지 않는다.
        Renderer measured;
        measured.ensureViewport(32, 32);
        auto &scratch = measured.m_workerScratch.Local();
        scratch.tileScatter.resize(4);
        append(scratch, 0, &triangles[0]);
        append(scratch, 3, &triangles[1]);
        measured.collectActiveTileWorkers();
        measured.prepareTileReferences();
        const auto minimumBytes =
            scratch.sortedTileReferences.capacity() * sizeof(TriangleRef *) +
            measured.m_activeTileWorkers.capacity() * sizeof(Renderer::WorkerScratch *) +
            scratch.tileReferences.capacity() * sizeof(decltype(scratch.tileReferences)::value_type) +
            scratch.tileScatter.capacity() * sizeof(decltype(scratch.tileScatter)::value_type) +
            scratch.touchedTileIds.capacity() * sizeof(std::size_t);
        measured.updateRetainedBinBytes();
        check(measured.GetStats().retainedBinBytes >= minimumBytes,
              "retained bins include flat references prefix offsets and worker scatter storage");
        const auto checkRetainedGrowth = [&](auto &storage)
        {
            const auto beforeCapacity = storage.capacity();
            const auto beforeBytes = measured.GetStats().retainedBinBytes;
            storage.reserve(beforeCapacity + 127);
            measured.updateRetainedBinBytes();
            const auto requiredGrowth = (storage.capacity() - beforeCapacity) * sizeof(storage[0]);
            check(measured.GetStats().retainedBinBytes >= beforeBytes &&
                      measured.GetStats().retainedBinBytes - beforeBytes >= requiredGrowth,
                  "each flat bin allocation contributes its retained capacity independently");
        };
        checkRetainedGrowth(scratch.sortedTileReferences);
        checkRetainedGrowth(measured.m_tileGather.Local());
        checkRetainedGrowth(measured.m_activeTileWorkers);
        checkRetainedGrowth(scratch.tileReferences);
        checkRetainedGrowth(scratch.tileScatter);
        checkRetainedGrowth(scratch.touchedTileIds);
        const auto retainedBeforeEmpty = measured.GetStats().retainedBinBytes;
        measured.prepareFrame();
        measured.collectActiveTileWorkers();
        measured.prepareTileReferences();
        measured.updateRetainedBinBytes();
        check(measured.GetStats().retainedBinBytes >= retainedBeforeEmpty,
              "retained bin statistics keep inactive worker storage after empty frame");
    }
    static void optimizedRasterPaths()
    {
        MemorySurface memory(256, 256);
        std::mt19937 random(735);
        std::uniform_int_distribution<int> coordinate(0, 255);
        std::uniform_int_distribution<int> outsideCoordinate(-100, 350);
        const RenderFrameContext lineFrame{SRMath::mat4::identity(), {}, {}, 16, 16};
        // 256의 이진 배수 좌표는 투영 왕복이 정확하다. clipping으로 시작점이
        // 달라지지 않는 8방향/한 점 선분에서 기존 Bresenham 규칙을 독립 비교한다.
        // 후반 400개는 화면 밖 정수 끝점을 저수준 경로에 직접 넣어 기존
        // 경계 검사와 Bresenham 위상까지 그대로 유지하는지 확인한다.
        for (int iteration = 0; iteration < 800; ++iteration)
        {
            const bool direct = iteration >= 400;
            auto &distribution = direct ? outsideCoordinate : coordinate;
            int x = distribution(random), y = distribution(random);
            const int endX = iteration % 400 == 0 ? x : distribution(random);
            const int endY = iteration % 400 == 0 ? y : distribution(random);
            const auto mask = random() & 7u;
            const SRMath::vec4 color{(mask & 1u) ? 1.f : 0.f, (mask & 2u) ? 1.f : 0.f,
                                     (mask & 4u) ? 1.f : 0.f, 1.f};
            const unsigned int pixel = ((mask & 1u) ? 0xFF0000u : 0u) | ((mask & 2u) ? 0x00FF00u : 0u) |
                                       ((mask & 4u) ? 0x0000FFu : 0u);
            DebugPrimitiveCommand command;
            command.worldTransform = SRMath::mat4::identity();
            command.vertices = {{{x / 128.f - 1.f, 1.f - y / 128.f, 0.f}, color},
                                {{endX / 128.f - 1.f, 1.f - endY / 128.f, 0.f}, color}};
            memory.target.Clear();
            Rasterizer rasterizer(memory.target, lineFrame);
            if (direct)
                rasterizer.DrawScreenLine({static_cast<float>(x), static_cast<float>(y)},
                                          {static_cast<float>(endX), static_cast<float>(endY)}, 1.f, 1.f,
                                          pixel, ELineAlgorithm::Bresenham, false);
            else
                rasterizer.DrawDebugPrimitive(command, ELineAlgorithm::Bresenham);
            const auto optimized = memory.pixels;
            memory.target.Clear();
            const int dx = std::abs(endX - x), dy = -std::abs(endY - y);
            const int sx = x < endX ? 1 : -1, sy = y < endY ? 1 : -1;
            int error = dx + dy;
            for (;;)
            {
                if (x >= 0 && x < 256 && y >= 0 && y < 256)
                    memory.pixels[static_cast<std::size_t>(y) * 256 + x] = pixel;
                if (x == endX && y == endY)
                    break;
                const int twiceError = 2 * error;
                if (twiceError >= dy)
                {
                    error += dy;
                    x += sx;
                }
                if (twiceError <= dx)
                {
                    error += dx;
                    y += sy;
                }
            }
            check(optimized == memory.pixels, "Bresenham pixel equivalence");
        }
        // 준비 결과를 재사용하는 bin 경로와 매 타일 별도로 투영/edge/coverage를
        // 계산하는 기준 경로를 비교한다. 기준은 Prepare/DrawTriangle을 호출하지 않는다.
        for (const auto &dimensions : {std::pair{160, 128}, std::pair{137, 93}})
        {
            const auto [width, height] = dimensions;
            memory.Resize(width, height);
            MemorySurface reference(width, height);
            Renderer renderer;
            renderer.ensureViewport(width, height);
            Material material;
            material.ambient = {0.3f, 0.5f, 0.7f};
            MeshRenderCommand command{};
            command.material = &material;
            command.rasterizeMode = ERasterizeMode::Fill;
            Renderer::FrameContext frame{SRMath::mat4::identity(),
                                         {0.f, 0.f, -5.f},
                                         {},
                                         (width + tileSize - 1) / tileSize,
                                         (height + tileSize - 1) / tileSize};
            std::uniform_real_distribution<float> clip(-1.f, 1.f);
            std::uniform_real_distribution<float> inverseDepth(0.3f, 3.f);
            for (int iteration = 0; iteration < 100; ++iteration)
            {
                ShadedVertex a{}, b{}, c{};
                for (auto *vertex : {&a, &b, &c})
                {
                    const float w = inverseDepth(random);
                    vertex->posClip = {clip(random) * w, clip(random) * w, 0.f, w};
                    vertex->normalWorld = {0.f, 1.f, 0.f};
                }
                if (iteration == 0)
                {
                    a.posClip = {-1.f, -1.f, 0.f, 1.f};
                    b.posClip = {0.f, 1.f, 0.f, 1.f};
                    c.posClip = {1.f, -1.f, 0.f, 1.f};
                }
                else if (iteration == 1)
                {
                    a.posClip = b.posClip = c.posClip = {0.f, 0.f, 0.f, 1.f};
                }
                memory.target.Clear();
                reference.target.Clear();
                renderer.prepareFrame();
                renderer.binTriangle(command, a, b, c, frame, renderer.m_workerScratch.Local());
                const auto &scratch = renderer.m_workerScratch.Local();
                for (std::size_t tile = 0; tile < scratch.tileScatter.size(); ++tile)
                    check(scratch.tileScatter[tile].count ==
                              static_cast<std::size_t>(
                                  std::ranges::count_if(scratch.tileReferences, [&](const auto &reference)
                                                        { return reference.tileIndex == tile; })),
                          "frontend flat tile count matches its actual reference records");
                renderer.collectActiveTileWorkers();
                renderer.renderTiles(memory.target, frame, false);
                for (int ty = 0; ty < frame.tilesY; ++ty)
                    for (int tx = 0; tx < frame.tilesX; ++tx)
                        referenceTriangle(reference.target, a, b, c, material, frame,
                                          {tx * tileSize, ty * tileSize, std::min((tx + 1) * tileSize, width),
                                           std::min((ty + 1) * tileSize, height)});
                check(memory.pixels == reference.pixels, "independent prepared pixel equivalence");
                check(std::ranges::equal(memory.target.PrimitiveIds(), reference.target.PrimitiveIds()),
                      "independent prepared owner equivalence");
                check(std::ranges::equal(memory.target.Depth(), reference.target.Depth(),
                                         [](float left, float right)
                                         {
                                             return std::bit_cast<std::uint32_t>(left) ==
                                                    std::bit_cast<std::uint32_t>(right);
                                         }),
                      "independent prepared bitwise depth equivalence");
            }
        }
    }
    static void referenceTriangle(RenderTarget &target, const ShadedVertex &a, const ShadedVertex &b,
                                  const ShadedVertex &c, const Material &material,
                                  const RenderFrameContext &frame, const TileBounds &bounds)
    {
        const auto project = [&](const ShadedVertex &source)
        {
            RasterizerVertex result;
            const float oneOverW = 1.f / source.posClip.w;
            const auto ndc = SRMath::vec3(source.posClip) * oneOverW;
            result.screenPos = {(ndc.x + 1.f) * 0.5f * target.Width(),
                                (1.f - ndc.y) * 0.5f * target.Height()};
            result.oneOverW = oneOverW;
            result.normalWorldOverW = source.normalWorld * oneOverW;
            result.texcoordOverW = source.texcoord * oneOverW;
            result.worldPosOverW = source.posWorld * oneOverW;
            return result;
        };
        const auto v0 = project(a), v1 = project(b), v2 = project(c);
        const auto p0 = v0.screenPos, p1 = v1.screenPos, p2 = v2.screenPos;
        const int minX = std::max(bounds.minX, static_cast<int>(std::min({p0.x, p1.x, p2.x})));
        const int maxX = std::min(bounds.maxX - 1, static_cast<int>(std::max({p0.x, p1.x, p2.x})));
        const int minY = std::max(bounds.minY, static_cast<int>(std::min({p0.y, p1.y, p2.y})));
        const int maxY = std::min(bounds.maxY - 1, static_cast<int>(std::max({p0.y, p1.y, p2.y})));
        const float dx01 = p0.x - p1.x, dy01 = p0.y - p1.y;
        const float dx12 = p1.x - p2.x, dy12 = p1.y - p2.y;
        const float dx20 = p2.x - p0.x, dy20 = p2.y - p0.y;
        const auto fixed = [](float value) { return static_cast<std::int64_t>(value * 256.f); };
        auto row0 = fixed(dy12 * (minX + 0.5f - p1.x) - dx12 * (minY + 0.5f - p1.y));
        auto row1 = fixed(dy20 * (minX + 0.5f - p2.x) - dx20 * (minY + 0.5f - p2.y));
        auto row2 = fixed(dy01 * (minX + 0.5f - p0.x) - dx01 * (minY + 0.5f - p0.y));
        const auto surface = target.Surface();
        for (int y = minY; y <= maxY; ++y)
        {
            auto edge0 = row0, edge1 = row1, edge2 = row2;
            for (int x = minX; x <= maxX; ++x)
            {
                if ((edge0 | edge1 | edge2) >= 0)
                {
                    const float w0 = static_cast<float>(edge0) / 256.f;
                    const float w1 = static_cast<float>(edge1) / 256.f;
                    const float w2 = static_cast<float>(edge2) / 256.f;
                    const float totalWeight = (w0 + w1 + w2);
                    // 이전 continue는 x-edge 증가까지 건너뛰었다. 퇴화 sample만
                    // 건너뛰고 다음 픽셀로 진행하는 별도 버그 수정 기준을 적용한다.
                    if (std::abs(totalWeight) >= 1e-5f)
                    {
                        const float inverseTotalWeight = 1.f / totalWeight;
                        const float wBary = w0 * inverseTotalWeight;
                        const float uBary = w1 * inverseTotalWeight;
                        const float vBary = w2 * inverseTotalWeight;
                        float depth = v0.oneOverW * wBary;
                        depth += v1.oneOverW * uBary;
                        depth += v2.oneOverW * vBary;
                        const auto index = static_cast<std::size_t>(y) * target.Width() + x;
                        if (std::isfinite(depth) && depth > 0.f && depth > target.Depth()[index])
                        {
                            const auto sample =
                                Rasterizer::InterpolateSurface(v0, v1, v2, wBary, uBary, vBary, 1.f / depth);
                            const auto color = Rasterizer::ShadeSurface(sample, material, frame);
                            const auto red = static_cast<unsigned int>(color.x * 255.f);
                            const auto green = static_cast<unsigned int>(color.y * 255.f);
                            const auto blue = static_cast<unsigned int>(color.z * 255.f);
                            surface.pixels[static_cast<std::size_t>(y) * surface.stridePixels + x] =
                                (red << 16) | (green << 8) | blue;
                            target.Depth()[index] = depth;
                            target.PrimitiveIds()[index] = 0;
                        }
                    }
                }
                edge0 += fixed(dy12);
                edge1 += fixed(dy20);
                edge2 += fixed(dy01);
            }
            row0 -= fixed(dx12);
            row1 -= fixed(dx20);
            row2 -= fixed(dx01);
        }
    }
    static SRMath::vec3 originalPhong(const Rasterizer::SurfaceSample &surface, const Material &material,
                                      const SRMath::vec3 &camera, std::span<const DirectionalLight> lights)
    {
        // 이전 픽셀별 구현을 독립 기준으로 보존한다. From/prepareLights를 호출하지 않으며
        // 매 light의 부호 반전→normalize 및 diffuse/specular 누적 순서도 그대로다.
        SRMath::vec3 baseColor;
        if (material.diffuseTexture)
        {
            baseColor = material.diffuseTexture->Sample(surface.texcoord.x, surface.texcoord.y);
            baseColor *= material.diffuse;
        }
        else
            baseColor = material.diffuse;
        SRMath::vec3 ambientColor = material.ambient;
        SRMath::vec3 totalDiffuseColor{}, totalSpecularColor{};
        SRMath::vec3 viewDir = SRMath::normalize(camera - surface.positionWorld);
        for (const auto &light : lights)
        {
            const SRMath::vec3 lightDir = SRMath::normalize(-1.0f * light.rayDirection);
            const float diffuseIntensity = std::max(0.0f, SRMath::dot(surface.normalWorld, lightDir));
            SRMath::vec3 diffuseTerm = baseColor;
            diffuseTerm *= diffuseIntensity;
            diffuseTerm *= light.color;
            totalDiffuseColor += diffuseTerm;
            const SRMath::vec3 reflectDirection = SRMath::reflect(-1.0f * lightDir, surface.normalWorld);
            const float specularFactor =
                diffuseIntensity > 0.0f ? std::pow(std::max(0.0f, SRMath::dot(viewDir, reflectDirection)),
                                                   std::max(1.0f, material.shininess))
                                        : 0.0f;
            SRMath::vec3 specularTerm = material.specular;
            specularTerm *= specularFactor;
            specularTerm *= light.color;
            totalSpecularColor += specularTerm;
        }
        SRMath::vec3 color = ambientColor;
        color += totalDiffuseColor;
        color += totalSpecularColor;
        color.clamp(0.f, 1.0f);
        return color;
    }
    static void phongLighting(const std::filesystem::path &root)
    {
        Material material;
        material.ambient = {0.f, 0.f, 0.f};
        material.diffuse = {1.f, 1.f, 1.f};
        material.specular = {1.f, 1.f, 1.f};
        const std::array<DirectionalLight, 1> lights{DirectionalLight{{0.f, -1.f, 0.f}, {1.f, 1.f, 1.f}}};
        const std::array prepared{PreparedDirectionalLight::From(lights[0])};
        const Renderer::FrameContext frame{SRMath::mat4::identity(), {0.f, 3.f, 0.f}, prepared, 1, 1};
        const auto lit =
            Rasterizer::ShadeSurface({{0.f, 1.f, 0.f}, {0.f, 0.f}, {0.f, 0.f, 0.f}}, material, frame);
        const auto dark =
            Rasterizer::ShadeSurface({{0.f, -1.f, 0.f}, {0.f, 0.f}, {0.f, 0.f, 0.f}}, material, frame);
        check(lit.x > 0.9f && approximatelyEqual(dark.x, 0.f), "downward light illuminates upward normal");

        const auto sameFloat = [](float a, float b)
        {
            return std::isnan(a) ? std::isnan(b)
                                 : std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
        };
        const auto sameColor = [&](const SRMath::vec3 &a, const SRMath::vec3 &b)
        { return sameFloat(a.x, b.x) && sameFloat(a.y, b.y) && sameFloat(a.z, b.z); };
        const auto checkPrepared =
            [&](std::span<const PreparedDirectionalLight> actual, std::span<const DirectionalLight> raw)
        {
            check(actual.size() == raw.size(), "prepared light count matches the current input span");
            for (std::size_t i = 0; i < raw.size(); ++i)
                check(sameColor(actual[i].directionToLight, SRMath::normalize(-1.0f * raw[i].rayDirection)) &&
                          sameColor(actual[i].color, raw[i].color),
                      "prepared light preserves original normalization bits and NaN classification");
        };
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float infinity = std::numeric_limits<float>::infinity();
        const float below = std::nextafter(1e-5f, 0.f), above = std::nextafter(1e-5f, infinity);
        const std::array<SRMath::vec3, 13> directions{{{0.f, 0.f, 0.f},
                                                       {-0.f, 0.f, -0.f},
                                                       {below, 0.f, 0.f},
                                                       {1e-5f, 0.f, 0.f},
                                                       {above, 0.f, 0.f},
                                                       {std::numeric_limits<float>::denorm_min(), 0.f, 0.f},
                                                       {2.f, -3.f, 4.f},
                                                       {0.f, -7.f, 0.f},
                                                       {nan, 1.f, 0.f},
                                                       {infinity, -1.f, 0.f},
                                                       {-infinity, 0.f, 1.f},
                                                       {std::numeric_limits<float>::max(), 1.f, -2.f},
                                                       {-1e-6f, 2e-6f, -3e-6f}}};
        const std::array<SRMath::vec3, 3> lightColors{
            {{.13f, .37f, .71f}, {nan, .5f, infinity}, {-1.f, 2.f, .25f}}};
        const auto texture = TextureLoader::LoadImageFile(root / "assets/teapot.png");
        check(texture.has_value(), "prepared Phong texture fixture loads");
        std::array<Material, 4> materials;
        for (std::size_t i = 0; i < materials.size(); ++i)
        {
            materials[i].ambient = {.015f, .025f, .035f};
            materials[i].diffuse = {.2f, .3f, .4f};
            materials[i].specular = {.11f, .07f, .03f};
            materials[i].shininess = std::array{-2.f, 1.f, 19.f, nan}[i];
        }
        materials[2].diffuseTexture = *texture;
        const std::array<Rasterizer::SurfaceSample, 4> surfaces{
            {{{0.f, 1.f, 0.f}, {.25f, .75f}, {0.f, 0.f, 0.f}},
             {SRMath::normalize(SRMath::vec3{1.f, 2.f, 3.f}), {0.f, 1.f}, {.2f, -.3f, .4f}},
             {{0.f, 0.f, 0.f}, {1.f, 0.f}, {0.f, 0.f, 0.f}},
             {{0.f, -1.f, 0.f}, {-.25f, 1.25f}, {0.f, 1.f, 0.f}}}};
        const std::array<SRMath::vec3, 3> cameras{{{0.f, 3.f, 0.f}, {0.f, 0.f, 0.f}, {2.f, -1.f, 4.f}}};
        Renderer preparer;
        const auto compareShader = [&](std::span<const DirectionalLight> raw)
        {
            const auto ready = preparer.prepareLights(raw);
            checkPrepared(ready, raw);
            for (const auto &sample : surfaces)
                for (const auto &testMaterial : materials)
                    for (const auto &camera : cameras)
                    {
                        const Renderer::FrameContext context{SRMath::mat4::identity(), camera, ready, 1, 1};
                        check(sameColor(Rasterizer::ShadeSurface(sample, testMaterial, context),
                                        originalPhong(sample, testMaterial, camera, raw)),
                              "prepared Phong matches independent per-pixel shader bitwise");
                    }
            // 4-lane 셰이더: 표면·재질 조합을 lane마다 다르게(같은 재질 broadcast 경로 포함) 넣어 lane별로 비교한다.
            for (const auto &camera : cameras)
                for (std::size_t rotation = 0; rotation < 4; ++rotation)
                    for (const bool sharedMaterial : {false, true})
                    {
                        const Renderer::FrameContext context{SRMath::mat4::identity(), camera, ready, 1, 1};
                        Rasterizer::SurfaceSample lanes[4];
                        const Material *laneMaterials[4];
                        for (std::size_t i = 0; i < 4; ++i)
                        {
                            lanes[i] = surfaces[(i + rotation) % 4];
                            laneMaterials[i] = &materials[sharedMaterial ? rotation : (i * 3 + rotation) % 4];
                        }
                        SRMath::vec3 colors[4];
                        Rasterizer::ShadeSurface4(lanes, laneMaterials, context, colors);
                        for (std::size_t i = 0; i < 4; ++i)
                            check(sameColor(colors[i], Rasterizer::ShadeSurface(lanes[i], *laneMaterials[i], context)),
                                  "4-lane Phong matches the scalar shader bitwise in every lane");
                    }
        };
        compareShader({});
        for (const auto &direction : directions)
            for (const auto &color : lightColors)
            {
                const std::array raw{DirectionalLight{direction, color}};
                compareShader(raw);
            }
        std::array<DirectionalLight, 3> mutableLights{{{{0.f, -2.f, -1.f}, {.13f, .21f, .34f}},
                                                       {{2.f, -3.f, 1.f}, {.01f, .02f, .04f}},
                                                       {{-1.f, -.5f, -4.f}, {.31f, .23f, .17f}}}};
        compareShader(mutableLights);
        std::ranges::reverse(mutableLights);
        compareShader(mutableLights); // 순서를 바꿔도 이전 구현과 같은 순서로 누적해야 한다.

        Renderer other;
        const auto ready = preparer.prepareLights(mutableLights);
        const auto *storage = ready.data();
        const auto capacity = preparer.m_preparedLights.capacity();
        const auto otherReady = other.prepareLights(mutableLights);
        const std::vector<PreparedDirectionalLight> savedOther(otherReady.begin(), otherReady.end());
        const auto *rawAddress = mutableLights.data();
        mutableLights[0].rayDirection = {4.f, -1.f, 2.f};
        mutableLights[0].color = {.91f, .19f, .47f};
        checkPrepared(preparer.prepareLights(mutableLights), mutableLights);
        check(mutableLights.data() == rawAddress && preparer.m_preparedLights.data() == storage &&
                  preparer.m_preparedLights.capacity() == capacity,
              "same-address light mutation refreshes values without reallocating capacity");
        for (std::size_t i = 0; i < savedOther.size(); ++i)
            check(sameColor(other.m_preparedLights[i].directionToLight, savedOther[i].directionToLight) &&
                      sameColor(other.m_preparedLights[i].color, savedOther[i].color),
                  "renderer prepared light storage is independent");
        check(preparer.prepareLights({}).empty() && preparer.m_preparedLights.capacity() == capacity,
              "N to zero lights clears live values but retains capacity");
        checkPrepared(preparer.prepareLights(mutableLights), mutableLights);
        check(preparer.m_preparedLights.data() == storage, "zero to N lights reuses owned storage");

        // 실제 프레임에서도 같은 원본 주소를 갱신한다. 준비의 스레드/시점은 이동하므로
        // 여기서는 전체 MXCSR/trap 동등성을 주장하거나 process FP 모드를 바꾸지 않는다.
        Mesh mesh;
        mesh.vertices = {{{-1.f, -1.f, 0.f}, {}, {0.f, 0.f, 1.f}},
                         {{1.f, -1.f, 0.f}, {}, {0.f, 0.f, 1.f}},
                         {{1.f, 1.f, 0.f}, {}, {0.f, 0.f, 1.f}},
                         {{-1.f, 1.f, 0.f}, {}, {0.f, 0.f, 1.f}}};
        mesh.indices = {0, 2, 1, 0, 3, 2};
        mesh.material.ambient = {.01f, .02f, .03f};
        mesh.material.diffuse = {.5f, .4f, .3f};
        mesh.material.specular = {};
        Camera camera({0.f, 0.f, 5.f});
        camera.SetCameraYaw(std::numbers::pi_v<float>);
        std::array<bool, 256> keys{};
        camera.Update(0.f, keys, 1.f);
        Renderer reused;
        MemorySurface actual(32, 32), expected(32, 32);
        std::vector<unsigned int> firstColors;
        const std::array<std::size_t, 6> counts{3, 0, 3, 1, 3, 3};
        for (std::size_t iteration = 0; iteration < counts.size(); ++iteration)
        {
            mutableLights[0].rayDirection = {static_cast<float>(iteration) * .3f, 0.f, -2.f};
            mutableLights[0].color = {.2f + static_cast<float>(iteration) * .05f, .3f, .4f};
            const auto input = std::span<const DirectionalLight>(mutableLights).first(counts[iteration]);
            RenderQueue queue;
            queue.Submit(
                MeshRenderCommand{&mesh, mesh.indices, SRMath::mat4::identity(), &mesh.material,
                                  iteration == 3 ? ERasterizeMode::Wireframe : ERasterizeMode::Fill});
            if (iteration == 4)
            {
                RenderTarget empty;
                reused.RenderScene(empty, queue, camera.GetViewParams(), input);
                check(empty.Empty(), "empty target light preparation remains a no-op render");
            }
            actual.target.Clear();
            expected.target.Clear();
            reused.RenderScene(actual.target, queue, camera.GetViewParams(), input);
            Renderer fresh;
            fresh.RenderScene(expected.target, queue, camera.GetViewParams(), input);
            check(actual.pixels == expected.pixels &&
                      std::ranges::equal(actual.target.Depth(), expected.target.Depth()) &&
                      std::ranges::equal(actual.target.PrimitiveIds(), expected.target.PrimitiveIds()),
                  "light refresh across N zero wire and empty transitions matches a fresh renderer");
            checkPrepared(reused.m_preparedLights, input);
            if (iteration == 0)
                firstColors = actual.pixels;
            if (iteration == 1)
                check(actual.pixels != firstColors, "zero-light frame actually changes the lit surface");
        }
    }
    static void debugColors()
    {
        MemorySurface memory;
        const RenderFrameContext frame{SRMath::mat4::identity(), {}, {}, 10, 8};
        Rasterizer rasterizer(memory.target, frame);
        DebugPrimitiveCommand command;
        command.worldTransform = SRMath::mat4::identity();
        command.type = DebugPrimitiveType::Line;
        command.vertices = {{{-0.5f, 0.f, 0.f}, {1.f, 0.f, 0.f, 1.f}},
                            {{-0.25f, 0.f, 0.f}, {1.f, 0.f, 0.f, 1.f}},
                            {{0.25f, 0.f, 0.f}, {0.f, 1.f, 0.f, 1.f}},
                            {{0.5f, 0.f, 0.f}, {0.f, 1.f, 0.f, 1.f}}};
        rasterizer.DrawDebugPrimitive(command, ELineAlgorithm::Bresenham);
        const auto &image = memory.pixels;
        check(image[60 * 160 + 45] == 0xFF0000, "first segment red");
        check(image[60 * 160 + 105] == 0x00FF00, "second segment green");
    }
};


namespace
{
void fxaaTests()
{
    const std::array<unsigned int, 4> small{0xFF0000, 0x00FF00, 0x0000FF, 0xFFFFFF};
    std::array<unsigned int, 4> output{};
    sr::fxaa::Apply(small, output, 2, 2);
    check(output == small, "FXAA tiny image copy");
    sr::fxaa::Apply({}, {}, 0, 0);
    std::vector<unsigned int> input(64, 0x808080), filtered(64);
    sr::fxaa::Apply(input, filtered, 8, 8);
    check(input == filtered, "FXAA uniform field");
    std::array<unsigned int, 1> tooSmall{123};
    sr::fxaa::Apply(input, tooSmall, 8, 8);
    check(tooSmall[0] == 123, "FXAA size mismatch does not write");
}
void renderSmoke(const TestContext &context)
{
    const auto &root = context.root;
    MemorySurface memory;
    Renderer renderer;
    std::array<bool, 256> keys{};
    // fixture의 바깥 법선은 +Z다. +Z 쪽에서 바라봐야 뒷면 컬링 후에도 실제 픽셀이 남는다.
    Camera camera({0.f, 1.f, 8.f});
    camera.SetCameraYaw(std::numbers::pi_v<float>);
    camera.Update(0.f, keys, 160.f / 120.f);
    const std::array<DirectionalLight, 1> lights{DirectionalLight{{0.f, -1.f, 0.f}, {1.f, 1.f, 1.f}}};
    auto triangle = loadObject(root / "tests/fixtures/triangle.obj", {0.f, 0.f, 0.f});
    const auto draw =
        [&](const std::shared_ptr<GameObject> &object, const DebugFlags &flags, bool gizmos = true)
    {
        RenderQueue queue;
        SceneRenderCollector().Collect(*object, camera.GetFrustum(), flags, queue);
        if (gizmos)
            sr::debug::SubmitDirectionalLightGizmos(queue, lights);
        memory.target.Clear();
        renderer.RenderScene(memory.target, queue, camera.GetViewParams(), lights);
        return memory.pixels;
    };
    for (bool wireframe : {false, true})
    {
        DebugFlags flags{};
        flags.bShowWireframe = wireframe;
        std::vector<unsigned int> serial;
        {
            sr::jobs::ConcurrencyLimit single(1);
            serial = draw(triangle, flags, false);
            for (int iteration = 1; iteration < 100; ++iteration)
                check(serial == draw(triangle, flags, false), "single-worker repeated pixel equality");
        }
        check(std::ranges::any_of(serial, [](auto pixel) { return pixel != 0; }), "nonempty render");
        for (int iteration = 0; iteration < 100; ++iteration)
            check(serial == draw(triangle, flags, false), "single/multi worker pixel equality");
    }
    for (const auto name : {"IronMan", "teapot"})
    {
        auto object = loadObject(root / "assets" / name, {0.f, 0.f, 0.f}, 0.04f);
        // 실제 다중 메시/옥트리 입력의 큐 순서가 worker 등록 순서에 의존하지 않는다.
        const auto collect = [&]
        {
            RenderQueue queue;
            SceneRenderCollector().Collect(*object, camera.GetFrustum(), DebugFlags{}, queue);
            return std::vector<MeshRenderCommand>{queue.GetRenderCommands().begin(),
                                                  queue.GetRenderCommands().end()};
        };
        std::vector<MeshRenderCommand> stableCommands;
        {
            sr::jobs::ConcurrencyLimit single(1);
            stableCommands = collect();
        }
        check(!stableCommands.empty(), "asset command submission");
        for (int iteration = 0; iteration < 100; ++iteration)
        {
            const auto commands = collect();
            check(commands.size() == stableCommands.size(), "stable command count");
            for (std::size_t index = 0; index < commands.size(); ++index)
                check(commands[index].sourceMesh == stableCommands[index].sourceMesh &&
                          commands[index].indicesToDraw.data() ==
                              stableCommands[index].indicesToDraw.data() &&
                          commands[index].indicesToDraw.size() == stableCommands[index].indicesToDraw.size(),
                      "stable per-mesh and octree command order");
        }
        const auto start = std::chrono::steady_clock::now();
        for (const auto aa : {EAAAlgorithm::None, EAAAlgorithm::FXAA})
        {
            renderer.SetAAAlgorithm(aa);
            for (int mode = 0; mode < 4; ++mode)
            {
                DebugFlags flags{};
                flags.bShowWireframe = mode == 1;
                flags.bShowNormal = mode == 2;
                flags.bShowAABB = mode == 3;
                object->Update(0.016f, true);
                const auto pixels = draw(object, flags, false);
                check(std::ranges::any_of(pixels, [](auto pixel) { return pixel != 0; }),
                      "asset smoke output");
            }
        }
        const double milliseconds =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        std::cout << name << " smoke (8 frames, 160x120): " << milliseconds << " ms\n";
    }
    for (const auto size : {0, 64, 160})
    {
        memory.Resize(size, size);
        memory.target.Clear();
        renderer.RenderScene(memory.target, RenderQueue{}, camera.GetViewParams(), lights);
        check(memory.target.Width() == size, "resize dimensions");
    }
}
// 앱/srcli는 ID 면 없는 타깃(Attach(..., false))에 그린다. 색과 깊이, 채운 픽셀 수가 ID 면이 있는 타깃과 같아야 한다.
void primitiveIdPlaneOptional(const TestContext &context)
{
    constexpr int width = 200, height = 150; // 16의 배수가 아닌 크기로 가장자리 타일도 확인한다.
    std::vector<unsigned int> withPixels(width * height), withoutPixels(width * height);
    RenderTarget with, without;
    check(with.Attach({withPixels, width, height, width}).has_value() &&
              without.Attach({withoutPixels, width, height, width}, false).has_value(),
          "id plane optional attachment");
    check(with.HasPrimitiveIds() && !without.HasPrimitiveIds() && without.PrimitiveIds().empty() &&
              without.Depth().size() == withPixels.size(),
          "target without id plane keeps depth only");
    std::array<bool, 256> keys{};
    Camera camera({0.f, 1.f, 8.f});
    camera.SetCameraYaw(std::numbers::pi_v<float>);
    camera.Update(0.f, keys, static_cast<float>(width) / height);
    const std::array<DirectionalLight, 1> lights{DirectionalLight{{0.f, -1.f, 0.f}, {1.f, 1.f, 1.f}}};
    auto object = loadObject(context.root / "assets" / "IronMan", {0.f, 0.f, 0.f}, 0.04f);
    object->Update(0.016f, true);
    for (int variant = 0; variant < 5; ++variant)
        for (int mode = 0; mode < 3; ++mode)
            for (bool clearInRenderer : {false, true})
            {
                DebugFlags flags{};
                flags.bShowWireframe = mode == 1;
                flags.bShowNormal = mode == 2;
                RenderOptions options;
                options.collectStats = true;
                options.hiZ = variant == 1 || variant == 3;
                options.frontToBack = variant == 2 || variant == 3;
                options.hiddenWireframe = variant == 4;
                RenderQueue queue;
                SceneRenderCollector().Collect(*object, camera.GetFrustum(), flags, queue);
                const auto render = [&](RenderTarget &target)
                {
                    Renderer renderer;
                    renderer.SetOptions(options);
                    if (clearInRenderer)
                        renderer.ClearAndRenderScene(target, queue, camera.GetViewParams(), lights);
                    else
                    {
                        target.Clear();
                        renderer.RenderScene(target, queue, camera.GetViewParams(), lights);
                    }
                    return renderer.GetStats().finalFilledPixels;
                };
                const auto filledWith = render(with);
                const auto filledWithout = render(without);
                check(withPixels == withoutPixels && std::ranges::equal(with.Depth(), without.Depth()),
                      "target without id plane renders identical color and depth");
                // X-ray 와이어는 면을 채우지 않는다(0개).
                check(filledWith == filledWithout && (filledWith > 0) == (mode != 1 || variant == 4),
                      "filled pixel count without id plane matches the id count");
            }
}

void textureBilinearAndMipmap()
{
    // 4x4 사분면 텍스처: 좌상 Red, 우상 Green, 좌하 Blue, 우하 White
    std::vector<unsigned char> rgba(4 * 4 * 4, 255);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
        {
            const std::size_t idx = static_cast<std::size_t>(y * 4 + x) * 4;
            const bool right = x >= 2;
            const bool bottom = y >= 2;
            rgba[idx + 0] = (!right && !bottom) || (right && bottom) ? 255 : 0;
            rgba[idx + 1] = (right && !bottom) || (right && bottom) ? 255 : 0;
            rgba[idx + 2] = (!right && bottom) || (right && bottom) ? 255 : 0;
            rgba[idx + 3] = 255;
        }
    auto tex = std::make_shared<Texture>(4, 4, rgba);
    check(!tex->HasMipmaps() && tex->GetMipCount() == 1, "initial texture has 1 mip level");

    const SRMath::vec3 nearestCenter = tex->Sample(0.5f, 0.5f);
    const SRMath::vec3 bilinearCenter = tex->SampleBilinear(0.5f, 0.5f, 0);
    // (0.5, 0.5)는 네 사분면 경계(fx=1.5, fy=1.5)이므로 bilinear는 정확히 (0.5, 0.5, 0.5)다.
    check(approximatelyEqual(bilinearCenter.x, 0.5f) && approximatelyEqual(bilinearCenter.y, 0.5f) &&
              approximatelyEqual(bilinearCenter.z, 0.5f),
          "bilinear center averages four quadrant texels");
    check(!approximatelyEqual(nearestCenter.x, bilinearCenter.x), "nearest differs from bilinear at boundary");

    tex->GenerateMipmaps();
    check(tex->HasMipmaps() && tex->GetMipCount() == 3, "4x4 mipmap chain has 3 levels (4x4, 2x2, 1x1)");
    check(tex->GetMipWidth(1) == 2 && tex->GetMipHeight(1) == 2 && tex->GetMipWidth(2) == 1 &&
              tex->GetMipHeight(2) == 1,
          "mip dimensions halve down to 1x1");
    const SRMath::vec3 topMip = tex->SampleNearest(0.5f, 0.5f, 2);
    check( std::abs(topMip.x - 128.f / 255.f) < 1e-4f && std::abs(topMip.y - 128.f / 255.f) < 1e-4f &&
               std::abs(topMip.z - 128.f / 255.f) < 1e-4f,
           "1x1 mip level is the box-filtered average of all 4 quadrants");

    tex->SetFilter(TextureFilter::Trilinear);
    const SRMath::vec3 trilinear = tex->Sample(0.125f, 0.125f, 1.5f);
    const SRMath::vec3 mip1 = tex->SampleBilinear(0.125f, 0.125f, 1);
    const SRMath::vec3 mip2 = tex->SampleBilinear(0.125f, 0.125f, 2);
    check(approximatelyEqual(trilinear.x, (mip1.x + mip2.x) * 0.5f) &&
              approximatelyEqual(trilinear.y, (mip1.y + mip2.y) * 0.5f) &&
              approximatelyEqual(trilinear.z, (mip1.z + mip2.z) * 0.5f),
          "trilinear interpolates between adjacent mip levels");
    tex->SetFilter(TextureFilter::Nearest);

    Material mat;
    mat.ambient = {0.f, 0.f, 0.f};
    mat.diffuse = {1.f, 1.f, 1.f};
    mat.specular = {0.f, 0.f, 0.f};
    mat.diffuseTexture = tex;
    const std::array<DirectionalLight, 1> lights{DirectionalLight{{0.f, 0.f, -1.f}, {1.f, 1.f, 1.f}}};
    const std::array prepared{PreparedDirectionalLight::From(lights[0])};
    RenderFrameContext frame{SRMath::mat4::identity(), {0.f, 0.f, 3.f}, prepared, 1, 1};
    frame.bilinearFiltering = true;
    frame.mipmapping = true;
    Rasterizer::SurfaceSample sample{{0.f, 0.f, 1.f}, {0.5f, 0.5f}, {0.f, 0.f, 0.f}};
    sample.lod = 1.5f;
    const SRMath::vec3 scalarOut = Rasterizer::ShadeSurface(sample, mat, frame);
    const Rasterizer::SurfaceSample lanes[4] = {sample, sample, sample, sample};
    const Material *mats[4] = {&mat, &mat, &mat, &mat};
    SRMath::vec3 laneOut[4];
    Rasterizer::ShadeSurface4(lanes, mats, frame, laneOut);
    for (int i = 0; i < 4; ++i)
        check(std::bit_cast<std::uint32_t>(scalarOut.x) == std::bit_cast<std::uint32_t>(laneOut[i].x) &&
                  std::bit_cast<std::uint32_t>(scalarOut.y) == std::bit_cast<std::uint32_t>(laneOut[i].y) &&
                  std::bit_cast<std::uint32_t>(scalarOut.z) == std::bit_cast<std::uint32_t>(laneOut[i].z),
              "ShadeSurface4 matches ShadeSurface bitwise with bilinear+mipmapping");
}

void pointAndSpotLights()
{
    Material mat;
    mat.ambient = {0.f, 0.f, 0.f};
    mat.diffuse = {1.f, 1.f, 1.f};
    mat.specular = {0.5f, 0.5f, 0.5f};
    mat.shininess = 16.f;

    PointLight rawPoint;
    rawPoint.position = {0.f, 0.f, 2.f};
    rawPoint.color = {1.f, 1.f, 1.f};
    rawPoint.range = 10.f;
    const std::array preparedPoint{PreparedPointLight::From(rawPoint)};

    RenderFrameContext framePoint{SRMath::mat4::identity(), {0.f, 0.f, 5.f}, {}, 1, 1, preparedPoint};
    const Rasterizer::SurfaceSample nearSample{{0.f, 0.f, 1.f}, {0.f, 0.f}, {0.f, 0.f, 0.f}};
    const Rasterizer::SurfaceSample farSample{{0.f, 0.f, 1.f}, {0.f, 0.f}, {0.f, 0.f, -4.f}};
    const Rasterizer::SurfaceSample outOfRangeSample{{0.f, 0.f, 1.f}, {0.f, 0.f}, {0.f, 0.f, -10.f}};

    const SRMath::vec3 nearLit = Rasterizer::ShadeSurface(nearSample, mat, framePoint);
    const SRMath::vec3 farLit = Rasterizer::ShadeSurface(farSample, mat, framePoint);
    const SRMath::vec3 outLit = Rasterizer::ShadeSurface(outOfRangeSample, mat, framePoint);
    check(nearLit.x > farLit.x && farLit.x > 0.f, "point light attenuates with distance");
    check(approximatelyEqual(outLit.x, 0.f), "point light smoothly falls to zero at range");

    SpotLight rawSpot;
    rawSpot.position = {0.f, 0.f, 3.f};
    rawSpot.rayDirection = {0.f, 0.f, -1.f};
    rawSpot.color = {1.f, 1.f, 1.f};
    rawSpot.range = 10.f;
    rawSpot.innerConeCos = 0.95f;
    rawSpot.outerConeCos = 0.80f;
    const std::array preparedSpot{PreparedSpotLight::From(rawSpot)};

    RenderFrameContext frameSpot{SRMath::mat4::identity(), {0.f, 0.f, 5.f}, {}, 1, 1, {}, preparedSpot};
    const Rasterizer::SurfaceSample centerSpot{{0.f, 0.f, 1.f}, {0.f, 0.f}, {0.f, 0.f, 0.f}};
    const Rasterizer::SurfaceSample edgeSpot{{0.f, 0.f, 1.f}, {0.f, 0.f}, {1.5f, 0.f, 0.f}};
    const Rasterizer::SurfaceSample outsideSpot{{0.f, 0.f, 1.f}, {0.f, 0.f}, {4.f, 0.f, 0.f}};

    const SRMath::vec3 centerSpotLit = Rasterizer::ShadeSurface(centerSpot, mat, frameSpot);
    const SRMath::vec3 edgeSpotLit = Rasterizer::ShadeSurface(edgeSpot, mat, frameSpot);
    const SRMath::vec3 outsideSpotLit = Rasterizer::ShadeSurface(outsideSpot, mat, frameSpot);
    check(centerSpotLit.x > edgeSpotLit.x && edgeSpotLit.x > 0.f, "spot light attenuates across cone penumbra");
    check(approximatelyEqual(outsideSpotLit.x, 0.f), "spot light is zero outside outerCos cone");

    // 4-lane SSE와 스칼라 경로의 비트 단위 일치 검증
    RenderFrameContext frameBoth{SRMath::mat4::identity(), {0.f, 0.f, 5.f}, {}, 1, 1, preparedPoint, preparedSpot};
    const Rasterizer::SurfaceSample lanes[4] = {nearSample, farSample, edgeSpot, outsideSpot};
    const Material *mats[4] = {&mat, &mat, &mat, &mat};
    SRMath::vec3 colors4[4];
    Rasterizer::ShadeSurface4(lanes, mats, frameBoth, colors4);
    for (int i = 0; i < 4; ++i)
    {
        const SRMath::vec3 scalar = Rasterizer::ShadeSurface(lanes[i], *mats[i], frameBoth);
        check(std::bit_cast<std::uint32_t>(colors4[i].x) == std::bit_cast<std::uint32_t>(scalar.x) &&
                  std::bit_cast<std::uint32_t>(colors4[i].y) == std::bit_cast<std::uint32_t>(scalar.y) &&
                  std::bit_cast<std::uint32_t>(colors4[i].z) == std::bit_cast<std::uint32_t>(scalar.z),
              "4-lane PointLight+SpotLight matches scalar ShadeSurface bitwise");
    }
}

void normalMappingTests()
{
    Mesh mesh;
    mesh.vertices = {{{-1.f, -1.f, 0.f}, {0.f, 0.f}, {0.f, 0.f, 1.f}},
                     {{1.f, -1.f, 0.f}, {1.f, 0.f}, {0.f, 0.f, 1.f}},
                     {{1.f, 1.f, 0.f}, {1.f, 1.f}, {0.f, 0.f, 1.f}},
                     {{-1.f, 1.f, 0.f}, {0.f, 1.f}, {0.f, 0.f, 1.f}}};
    mesh.indices = {0, 1, 2, 0, 2, 3};
    mesh.ComputeTangents();
    check(mesh.GetTangents().size() == 4, "ComputeTangents populates per-vertex tangents");
    for (const auto &t : mesh.GetTangents())
        check(approximatelyEqual(t.x, 1.f) && approximatelyEqual(t.y, 0.f) && approximatelyEqual(t.z, 0.f),
              "XY quad with standard UVs has +X tangent");

    // +X로 기울어진 탄젠트 공간 법선 맵 (R=220, G=128, B=200)
    const std::vector<unsigned char> normalPixels{220, 128, 200, 255};
    auto normalTex = std::make_shared<Texture>(1, 1, normalPixels);

    Material flatMat;
    flatMat.ambient = {0.f, 0.f, 0.f};
    flatMat.diffuse = {1.f, 1.f, 1.f};
    flatMat.specular = {0.f, 0.f, 0.f};

    Material bumpyMat = flatMat;
    bumpyMat.normalTexture = normalTex;
    bumpyMat.normalScale = 1.f;

    // +X 방향에서 비추는 광원 (광선 진행 방향 {-1, 0, 0} -> 광원 방향 {+1, 0, 0})
    const std::array<DirectionalLight, 1> lights{DirectionalLight{{-1.f, 0.f, 0.f}, {1.f, 1.f, 1.f}}};
    const std::array prepared{PreparedDirectionalLight::From(lights[0])};
    const RenderFrameContext frame{SRMath::mat4::identity(), {0.f, 0.f, 3.f}, prepared, 1, 1};

    Rasterizer::SurfaceSample sample{{0.f, 0.f, 1.f}, {0.5f, 0.5f}, {0.f, 0.f, 0.f}};
    sample.tangentWorld = {1.f, 0.f, 0.f};
    sample.bitangentWorld = {0.f, 1.f, 0.f};

    const SRMath::vec3 flatLit = Rasterizer::ShadeSurface(sample, flatMat, frame);
    const SRMath::vec3 bumpyLit = Rasterizer::ShadeSurface(sample, bumpyMat, frame);
    check(approximatelyEqual(flatLit.x, 0.f), "flat +Z normal receives zero grazing +X light");
    check(bumpyLit.x > 0.5f, "+X tilted normal map catches +X light");

    const Rasterizer::SurfaceSample lanes[4] = {sample, sample, sample, sample};
    const Material *mats[4] = {&bumpyMat, &flatMat, &bumpyMat, &flatMat};
    SRMath::vec3 laneOut[4];
    Rasterizer::ShadeSurface4(lanes, mats, frame, laneOut);
    check(std::bit_cast<std::uint32_t>(laneOut[0].x) == std::bit_cast<std::uint32_t>(bumpyLit.x) &&
              std::bit_cast<std::uint32_t>(laneOut[1].x) == std::bit_cast<std::uint32_t>(flatLit.x),
          "ShadeSurface4 matches ShadeSurface bitwise with normal mapping");
}

void srgbGammaTests()
{
    Material mat;
    mat.ambient = {0.25f, 0.5f, 0.75f};
    mat.diffuse = {0.f, 0.f, 0.f};
    mat.specular = {0.f, 0.f, 0.f};

    RenderFrameContext linearFrame{SRMath::mat4::identity(), {0.f, 0.f, 3.f}, {}, 1, 1};
    RenderFrameContext srgbFrame = linearFrame;
    srgbFrame.srgbGamma = true;

    const Rasterizer::SurfaceSample sample{{0.f, 0.f, 1.f}, {0.f, 0.f}, {0.f, 0.f, 0.f}};
    const SRMath::vec3 linearColor = Rasterizer::ShadeSurface(sample, mat, linearFrame);
    const SRMath::vec3 srgbColor = Rasterizer::ShadeSurface(sample, mat, srgbFrame);

    check(srgbColor.x > linearColor.x && srgbColor.y > linearColor.y && srgbColor.z > linearColor.z,
          "sRGB gamma brightens midtones");
    check(approximatelyEqual(srgbColor.x, std::pow(0.25f, 1.0f / 2.2f)) &&
              approximatelyEqual(srgbColor.y, std::pow(0.5f, 1.0f / 2.2f)) &&
              approximatelyEqual(srgbColor.z, std::pow(0.75f, 1.0f / 2.2f)),
          "sRGB gamma matches pow(linear, 1/2.2)");

    const Rasterizer::SurfaceSample lanes[4] = {sample, sample, sample, sample};
    const Material *mats[4] = {&mat, &mat, &mat, &mat};
    SRMath::vec3 laneOut[4];
    Rasterizer::ShadeSurface4(lanes, mats, srgbFrame, laneOut);
    for (int i = 0; i < 4; ++i)
        check(std::bit_cast<std::uint32_t>(laneOut[i].x) == std::bit_cast<std::uint32_t>(srgbColor.x) &&
                  std::bit_cast<std::uint32_t>(laneOut[i].y) == std::bit_cast<std::uint32_t>(srgbColor.y) &&
                  std::bit_cast<std::uint32_t>(laneOut[i].z) == std::bit_cast<std::uint32_t>(srgbColor.z),
              "ShadeSurface4 matches ShadeSurface bitwise with srgbGamma");
}
} // namespace

void sr::test::RegisterRendererTests(TestRegistry &registry)
{
    registry.Add("renderer.clippingAndViewport", RendererTestAccess::clippingAndViewport);
    registry.Add("renderer.workerIsolation", RendererTestAccess::workerIsolation);
    registry.Add("renderer.commandTransformCache", RendererTestAccess::commandTransformCache);
    registry.Add("renderer.flatTileReferenceLayout", RendererTestAccess::flatTileReferenceLayout);
    registry.Add("renderer.optimizedRasterPaths", RendererTestAccess::optimizedRasterPaths);
    registry.Add("renderer.debugColors", RendererTestAccess::debugColors);
    registry.Add("renderer.phongLighting", [](const TestContext &context)
                 { RendererTestAccess::phongLighting(context.root); });
    registry.Add("renderer.fxaaBasics", fxaaTests);
    registry.Add("renderer.textureBilinearAndMipmap", textureBilinearAndMipmap);
    registry.Add("renderer.pointAndSpotLights", pointAndSpotLights);
    registry.Add("renderer.normalMapping", normalMappingTests);
    registry.Add("renderer.srgbGamma", srgbGammaTests);
    registry.Add("renderer.smoke", renderSmoke);
    registry.Add("renderer.primitiveIdPlaneOptional", primitiveIdPlaneOptional);
}

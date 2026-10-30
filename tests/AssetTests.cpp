#include "Graphics/AssetParsing.h"
#include "Graphics/Model.h"
#include "Graphics/ModelLoader.h"
#include "Graphics/Material.h"
#include "Graphics/TextureLoader.h"
#include <fstream>
#include "Math/Frustum.h"
#include "Graphics/Octree.h"
#include "Graphics/MeshSimplifier.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <map>
#include <numbers>
#include <set>
#include <random>
#include "Renderer/RenderQueue.h"
#include "Scene/GameObject.h"
#include "Core/DebugFlags.h"
#include "Core/FrameRunner.h"
#include "Core/SceneRenderCollector.h"
#include "Scene/Scene.h"

#include "TestHarness.h"
#include "TestSupport.h"

using namespace sr::test;

namespace
{
void loaderTests(const TestContext &context)
{
    const auto &root = context.root;
    const auto fixtures = root / "tests/fixtures";
    auto triangle = ModelLoader::LoadOBJ(fixtures / "triangle.obj");
    check(triangle.has_value(), "triangle load");
    const auto meshes = (*triangle)->GetMeshes();
    check(meshes.size() == 1 && meshes[0].indices.size() == 3, "triangle topology");
    for (const auto &vertex : meshes[0].vertices)
        check(approximatelyEqual(vertex.normal.z, 1.f), "generated outward normal");
    auto quad = ModelLoader::LoadOBJ(fixtures / "quad.obj");
    check(quad.has_value() && (*quad)->GetMeshes()[0].indices.size() == 6, "quad triangulation");
    check(approximatelyEqual((*quad)->GetMeshes()[0].material.diffuse.y, 0.f), "material colon fallback");
    auto invalid = ModelLoader::LoadOBJ(fixtures / "invalid-index.obj");
    check(!invalid && invalid.error().line == 2, "invalid OBJ error line");
    auto ngon = ModelLoader::LoadOBJ(fixtures / "ngon.obj");
    check(!ngon && ngon.error().line == 6, "unsupported ngon");
    check(!ModelLoader::LoadOBJ(fixtures / "absent.obj"), "missing OBJ");
    check(!TextureLoader::LoadMTLFile(fixtures / "absent.mtl"), "missing MTL");
    const auto mtl = TextureLoader::LoadMTLFile(fixtures / "invalid.mtl");
    check(!mtl && mtl.error().line == 2, "invalid MTL line");
    check(!TextureLoader::LoadImageFile(fixtures / "absent.png"), "missing texture");
    check(TextureLoader::LoadImageFile(root / "assets/teapot.png").has_value(), "texture decode");
    check(!sr::asset::parseNumber<float>("1xyz"), "partial number rejected");
}
// assets/IronMan.mtl은 CRLF다. Linux에서 \r이 재질 이름에 남아 재질이 전부 기본 회색이 됐던 회귀를 막는다.
void crlfMaterialTests(const TestContext &context)
{
    const auto path = context.outputDirectory / "fixtures/crlf.mtl";
    std::filesystem::create_directories(path.parent_path());
    {
        std::ofstream file(path, std::ios::binary);
        file << "newmtl red\r\nKd 1 0 0\r\n";
    }
    const auto materials = TextureLoader::LoadMTLFile(path);
    check(materials.has_value(), "CRLF MTL load");
    const auto red = materials->find("red");
    check(red != materials->end(), "CRLF material name has no trailing CR");
    check(approximatelyEqual(red->second.diffuse.x, 1.f) && approximatelyEqual(red->second.diffuse.y, 0.f),
          "CRLF material diffuse");
}
void hierarchyTests(const TestContext &context)
{
    const auto &root = context.root;
    const auto fixture = root / "tests/fixtures/triangle.obj";
    auto parent = loadObject(fixture, {10.f, 0.f, 0.f});
    auto child = loadObject(fixture, {-10.f, 0.f, 0.f});
    check(parent->AddChild(child), "attach child");
    check(child->GetParent().lock() == parent, "weak parent link");
    check(!parent->AddChild(child) && !child->AddChild(parent), "duplicates/cycles rejected");
    check(!parent->AddChild(nullptr), "null child rejected");
    parent->Update(0.f, false);
    check(approximatelyEqual(child->GetWorldAABB().Center().x, 0.f), "composed world transform");
    Frustum frustum;
    frustum.Update(SRMath::mat4::identity());
    RenderQueue queue;
    SceneRenderCollector collector;
    collector.Collect(*parent, frustum, DebugFlags{}, queue);
    check(!queue.GetRenderCommands().empty(), "visible child of culled parent");

    // 모델 없는 그룹 노드는 그리지 않지만 자식 변환은 전달한다.
    auto group = std::make_shared<GameObject>(SRMath::vec3{10.f, 0.f, 0.f}, SRMath::vec3{0.f, 0.f, 0.f},
                                              SRMath::vec3{1.f, 1.f, 1.f}, nullptr);
    auto grouped = loadObject(fixture, {-10.f, 0.f, 0.f});
    Scene scene;
    check(scene.AddObject(group), "add root");
    check(!scene.AddObject(group), "duplicate root rejected");
    check(scene.AddObject(grouped, group), "add child through scene");
    check(!scene.AddObject(grouped), "parented object cannot become root");
    check(scene.GetRootObjects().size() == 1, "one root");
    scene.Update(0.f, 1.f, Scene::KeyState{});
    check(!group->GetWorldAABB().IsValid(), "model-less node has empty bounds");
    check(approximatelyEqual(grouped->GetWorldAABB().Center().x, 0.f), "group transform reaches child");
    RenderQueue groupQueue;
    collector.Collect(scene.GetRootObjects(), frustum, DebugFlags{}, groupQueue);
    check(groupQueue.GetRenderCommands().size() == queue.GetRenderCommands().size(),
          "group submits only its child meshes");
}
void frameRunnerTests(const TestContext &context)
{
    const auto fixture = context.root / "tests/fixtures/triangle.obj";
    Scene scene;
    check(scene.AddObject(loadObject(fixture, {0.f, 0.f, 0.f})), "add fixture");
    scene.SetCamera(Camera({0.f, 0.f, -5.f}));
    FrameRunner runner;
    runner.SetScene(std::move(scene));
    int hookCalls = 0;
    runner.SetFrameStartHook([&](FrameRunner &) { ++hookCalls; });
    runner.Update(0.f, 64, 64);
    check(hookCalls == 1, "frame start hook once per update");
    check(!runner.GetRenderQueue().GetRenderCommands().empty(), "runner collects visible mesh");
    runner.Update(0.f, 0, 64);
    check(hookCalls == 2 && runner.GetRenderQueue().GetRenderCommands().empty(), "minimized frame clears queue");
}
// 옥트리 구간 방문(ForEachVisibleRange)은 노드 방문(ForEachVisibleNode)이 내는 삼각형을 같은 순서로 내야 한다.
// 이 순서가 primitive ID와 깊이 동률 판정을 정하므로 하나라도 다르면 pixel_hash가 바뀐다.
void octreeRangeTests(const TestContext &context)
{
    const auto ironMan = ModelLoader::LoadOBJ(context.root / "assets/IronMan.obj");
    check(ironMan.has_value(), "IronMan load");
    std::mt19937 random(12345);
    std::uniform_real_distribution<float> unit(-1.f, 1.f);
    std::size_t cases = 0, insideHits = 0, outsideHits = 0;
    for (int pose = 0; pose < 64; ++pose)
    {
        // 물체 안, 가까이, 멀리, 경계에 걸친 카메라를 고루 섞는다.
        const float distance = pose % 4 == 0 ? 0.5f : 2.f + 40.f * (unit(random) + 1.f);
        Camera camera(SRMath::vec3{unit(random) * 5.f, unit(random) * 5.f, -distance});
        camera.SetCameraYaw(unit(random) * 3.14159f);
        camera.SetCameraPitch(unit(random) * 1.2f);
        camera.Update(0.f, Scene::KeyState{}, 0.5f + (unit(random) + 1.f));
        const float scale = 0.02f + 0.04f * (unit(random) + 1.f);
        const SRMath::mat4 world =
            SRMath::translate(SRMath::vec3{unit(random), unit(random), unit(random)}) *
            SRMath::rotate(SRMath::vec3{unit(random), unit(random) * 3.14159f, unit(random)}) *
            SRMath::scale(SRMath::vec3{scale, scale, scale});
        for (const auto &mesh : (*ironMan)->GetMeshes())
        {
            check(mesh.octree != nullptr, "IronMan mesh has octree");
            std::vector<unsigned int> byNode, byRange;
            mesh.octree->ForEachVisibleNode(camera.GetFrustum(), world, [&](std::span<const unsigned int> indices, const AABB &)
                                            { byNode.insert(byNode.end(), indices.begin(), indices.end()); });
            const unsigned int *previousEnd = nullptr;
            mesh.octree->ForEachVisibleRange(camera.GetFrustum(), world,
                                             [&](std::span<const unsigned int> indices)
                                             {
                                                 check(!indices.empty(), "range is not empty");
                                                 check(indices.data() != previousEnd, "adjacent ranges are merged");
                                                 previousEnd = indices.data() + indices.size();
                                                 byRange.insert(byRange.end(), indices.begin(), indices.end());
                                             });
            check(byNode == byRange, "range traversal emits the node traversal's triangles in order");
            ++cases;
        }
    }
    // 무작위 상자에서 Outside 판정은 IsAABBInFrustum과 같아야 하고, Inside면 그 안의 상자도 통과해야 한다.
    Camera camera(SRMath::vec3{0.f, 0.f, -5.f});
    camera.Update(0.f, Scene::KeyState{}, 16.f / 9.f);
    const Frustum &frustum = camera.GetFrustum();
    for (int index = 0; index < 20000; ++index)
    {
        AABB box;
        box.Encapsulate(SRMath::vec3{unit(random) * 8.f, unit(random) * 8.f, unit(random) * 8.f});
        box.Encapsulate(SRMath::vec3{unit(random) * 8.f, unit(random) * 8.f, unit(random) * 8.f});
        const auto test = frustum.ClassifyAABB(box);
        check((test == FrustumTest::Outside) == !frustum.IsAABBInFrustum(box), "outside matches IsAABBInFrustum");
        if (test == FrustumTest::Inside)
        {
            ++insideHits;
            AABB inner;
            const float t0 = (unit(random) + 1.f) * 0.5f, t1 = (unit(random) + 1.f) * 0.5f;
            inner.Encapsulate(box.min + (box.max - box.min) * std::min(t0, t1));
            inner.Encapsulate(box.min + (box.max - box.min) * std::max(t0, t1));
            check(frustum.IsAABBInFrustum(inner), "box inside an inside box is visible");
        }
        outsideHits += test == FrustumTest::Outside;
    }
    check(cases > 0 && insideHits > 0 && outsideHits > 0, "octree range test covered all classes");
}

// ---- 메시 LOD ----
// 위치(비트 그대로) 기준 정점 번호. LOD 검사는 정점 버퍼의 번호가 아니라 화면에 보이는 위치로 한다.
using PositionKey = std::array<std::uint32_t, 3>;
PositionKey positionKey(const SRMath::vec3 &p)
{
    return {std::bit_cast<std::uint32_t>(p.x + 0.f), std::bit_cast<std::uint32_t>(p.y + 0.f),
            std::bit_cast<std::uint32_t>(p.z + 0.f)};
}
// 방향 없는 변(위치 기준)마다 공유하는 삼각형 수.
std::map<std::pair<PositionKey, PositionKey>, int> edgeUse(const Mesh &mesh, std::span<const unsigned int> indices)
{
    std::map<std::pair<PositionKey, PositionKey>, int> uses;
    for (std::size_t i = 0; i < indices.size(); i += 3)
        for (int k = 0; k < 3; ++k)
        {
            auto a = positionKey(mesh.vertices[indices[i + k]].position);
            auto b = positionKey(mesh.vertices[indices[i + (k + 1) % 3]].position);
            if (b < a)
                std::swap(a, b);
            ++uses[{a, b}];
        }
    return uses;
}
double triangleArea(const Mesh &mesh, std::span<const unsigned int> indices)
{
    double area = 0;
    for (std::size_t i = 0; i < indices.size(); i += 3)
    {
        const auto &p0 = mesh.vertices[indices[i]].position;
        const auto e1 = mesh.vertices[indices[i + 1]].position - p0;
        const auto e2 = mesh.vertices[indices[i + 2]].position - p0;
        area += 0.5 * SRMath::length(SRMath::cross(e1, e2));
    }
    return area;
}
void checkLodChain(const Mesh &mesh, std::string_view name)
{
    std::size_t previous = mesh.indices.size();
    float previousError = 0.f;
    for (const auto &lod : mesh.lods)
    {
        check(lod.indices.size() % 3 == 0 && lod.indices.size() < previous, std::string(name) + ": LOD shrinks");
        check(std::ranges::all_of(lod.indices, [&](unsigned int v) { return v < mesh.vertices.size(); }),
              std::string(name) + ": LOD indices in range");
        check(lod.error >= previousError, std::string(name) + ": LOD error grows");
        previous = lod.indices.size();
        previousError = lod.error;
    }
}

// 평평한 격자: 오차 0으로 크게 줄고, 바깥 경계(정사각형 둘레)는 모양 그대로, 구멍 없이 면적이 같아야 한다.
void lodPlaneTests()
{
    constexpr int cells = 32;
    Mesh mesh;
    for (int y = 0; y <= cells; ++y)
        for (int x = 0; x <= cells; ++x)
        {
            Vertex vertex{};
            vertex.position = {static_cast<float>(x) / cells, static_cast<float>(y) / cells, 0.f};
            vertex.normal = {0.f, 0.f, 1.f};
            mesh.vertices.push_back(vertex);
        }
    for (int y = 0; y < cells; ++y)
        for (int x = 0; x < cells; ++x)
        {
            const unsigned int i0 = y * (cells + 1) + x, i1 = i0 + 1, i2 = i0 + cells + 1, i3 = i2 + 1;
            mesh.indices.insert(mesh.indices.end(), {i0, i1, i3, i0, i3, i2});
        }
    std::vector<Mesh> meshes;
    meshes.push_back(std::move(mesh));
    const auto model = ModelLoader::FromMeshes(std::move(meshes));
    const Mesh &plane = model->GetMeshes()[0];
    check(plane.lods.size() == 4, "plane: four LOD levels");
    checkLodChain(plane, "plane");
    const auto &coarsest = plane.lods.back();
    check(coarsest.indices.size() / 3 <= plane.indices.size() / 3 / 16 + 2, "plane: coarsest is about 1/16");
    check(coarsest.error < 1e-4f, "plane: flat surface simplifies without error");
    for (const auto &lod : plane.lods)
    {
        check(std::abs(triangleArea(plane, lod.indices) - 1.0) < 1e-4, "plane: area kept (no holes, no overlap)");
        for (const auto &[edge, count] : edgeUse(plane, lod.indices))
        {
            // 한 면만 쓰는 변은 정사각형 둘레 위에만 있어야 한다.
            const auto onBoundary = [](const PositionKey &k)
            {
                const float x = std::bit_cast<float>(k[0]), y = std::bit_cast<float>(k[1]);
                return x == 0.f || x == 1.f || y == 0.f || y == 1.f;
            };
            check(count <= 2, "plane: manifold");
            if (count == 1)
                check(onBoundary(edge.first) && onBoundary(edge.second), "plane: no interior hole");
        }
    }
}

// 닫힌 구: 모든 단계가 닫혀 있어야 한다(모든 변을 정확히 두 면이 공유 = 구멍 없음).
void lodSphereTests()
{
    constexpr int rings = 48, sectors = 64;
    Mesh mesh;
    for (int r = 0; r <= rings; ++r)
        for (int s = 0; s <= sectors; ++s)
        {
            const float theta = std::numbers::pi_v<float> * r / rings;
            const float phi = 2.f * std::numbers::pi_v<float> * (s % sectors) / sectors;
            Vertex vertex{};
            vertex.position = {std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi)};
            if (r == 0 || r == rings)
                vertex.position = {0.f, r == 0 ? 1.f : -1.f, 0.f};
            vertex.normal = vertex.position;
            mesh.vertices.push_back(vertex);
        }
    for (int r = 0; r < rings; ++r)
        for (int s = 0; s < sectors; ++s)
        {
            const unsigned int i0 = r * (sectors + 1) + s, i1 = i0 + 1, i2 = i0 + sectors + 1, i3 = i2 + 1;
            if (r != 0)
                mesh.indices.insert(mesh.indices.end(), {i0, i1, i2});
            if (r != rings - 1)
                mesh.indices.insert(mesh.indices.end(), {i1, i3, i2});
        }
    std::vector<Mesh> meshes;
    meshes.push_back(std::move(mesh));
    const auto model = ModelLoader::FromMeshes(std::move(meshes));
    const Mesh &sphere = model->GetMeshes()[0];
    check(!sphere.lods.empty(), "sphere: has LODs");
    checkLodChain(sphere, "sphere");
    for (const auto &[edge, count] : edgeUse(sphere, sphere.indices))
        check(count == 2, "sphere: source is closed");
    for (const auto &lod : sphere.lods)
    {
        for (const auto &[edge, count] : edgeUse(sphere, lod.indices))
            check(count == 2, "sphere: LOD stays closed");
        // 반지름 1 구의 표면에서 정점은 움직이지 않으므로(정점 위로 접기) 면적은 줄기만 하고 크게 줄지 않는다.
        const double area = triangleArea(sphere, lod.indices);
        check(area > 0.85 * 4.0 * std::numbers::pi && area <= 4.0 * std::numbers::pi, "sphere: LOD area plausible");
    }
}

// IronMan: 메시 228개가 맞닿는 위치(다른 메시도 쓰는 위치)는 모든 LOD에 그대로 남아야 틈이 생기지 않는다.
void lodIronManTests(const TestContext &context)
{
    const auto ironMan = ModelLoader::LoadOBJ(context.root / "assets/IronMan.obj");
    check(ironMan.has_value(), "IronMan load");
    const auto meshes = (*ironMan)->GetMeshes();
    std::map<PositionKey, int> owners; // 위치 → 쓰는 메시 수
    for (const auto &mesh : meshes)
    {
        std::set<PositionKey> used;
        for (const auto &vertex : mesh.vertices)
            used.insert(positionKey(vertex.position));
        for (const auto &key : used)
            ++owners[key];
    }
    std::size_t source = 0, coarsest = 0, withLods = 0;
    for (const auto &mesh : meshes)
    {
        checkLodChain(mesh, "IronMan");
        source += mesh.indices.size() / 3;
        coarsest += (mesh.lods.empty() ? mesh.indices.size() : mesh.lods.back().indices.size()) / 3;
        withLods += mesh.lods.empty() ? 0 : 1;
        std::set<PositionKey> sharedInSource;
        for (const auto v : mesh.indices)
            if (const auto key = positionKey(mesh.vertices[v].position); owners[key] > 1)
                sharedInSource.insert(key);
        for (const auto &lod : mesh.lods)
        {
            std::set<PositionKey> sharedInLod;
            for (const auto v : lod.indices)
                if (const auto key = positionKey(mesh.vertices[v].position); owners[key] > 1)
                    sharedInLod.insert(key);
            check(sharedInLod == sharedInSource, "IronMan: shared border positions survive every LOD");
        }
    }
    check(withLods > meshes.size() / 2, "IronMan: most meshes get LODs");
    check(coarsest * 3 < source, "IronMan: coarsest LODs below a third of the triangles");
}

// 선택: 가까우면 원본(옥트리 구간), 멀면 LOD 인덱스, bDisableLod면 항상 원본.
void lodSelectionTests()
{
    constexpr int cells = 32;
    Mesh mesh;
    for (int y = 0; y <= cells; ++y)
        for (int x = 0; x <= cells; ++x)
        {
            Vertex vertex{};
            // 살짝 굽은 면이라 단계마다 오차가 0보다 크다.
            const float u = static_cast<float>(x) / cells - 0.5f, v = static_cast<float>(y) / cells - 0.5f;
            vertex.position = {u, v, 2.f * (u * u + v * v)};
            vertex.normal = {0.f, 0.f, -1.f};
            mesh.vertices.push_back(vertex);
        }
    for (int y = 0; y < cells; ++y)
        for (int x = 0; x < cells; ++x)
        {
            const unsigned int i0 = y * (cells + 1) + x, i1 = i0 + 1, i2 = i0 + cells + 1, i3 = i2 + 1;
            mesh.indices.insert(mesh.indices.end(), {i0, i1, i3, i0, i3, i2});
        }
    std::vector<Mesh> meshes;
    meshes.push_back(std::move(mesh));
    std::shared_ptr<const Model> model = ModelLoader::FromMeshes(std::move(meshes));
    const Mesh &source = model->GetMeshes()[0];
    check(source.lods.size() >= 2 && source.lods[0].error > 0.f, "selection: curved patch has LODs");

    const auto drawnFrom = [&](float distance, bool disableLod)
    {
        Scene scene;
        scene.SetRotateMode(false);
        check(scene.AddObject(std::make_shared<GameObject>(SRMath::vec3{0.f, 0.f, 0.f}, SRMath::vec3{0.f, 0.f, 0.f},
                                                           SRMath::vec3{1.f, 1.f, 1.f}, model)),
              "selection: add object");
        scene.SetCamera(Camera({0.f, 0.f, -distance}));
        FrameRunner runner;
        runner.SetScene(std::move(scene));
        runner.GetDebugFlags().bDisableLod = disableLod;
        runner.Update(0.f, 640, 360);
        const auto commands = runner.GetRenderQueue().GetRenderCommands();
        check(!commands.empty(), "selection: mesh submitted");
        const unsigned int *data = commands.front().indicesToDraw.data();
        for (std::size_t i = 0; i < source.lods.size(); ++i)
            if (data == source.lods[i].indices.data())
                return static_cast<int>(i) + 1;
        return 0;
    };
    check(drawnFrom(1.f, false) == 0, "selection: near object draws the source mesh");
    const int far = drawnFrom(90.f, false);
    check(far == static_cast<int>(source.lods.size()), "selection: far object draws the coarsest LOD");
    check(drawnFrom(90.f, true) == 0, "selection: bDisableLod draws the source mesh");
    // 중간 거리는 그 사이 단계이고 거리가 멀수록 거칠어진다.
    // (카메라 far 평면이 100이라 90까지만 본다.)
    int previous = 0;
    bool intermediate = false;
    for (const float distance : {1.f, 3.f, 10.f, 30.f, 90.f})
    {
        const int level = drawnFrom(distance, false);
        check(level >= previous, "selection: coarser with distance");
        intermediate = intermediate || (level > 0 && level < static_cast<int>(source.lods.size()));
        previous = level;
    }
    check(intermediate, "selection: some distance picks an intermediate LOD");
}
} // namespace

void sr::test::RegisterAssetTests(TestRegistry &registry)
{
    registry.Add("assets.loader", loaderTests);
    registry.Add("assets.crlfMaterial", crlfMaterialTests);
    registry.Add("scene.hierarchy", hierarchyTests);
    registry.Add("scene.frameRunner", frameRunnerTests);
    registry.Add("scene.octreeRanges", octreeRangeTests);
    registry.Add("lod.plane", lodPlaneTests);
    registry.Add("lod.sphere", lodSphereTests);
    registry.Add("lod.ironMan", lodIronManTests);
    registry.Add("lod.selection", lodSelectionTests);
}

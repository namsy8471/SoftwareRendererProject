#include "Graphics/AssetParsing.h"
#include "Graphics/Model.h"
#include "Graphics/ModelLoader.h"
#include "Graphics/Material.h"
#include "Graphics/TextureLoader.h"
#include <fstream>
#include "Math/Frustum.h"
#include "Graphics/Octree.h"
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
} // namespace

void sr::test::RegisterAssetTests(TestRegistry &registry)
{
    registry.Add("assets.loader", loaderTests);
    registry.Add("assets.crlfMaterial", crlfMaterialTests);
    registry.Add("scene.hierarchy", hierarchyTests);
    registry.Add("scene.frameRunner", frameRunnerTests);
    registry.Add("scene.octreeRanges", octreeRangeTests);
}

#include "Graphics/AssetParsing.h"
#include "Graphics/Model.h"
#include "Graphics/ModelLoader.h"
#include "Graphics/Material.h"
#include "Graphics/TextureLoader.h"
#include <fstream>
#include "Math/Frustum.h"
#include "Renderer/RenderQueue.h"
#include "Scene/GameObject.h"
#include "Utils/DebugUtils.h"

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
    parent->SubmitToRenderQueue(queue, frustum, DebugFlags{});
    check(!queue.GetRenderCommands().empty(), "visible child of culled parent");
}
} // namespace

void sr::test::RegisterAssetTests(TestRegistry &registry)
{
    registry.Add("assets.loader", loaderTests);
    registry.Add("assets.crlfMaterial", crlfMaterialTests);
    registry.Add("scene.hierarchy", hierarchyTests);
}

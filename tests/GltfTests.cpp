#include "IO/Gltf.h"
#include "IO/FileIO.h"
#include "Core/FrameRunner.h"
#include "Graphics/GltfLoader.h"
#include "Graphics/Material.h"
#include "Graphics/Model.h"
#include "Graphics/Texture.h"
#include "Scene/GameObject.h"
#include "Scene/Scene.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>

#include "TestHarness.h"
#include "TestSupport.h"

using namespace sr::test;

namespace
{
namespace gltf = sr::gltf;

std::span<const std::byte> asBytes(std::string_view text)
{
    return std::as_bytes(std::span{text.data(), text.size()});
}

std::string base64(std::span<const std::uint8_t> bytes)
{
    constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string text;
    for (std::size_t i = 0; i < bytes.size(); i += 3)
    {
        std::uint32_t chunk = static_cast<std::uint32_t>(bytes[i]) << 16;
        if (i + 1 < bytes.size())
            chunk |= static_cast<std::uint32_t>(bytes[i + 1]) << 8;
        if (i + 2 < bytes.size())
            chunk |= bytes[i + 2];
        text += alphabet[(chunk >> 18) & 63];
        text += alphabet[(chunk >> 12) & 63];
        text += i + 1 < bytes.size() ? alphabet[(chunk >> 6) & 63] : '=';
        text += i + 2 < bytes.size() ? alphabet[chunk & 63] : '=';
    }
    return text;
}

template <class T> std::vector<std::uint8_t> packed(std::initializer_list<T> values)
{
    std::vector<std::uint8_t> bytes(values.size() * sizeof(T));
    std::memcpy(bytes.data(), values.begin(), bytes.size());
    return bytes;
}

std::vector<std::byte> readFixture(const std::filesystem::path &path)
{
    auto text = sr::io::ReadWholeFile(path);
    check(text.has_value(), "fixture read: " + path.generic_string());
    const auto bytes = std::as_bytes(std::span{*text});
    return {bytes.begin(), bytes.end()};
}

void vec3Near(const SRMath::vec3 &actual, float x, float y, float z, std::string_view what)
{
    check(approximatelyEqual(actual.x, x, 1e-4f) && approximatelyEqual(actual.y, y, 1e-4f) &&
              approximatelyEqual(actual.z, z, 1e-4f),
          std::string(what) + " (" + std::to_string(actual.x) + ", " + std::to_string(actual.y) + ", " +
              std::to_string(actual.z) + ")");
}

void base64Tests()
{
    const auto decoded = gltf::DecodeBase64("SGVsbG8=");
    check(decoded && decoded->size() == 5 && static_cast<char>((*decoded)[4]) == 'o', "padded base64");
    check(gltf::DecodeBase64("SGVsbG8").has_value(), "unpadded base64");
    check(gltf::DecodeBase64("").has_value() && gltf::DecodeBase64("")->empty(), "empty base64");
    check(!gltf::DecodeBase64("SGV*bG8="), "invalid base64 character");
    check(!gltf::DecodeBase64("S"), "truncated base64");
}

// glTF sampler의 wrapS/wrapT. 기본(가장자리 고정)은 기존 OBJ 텍스처 동작 그대로다.
void textureWrapTests()
{
    // 가로 3텍셀(빨강, 초록, 파랑). 최근접 샘플은 x = floor(u' * 2)라 u' 0~0.5 빨강, 0.5~1 초록, 1 파랑이다.
    Texture texture(3, 1, {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255});
    const auto texel = [&](float u)
    {
        const auto color = texture.Sample(u, 0.f);
        return color.x > 0.5f ? 'r' : color.y > 0.5f ? 'g' : 'b';
    };
    check(texel(3.25f) == 'b' && texel(-2.f) == 'r', "clamp to edge by default");
    texture.SetWrap(TextureWrap::Repeat, TextureWrap::Repeat);
    check(texel(3.25f) == 'r' && texel(3.75f) == 'g' && texel(-0.25f) == 'g', "repeat");
    texture.SetWrap(TextureWrap::MirroredRepeat, TextureWrap::ClampToEdge);
    check(texel(1.25f) == 'g' && texel(1.75f) == 'r' && texel(-0.25f) == 'r' && texel(1.f) == 'b',
          "mirrored repeat");
}

// 외부 .bin, 외부 PNG, ushort 인덱스, 선 primitive 건너뛰기, 재질 변환.
void texturedQuadTests(const TestContext &context)
{
    const auto path = context.root / "tests/fixtures/gltf/textured-quad.gltf";
    const auto document = gltf::LoadFile(path);
    check(document.has_value(),
          "textured quad document: " + (document ? std::string{} : document.error().message));
    check(document->buffers.size() == 1 && document->buffers[0].size() == 144, "external buffer bytes");
    check(document->meshes.size() == 1 && document->meshes[0].primitives.size() == 2, "mesh primitives");
    const auto uvs = gltf::ReadAccessorFloats(*document, 2);
    check(uvs && uvs->size() == 8 && (*uvs)[6] == 0.f && (*uvs)[7] == 0.f, "uv accessor");

    int decodedImages = 0;
    std::string seenMime = "unset";
    gltf::SceneLoadOptions options;
    // 디코더 주입: 3단계 자체 디코더가 같은 자리를 쓴다. 외부 파일 URI는 mimeType이 비어 있다.
    options.decodeImage =
        [&](std::span<const std::byte> bytes,
            std::string_view mime) -> std::expected<std::shared_ptr<Texture>, AssetLoadError>
    {
        ++decodedImages;
        seenMime = std::string(mime);
        check(bytes.size() > 8 && bytes[1] == std::byte{'P'}, "decoder receives PNG bytes");
        return std::make_shared<Texture>(1, 1, std::vector<unsigned char>{10, 20, 30, 255});
    };
    const auto custom = gltf::LoadScene(path, options);
    check(custom.has_value() && decodedImages == 1 && seenMime.empty(), "custom image decoder");

    const auto scene = gltf::LoadScene(path);
    check(scene.has_value(), "textured quad scene");
    check(scene->warnings.size() == 1 && scene->warnings[0].find("line") != std::string::npos,
          "line primitive skipped with warning");
    check(scene->root->GetChildren().size() == 1, "one root node");
    const auto *model = scene->nodes[0].object->GetModel();
    if (!model) // /analyze(C6011)가 check 안의 null 검사를 따라가지 못한다
        throw std::runtime_error("textured quad node has no model");
    check(model->GetMeshes().size() == 1, "line primitive not drawn");
    const auto &mesh = model->GetMeshes()[0];
    check(mesh.vertices.size() == 4 && mesh.indices.size() == 6, "quad topology");
    // CCW (0,1,2)는 OBJ 로더처럼 (0,2,1)로 저장된다.
    check(mesh.indices[0] == 0 && mesh.indices[1] == 2 && mesh.indices[2] == 1, "winding flipped like OBJ");
    check(approximatelyEqual(mesh.vertices[3].texcoord.x, 0.f) &&
              approximatelyEqual(mesh.vertices[3].texcoord.y, 0.f),
          "uv kept (glTF origin is top-left like Texture)");
    vec3Near(mesh.material.diffuse, 1.f, 0.5f, 0.25f, "base color factor");
    check(approximatelyEqual(mesh.material.specular.x, 0.02f) && mesh.material.shininess > 20.f,
          "metallic-roughness approximation");
    check(mesh.material.name == "Checker", "material name");
    const auto &texture = mesh.material.diffuseTexture;
    check(texture && texture->GetWidth() == 2 && texture->GetHeight() == 2, "PNG texture decoded");
    check(texture->GetWrapU() == TextureWrap::Repeat && texture->GetWrapV() == TextureWrap::Repeat,
          "sampler without wrap defaults to REPEAT");
    Texture clamped = *texture; // REPEAT에서는 v=1이 0으로 돌아가므로 가장자리 고정 복사본으로 행 순서를 본다
    clamped.SetWrap(TextureWrap::ClampToEdge, TextureWrap::ClampToEdge);
    const auto topLeft = clamped.Sample(0.f, 0.f);
    const auto bottomLeft = clamped.Sample(0.f, 1.f);
    check(approximatelyEqual(topLeft.x, 1.f) && approximatelyEqual(bottomLeft.z, 1.f), "texel orientation");
    vec3Near(scene->bounds.min, -1.f, -1.f, 0.f, "bounds min");
    vec3Near(scene->bounds.max, 1.f, 1.f, 0.f, "bounds max");
}

void checkHierarchy(const gltf::LoadedScene &scene, bool textured)
{
    check(scene.nodes.size() == 4, "node count");
    check(scene.root->GetChildren().size() == 1 && scene.root->GetChildren()[0] == scene.nodes[0].object,
          "scene root under group");
    const auto &root = scene.nodes[0].object;
    const auto &spinner = scene.nodes[1].object;
    const auto &matrixNode = scene.nodes[2].object;
    const auto &joint = scene.nodes[3].object;
    check(root->GetChildren().size() == 2 && spinner->GetChildren().size() == 1, "children attached");
    check(joint->GetParent().lock() == spinner, "joint parent");
    vec3Near(root->GetPosition(), 1.f, 0.f, 0.f, "TRS translation");
    vec3Near(spinner->GetRotation(), 0.f, std::numbers::pi_v<float> / 2.f, 0.f, "quaternion to axis-angle");
    vec3Near(matrixNode->GetPosition(), 0.f, 2.f, 0.f, "matrix translation");
    vec3Near(matrixNode->GetScale(), 2.f, 2.f, 2.f, "matrix scale");
    vec3Near(matrixNode->GetRotation(), 0.f, 0.f, 0.f, "matrix rotation");
    // root(1,0,0) * spinner(Y 90도) * joint(0,1,0) → 월드 (1,1,0)
    const auto &world = joint->GetWorldMatrix();
    vec3Near(SRMath::vec3(world[3].x, world[3].y, world[3].z), 1.f, 1.f, 0.f, "composed world translation");

    // 같은 mesh를 쓰는 두 node는 각자 Model을 가진다.
    const auto *spinnerModel = spinner->GetModel();
    const auto *matrixModel = matrixNode->GetModel();
    if (!spinnerModel || !matrixModel) // /analyze(C6011)가 check 안의 null 검사를 따라가지 못한다
        throw std::runtime_error("hierarchy mesh node has no model");
    check(spinnerModel != matrixModel, "mesh instanced per node");
    check(!root->GetModel() && !joint->GetModel(), "mesh-less nodes are groups");
    const auto meshes = spinnerModel->GetMeshes();
    check(meshes.size() == 2, "two triangle primitives");
    check(meshes[0].indices.size() == 3 && meshes[0].indices[1] == 2, "non-indexed triangle");
    for (const auto &vertex : meshes[0].vertices)
        vec3Near(vertex.normal, 0.f, 0.f, 1.f, "generated outward normal");
    // strip 4정점 → 삼각형 2개. 두 번째는 스펙대로 (1,3,2)=(2,1,3)이고 뒤집어 (2,3,1)로 저장된다.
    check(meshes[1].indices.size() == 6, "strip triangulated");
    check(meshes[1].indices[3] == 2 && meshes[1].indices[4] == 3 && meshes[1].indices[5] == 1,
          "strip winding");
    vec3Near(meshes[1].vertices[3].position, 1.f, 1.f, 2.f, "sparse override");
    vec3Near(meshes[1].vertices[2].position, 0.f, 1.f, 1.f, "sparse keeps base");
    check(approximatelyEqual(meshes[1].vertices[1].texcoord.x, 1.f) &&
              approximatelyEqual(meshes[1].vertices[1].texcoord.y, 1.f),
          "normalized ubyte uv");
    vec3Near(meshes[1].material.diffuse, 1.f, 0.5f, 0.f, "shared material");
    check(static_cast<bool>(meshes[1].material.diffuseTexture) == textured, "embedded texture");
    if (textured)
    {
        // UV 없는 삼각형은 텍스처를 버리고 경고를 남긴다. 같은 이미지는 인스턴스끼리 공유한다.
        check(!meshes[0].material.diffuseTexture, "texture without TEXCOORD dropped");
        check(scene.warnings.size() == 1 && scene.warnings[0].find("TEXCOORD_0") != std::string::npos,
              "missing TEXCOORD warning");
        check(matrixModel->GetMeshes()[1].material.diffuseTexture == meshes[1].material.diffuseTexture,
              "texture decoded once");
    }

    check(scene.meshSkinWeights.size() == 1 && scene.meshSkinWeights[0].size() == 2, "skin weights per mesh");
    const auto &weights = scene.meshSkinWeights[0][0];
    check(weights.joints.size() == 3 && weights.joints[0][1] == 1 &&
              approximatelyEqual(weights.weights[0][0], 0.75f),
          "joint weights");
    check(scene.meshSkinWeights[0][1].joints.empty(), "unskinned primitive has no weights");
    check(scene.nodes[1].skin == 0 && scene.nodes[1].mesh == 0, "node bindings");
    check(scene.skins.size() == 1 && scene.skins[0].joints == std::vector<int>{1, 3} &&
              scene.skins[0].skeleton == 1,
          "skin joints");
    check(approximatelyEqual(scene.skins[0].inverseBindMatrices[1][3].y, -1.f),
          "inverse bind matrix column-major");

    check(scene.animations.size() == 1 && scene.animations[0].name == "Wiggle", "animation clip");
    const auto &tracks = scene.animations[0].tracks;
    check(tracks.size() == 3 && approximatelyEqual(scene.animations[0].duration, 2.f), "animation tracks");
    check(tracks[0].node == 3 && tracks[0].path == gltf::AnimationPath::Translation &&
              tracks[0].values.size() == 6,
          "linear translation track");
    check(tracks[1].interpolation == gltf::Interpolation::Step && tracks[1].componentCount == 4 &&
              approximatelyEqual(tracks[1].values[5], 1.f),
          "step rotation track");
    check(tracks[2].interpolation == gltf::Interpolation::CubicSpline && tracks[2].values.size() == 18 &&
              approximatelyEqual(tracks[2].values[12], 2.f),
          "cubic spline scale track");
    // 경계: matrix node(크기 2, +2 위)의 strip 정점 (1,1,2) → (2,4,4) + root (1,0,0) = (3,4,4)
    vec3Near(scene.bounds.max, 3.f, 4.f, 4.f, "hierarchy bounds");
}

void hierarchyTests(const TestContext &context)
{
    const auto fixtures = context.root / "tests/fixtures/gltf";
    const auto embedded = gltf::LoadScene(fixtures / "hierarchy.gltf");
    check(embedded.has_value(), "data URI glTF: " + (embedded ? std::string{} : embedded.error().message));
    check(embedded->warnings.empty(), "no warnings");
    checkHierarchy(*embedded, false);

    const auto binary = gltf::LoadScene(fixtures / "hierarchy.glb");
    check(binary.has_value(), "GLB: " + (binary ? std::string{} : binary.error().message));
    checkHierarchy(*binary, true);
    // 확장자가 아니라 매직으로 GLB를 알아본다.
    const auto bytes = readFixture(fixtures / "hierarchy.glb");
    check(gltf::LoadFromMemory(bytes, fixtures).has_value(), "GLB from memory");
}

// 문서 하나를 메모리에서 읽고, 실패하면 메시지에 needle이 들어 있는지 본다.
void expectError(std::string_view json, std::string_view needle, bool buildScene = false)
{
    auto document = gltf::LoadFromMemory(asBytes(json), ".");
    std::string message;
    if (!document)
        message = document.error().message;
    else if (buildScene)
    {
        const auto scene = gltf::BuildScene(*document);
        check(!scene.has_value(), "expected scene error: " + std::string(needle));
        message = scene.error().message;
    }
    else
        throw std::runtime_error("expected glTF error: " + std::string(needle));
    check(message.find(needle) != std::string::npos,
          "error '" + message + "' should mention '" + std::string(needle) + "'");
}

// 위치 3개(float VEC3)와 인덱스 3개(ubyte)를 담은 data URI 버퍼로 문서를 만든다.
std::string triangleDocument()
{
    auto bytes = packed<float>({0, 0, 0, 1, 0, 0, 0, 1, 0});
    const auto indices = packed<std::uint8_t>({0, 1, 5, 0});
    bytes.insert(bytes.end(), indices.begin(), indices.end());
    const std::string data = base64(bytes);
    return R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":40,"uri":"data:application/octet-stream;base64,)" +
           data +
           R"("}],"bufferViews":[{"buffer":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":4}],
        "accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},
                     {"bufferView":1,"componentType":5121,"count":3,"type":"SCALAR"}],
        "meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],
        "nodes":[{"mesh":0}]})";
}

void errorTests()
{
    const auto minimal = gltf::LoadFromMemory(asBytes(R"({"asset":{"version":"2.0"}})"), ".");
    check(minimal.has_value(), "minimal document");
    const auto empty = gltf::BuildScene(*minimal);
    check(empty && empty->root->GetChildren().empty() && !empty->bounds.IsValid(), "empty scene");

    expectError("{}", "asset");
    expectError(R"({"asset":{"version":"1.0"}})", "version");
    expectError(R"({"asset":{"version":"2.0"},"extensionsRequired":["KHR_draco_mesh_compression"]})",
                "KHR_draco_mesh_compression");
    check(gltf::LoadFromMemory(
              asBytes(R"({"asset":{"version":"2.0"},"extensionsRequired":["KHR_mesh_quantization"]})"), ".")
              .has_value(),
          "quantization extension accepted");
    const auto badJson = gltf::LoadFromMemory(asBytes("{\n\"asset\": [}"), ".");
    check(!badJson && badJson.error().line == 2, "JSON error line");

    expectError(R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":4,"uri":"data:;base64,AAAA"}]})",
                "fewer bytes");
    expectError(R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":1,"uri":"data:;base64,A*AA"}]})",
                "base64");
    expectError(
        R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":1,"uri":"http://example.com/a.bin"}]})",
        "relative");
    expectError(R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":1,"uri":"/etc/passwd"}]})",
                "relative");
    expectError(R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":1,"uri":"absent%20file.bin"}]})",
                "open");
    expectError(R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":1}]})", "no uri");
    expectError(R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":3,"uri":"data:;base64,AAAA"}],
        "bufferViews":[{"buffer":0,"byteOffset":2,"byteLength":2}]})",
                "outside its buffer");
    expectError(R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":3,"uri":"data:;base64,AAAA"}],
        "bufferViews":[{"buffer":0,"byteLength":3}],
        "accessors":[{"bufferView":0,"componentType":5126,"count":1,"type":"SCALAR"}]})",
                "outside its bufferView");
    expectError(
        R"({"asset":{"version":"2.0"},"accessors":[{"componentType":5126,"normalized":true,"count":1,"type":"SCALAR"}]})",
        "normalized");
    expectError(R"({"asset":{"version":"2.0"},"accessors":[{"componentType":5126,"count":1,"type":"VEC5"}]})",
                "type");
    expectError(
        R"({"asset":{"version":"2.0"},"accessors":[{"componentType":5126,"count":1e12,"type":"MAT4"}]})",
        "too large");
    expectError(R"({"asset":{"version":"2.0"},"nodes":[{"children":[1]},{"children":[0]}]})", "cycle");
    expectError(R"({"asset":{"version":"2.0"},"nodes":[{"children":[2]},{"children":[2]},{}]})",
                "more than one parent");
    expectError(R"({"asset":{"version":"2.0"},"nodes":[{"children":[0]}]})", "more than one parent");
    expectError(R"({"asset":{"version":"2.0"},"nodes":[{"children":[1]},{}],"scenes":[{"nodes":[1]}]})",
                "not a root");
    expectError(R"({"asset":{"version":"2.0"},"nodes":[{"mesh":0}]})", "missing mesh");
    expectError(R"({"asset":{"version":"2.0"},"nodes":[{"translation":[1,2]}]})", "3 numbers");
    expectError(R"({"asset":{"version":"2.0"},"scene":0})", "missing scene");
    expectError(R"({"asset":{"version":"2.0"},"samplers":[{"wrapS":1234}]})", "wrapS");
    expectError(R"({"asset":{"version":"2.0"},"textures":[{"sampler":0}]})", "missing sampler");

    check(gltf::LoadFromMemory(asBytes(triangleDocument()), ".").has_value(), "triangle document parses");
    expectError(triangleDocument(), "missing vertex", true);
    expectError(R"({"asset":{"version":"2.0"},"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}]})",
                "missing accessor");

    // sparse 인덱스가 count를 넘으면 읽기에서 거부한다.
    auto sparseBytes = packed<std::uint8_t>({9, 0, 0, 0});
    const auto value = packed<float>({1.f});
    sparseBytes.insert(sparseBytes.end(), value.begin(), value.end());
    const std::string sparse =
        R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":8,"uri":"data:;base64,)" +
        base64(sparseBytes) + R"("}],
        "bufferViews":[{"buffer":0,"byteLength":1},{"buffer":0,"byteOffset":4,"byteLength":4}],
        "accessors":[{"componentType":5126,"count":2,"type":"SCALAR",
                      "sparse":{"count":1,"indices":{"bufferView":0,"componentType":5121},"values":{"bufferView":1}}}]})";
    const auto sparseDocument = gltf::LoadFromMemory(asBytes(sparse), ".");
    check(sparseDocument.has_value(), "sparse document");
    check(!gltf::ReadAccessorFloats(*sparseDocument, 0), "sparse index out of range");

    // GLB 헤더·청크 손상
    // length 0이면 실제 길이를 넣는다.
    const auto glb = [](std::uint32_t version, std::uint32_t length, std::uint32_t chunkType)
    {
        const std::string json = R"({"asset":{"version":"2.0"}}   )";
        const auto size = static_cast<std::uint32_t>(json.size());
        std::vector<std::uint8_t> bytes =
            packed<std::uint32_t>({0x46546C67, version, length != 0 ? length : 20 + size, size, chunkType});
        bytes.insert(bytes.end(), json.begin(), json.end());
        return bytes;
    };
    const auto glbError = [](const std::vector<std::uint8_t> &bytes, std::string_view needle)
    {
        const auto result = gltf::LoadFromMemory(std::as_bytes(std::span{bytes}), ".");
        check(!result && result.error().message.find(needle) != std::string::npos,
              "GLB error should mention " + std::string(needle));
    };
    const auto good = glb(2, 0, 0x4E4F534A);
    check(gltf::LoadFromMemory(std::as_bytes(std::span{good}), ".").has_value(), "minimal GLB");
    glbError(glb(1, 0, 0x4E4F534A), "version");
    glbError(glb(2, 4000, 0x4E4F534A), "length");
    glbError(glb(2, 0, 0x004E4942), "JSON chunk");
    auto truncated = good;
    truncated.resize(30);
    truncated[8] = 30;
    glbError(truncated, "outside");
}

// 잘린 GLB와 바이트가 바뀐 GLB/JSON을 많이 넣어 본다. 결과(성공/실패)는 상관없고 ASan/UBSan과
// 범위 검사가 깨지지 않는지가 목적이다.
void robustnessTests(const TestContext &context)
{
    const auto fixtures = context.root / "tests/fixtures/gltf";
    const auto glb = readFixture(fixtures / "hierarchy.glb");
    const auto text = readFixture(fixtures / "hierarchy.gltf");
    gltf::SceneLoadOptions options;
    // 바뀐 PNG를 이미지 디코더에 넘기는 것은 이 테스트의 관심사가 아니다.
    options.decodeImage = [](std::span<const std::byte>,
                             std::string_view) -> std::expected<std::shared_ptr<Texture>, AssetLoadError>
    { return std::make_shared<Texture>(); };
    const auto attempt = [&](std::span<const std::byte> bytes)
    {
        if (auto document = gltf::LoadFromMemory(bytes, fixtures))
            (void)gltf::BuildScene(*document, options);
    };
    for (std::size_t length = 0; length < glb.size(); length += 7)
        attempt(std::span{glb}.first(length));

    std::uint32_t seed = 12345;
    const auto next = [&]
    {
        seed = seed * 1664525u + 1013904223u;
        return seed >> 8;
    };
    for (int round = 0; round < 1500; ++round)
    {
        auto mutated = (round % 2 == 0) ? glb : text;
        const int flips = 1 + static_cast<int>(next() % 4);
        for (int f = 0; f < flips; ++f)
            mutated[next() % mutated.size()] = static_cast<std::byte>(next() & 0xFF);
        attempt(mutated);
    }
}

// 불러온 장면이 실제 렌더 경로(FrameRunner)를 지나 픽셀로 그려지는지 본다.
void renderTests(const TestContext &context)
{
    auto loaded = gltf::LoadScene(context.root / "tests/fixtures/gltf/textured-quad.gltf");
    check(loaded.has_value(), "quad load");
    // 쿼드는 +z를 본다. 카메라(-z)와 광원(+z로 진행)을 향하도록 Y축으로 반 바퀴 돌린다.
    loaded->root->SetRotation({0.f, std::numbers::pi_v<float>, 0.f});
    Scene scene;
    check(scene.AddObject(loaded->root), "add glTF root");
    scene.AddLight(DirectionalLight{});
    scene.SetCamera(Camera({0.f, 0.f, -5.f}));
    scene.SetRotateMode(false);
    FrameRunner runner;
    runner.SetScene(std::move(scene));
    MemorySurface memory(64, 64);
    runner.Update(0.f, 64, 64);
    check(!runner.GetRenderQueue().GetRenderCommands().empty(), "glTF mesh collected");
    runner.Render(memory.target);
    const std::uint32_t center = memory.pixels[32 * 64 + 32];
    const std::uint32_t corner = memory.pixels[0];
    check(center != corner, "quad drawn over background");
    const auto red = (center >> 16) & 0xFF, green = (center >> 8) & 0xFF, blue = center & 0xFF;
    check(red > green && red > blue, "textured base color (red texel * orange factor)");
}

void alphaMaterialTests()
{
    const std::string gltfJson = R"({
        "asset": {"version": "2.0"},
        "materials": [
            {
                "name": "MaskMat",
                "alphaMode": "MASK",
                "alphaCutoff": 0.35,
                "pbrMetallicRoughness": {
                    "baseColorFactor": [1.0, 1.0, 1.0, 0.8]
                }
            },
            {
                "name": "BlendMat",
                "alphaMode": "BLEND",
                "pbrMetallicRoughness": {
                    "baseColorFactor": [0.5, 0.5, 0.5, 0.4]
                }
            },
            {
                "name": "OpaqueMat",
                "alphaMode": "OPAQUE",
                "pbrMetallicRoughness": {
                    "baseColorFactor": [1.0, 1.0, 1.0, 1.0]
                }
            }
        ],
        "meshes": [
            {
                "primitives": [
                    {
                        "attributes": {"POSITION": 0},
                        "material": 0
                    },
                    {
                        "attributes": {"POSITION": 0},
                        "material": 1
                    },
                    {
                        "attributes": {"POSITION": 0},
                        "material": 2
                    }
                ]
            }
        ],
        "accessors": [
            {
                "bufferView": 0,
                "componentType": 5126,
                "count": 3,
                "type": "VEC3"
            }
        ],
        "bufferViews": [
            {
                "buffer": 0,
                "byteLength": 36
            }
        ],
        "buffers": [
            {
                "byteLength": 36,
                "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=="
            }
        ],
        "nodes": [
            {"mesh": 0}
        ],
        "scenes": [
            {"nodes": [0]}
        ],
        "scene": 0
    })";

    const auto bytes = asBytes(gltfJson);
    auto doc = gltf::LoadFromMemory(bytes, "");
    check(doc.has_value(), "load glTF with alpha materials");
    check(doc->materials.size() == 3, "3 materials loaded");

    check(doc->materials[0].alphaMode == 1, "doc MaskMat alphaMode MASK (1)");
    check(approximatelyEqual(doc->materials[0].alphaCutoff, 0.35f), "doc MaskMat alphaCutoff 0.35");
    check(approximatelyEqual(doc->materials[0].baseColorFactor[3], 0.8f), "doc MaskMat baseColorFactor.w 0.8");

    check(doc->materials[1].alphaMode == 2, "doc BlendMat alphaMode BLEND (2)");
    check(approximatelyEqual(doc->materials[1].alphaCutoff, 0.5f), "doc BlendMat default alphaCutoff 0.5");
    check(approximatelyEqual(doc->materials[1].baseColorFactor[3], 0.4f), "doc BlendMat baseColorFactor.w 0.4");

    check(doc->materials[2].alphaMode == 0, "doc OpaqueMat alphaMode OPAQUE (0)");
    check(approximatelyEqual(doc->materials[2].alphaCutoff, 0.5f), "doc OpaqueMat default alphaCutoff 0.5");
    check(approximatelyEqual(doc->materials[2].baseColorFactor[3], 1.0f), "doc OpaqueMat baseColorFactor.w 1.0");

    // Test BuildScene and convertMaterial mapping
    gltf::SceneLoadOptions options;
    auto loaded = gltf::BuildScene(*doc, options);
    check(loaded.has_value(), "BuildScene succeeds");
    check(!loaded->nodes.empty() && loaded->nodes[0].object != nullptr, "node binding valid");
    const auto *model = loaded->nodes[0].object->GetModel();
    check(model != nullptr, "node has model");
    if (!model)
        return;
    const auto meshes = model->GetMeshes();
    check(meshes.size() == 3, "model has 3 primitive meshes");

    // Mesh 0: Mask
    check(meshes[0].material.alphaMode == AlphaMode::Mask, "mapped MaskMat alphaMode is AlphaMode::Mask");
    check(approximatelyEqual(meshes[0].material.alphaCutoff, 0.35f), "mapped MaskMat alphaCutoff is 0.35");
    check(approximatelyEqual(meshes[0].material.opacity, 0.8f), "mapped MaskMat opacity is 0.8");

    // Mesh 1: Blend
    check(meshes[1].material.alphaMode == AlphaMode::Blend, "mapped BlendMat alphaMode is AlphaMode::Blend");
    check(approximatelyEqual(meshes[1].material.alphaCutoff, 0.5f), "mapped BlendMat alphaCutoff is 0.5");
    check(approximatelyEqual(meshes[1].material.opacity, 0.4f), "mapped BlendMat opacity is 0.4");

    // Mesh 2: Opaque
    check(meshes[2].material.alphaMode == AlphaMode::Opaque, "mapped OpaqueMat alphaMode is AlphaMode::Opaque");
    check(approximatelyEqual(meshes[2].material.alphaCutoff, 0.5f), "mapped OpaqueMat alphaCutoff is 0.5");
    check(approximatelyEqual(meshes[2].material.opacity, 1.0f), "mapped OpaqueMat opacity is 1.0");
}
} // namespace

void sr::test::RegisterGltfTests(TestRegistry &registry)
{
    registry.Add("gltf.alphaMaterials", alphaMaterialTests);
    registry.Add("gltf.base64", base64Tests);
    registry.Add("gltf.textureWrap", textureWrapTests);
    registry.Add("gltf.texturedQuad", texturedQuadTests);
    registry.Add("gltf.hierarchy", hierarchyTests);
    registry.Add("gltf.errors", errorTests);
    registry.Add("gltf.robustness", robustnessTests);
    registry.Add("gltf.render", renderTests);
}

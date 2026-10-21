// 자체 이미지 코덱(src/Graphics/Codecs) 테스트. fixture는 tools/make_image_fixtures.py가 만든
// tests/fixtures/images/*이고, 형식 변형(색 형식, 비트 깊이, 인터레이스, RLE, 샘플링 비율, 재시작 마커 등)을
// 하나씩 담는다.
#include "Graphics/Codecs/ImageCodecs.h"
#include "Graphics/Material.h"
#include "Graphics/Model.h"
#include "Graphics/ModelLoader.h"
#include "Graphics/Texture.h"
#include "Graphics/TextureLoader.h"

#include "TestHarness.h"
#include "TestSupport.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

using namespace sr::test;

namespace
{
[[nodiscard]] std::vector<std::uint8_t> readFile(const std::filesystem::path &path)
{
    std::ifstream file(path, std::ios::binary);
    check(static_cast<bool>(file), "cannot open " + path.generic_string());
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

[[nodiscard]] std::vector<std::filesystem::path> fixtureImages(const TestContext &context)
{
    std::vector<std::filesystem::path> paths;
    for (const auto &entry : std::filesystem::directory_iterator(context.root / "tests/fixtures/images"))
        if (entry.is_regular_file() && entry.path().extension() != ".txt")
            paths.push_back(entry.path());
    std::ranges::sort(paths);
    check(paths.size() >= 60, "image fixtures present");
    return paths;
}

// 이름에 "unsupported"가 있으면 자체 디코더가 거부해야 하는 파일이다.
[[nodiscard]] bool expectedUnsupported(const std::filesystem::path &path)
{
    return path.filename().string().find("unsupported") != std::string::npos;
}

[[nodiscard]] std::uint64_t hashBytes(std::span<const std::uint8_t> bytes) noexcept
{
    std::uint64_t hash = 14695981039346656037ull;
    for (const auto byte : bytes)
        hash = (hash ^ byte) * 1099511628211ull;
    return hash;
}

// 기준 해시 표(이름 너비 높이 RGBA FNV-1a64)와 디코딩 결과를 비교한다.
//  stb-reference.txt: PNG/JPEG/BMP/TGA. stb_image 2.30을 오라클로 픽셀 단위 비교해 모두 같았던 값을 기록했다.
//  dds-reference.txt: DDS. tools/make_image_fixtures.py가 Pillow 디코더(BCn 등)나 직접 계산(비트 마스크)으로
//  만든다.
void referenceTests(const TestContext &context, const char *listName, int minimumEntries)
{
    const auto directory = context.root / "tests/fixtures/images";
    std::ifstream list(directory / listName);
    check(static_cast<bool>(list), std::string(listName) + " present");
    std::string line;
    int compared = 0;
    std::string failures;
    while (std::getline(list, line))
    {
        if (line.empty() || line.starts_with('#'))
            continue;
        std::istringstream fields(line);
        std::string name;
        int width = 0, height = 0;
        std::string hashText;
        fields >> name >> width >> height >> hashText;
        const auto path = directory / name;
        const auto bytes = readFile(path);
        const auto image = sr::codec::DecodeImage(bytes, path.extension().string());
        if (!image)
            failures += "\n    " + name + ": " + image.error();
        else if (image->width != width || image->height != height ||
                 hashBytes(image->rgba) != std::stoull(hashText, nullptr, 16))
            failures += "\n    " + name + ": pixels differ from reference";
        ++compared;
    }
    check(compared >= minimumEntries, std::string(listName) + " entries");
    check(failures.empty(), "fixtures differ from reference:" + failures);
    std::cout << "  " << compared << " fixtures match " << listName << '\n';
}

// 표에 없는 fixture가 생기면(생성 스크립트만 고치고 표를 안 고친 경우) 알린다.
void referenceCoverageTests(const TestContext &context)
{
    const auto directory = context.root / "tests/fixtures/images";
    std::string listed;
    for (const char *listName : {"stb-reference.txt", "dds-reference.txt"})
    {
        std::ifstream list(directory / listName);
        listed.append(std::istreambuf_iterator<char>(list), std::istreambuf_iterator<char>());
    }
    for (const auto &path : fixtureImages(context))
    {
        const auto name = path.filename().string();
        if (expectedUnsupported(path) || name == "tga-right-to-left.tga")
            continue;
        check(listed.find("\n" + name + " ") != std::string::npos, name + " has a reference hash");
    }
}

void unsupportedTests(const TestContext &context)
{
    for (const auto &path : fixtureImages(context))
    {
        if (!expectedUnsupported(path))
            continue;
        const auto bytes = readFile(path);
        const auto result = sr::codec::DecodeImage(bytes, path.extension().string());
        check(!result && !result.error().empty(),
              path.filename().string() + ": should be rejected with a reason");
    }
    const std::vector<std::uint8_t> text{'h', 'e', 'l', 'l', 'o'};
    check(!sr::codec::DecodeImage(text, ".png"), "unknown bytes rejected");
    check(sr::codec::DetectImageFormat(text, ".tga") == sr::codec::ImageFormat::Unknown,
          "short TGA rejected");
}

void tgaOrientationTests(const TestContext &context)
{
    // 3×2, 위→아래 + 오른쪽→왼쪽으로 저장. 파일 안의 R 값은 30,20,10 / 60,50,40 순서이고 화면에서는 각 행이
    // 뒤집힌다.
    const auto bytes = readFile(context.root / "tests/fixtures/images/tga-right-to-left.tga");
    const auto image = sr::codec::DecodeTga(bytes);
    check(image && image->width == 3 && image->height == 2, "right-to-left TGA decodes");
    const std::array<std::uint8_t, 6> expectedRed{10, 20, 30, 40, 50, 60};
    for (std::size_t i = 0; i < expectedRed.size(); ++i)
        check(image->rgba[i * 4] == expectedRed[i] && image->rgba[i * 4 + 3] == 255,
              "right-to-left pixel order");
}

// 잘리거나 바이트가 바뀐 파일에서 범위 밖 접근·무한 루프·UB가 없어야 한다(ASan/UBSan 빌드에서 의미가 크다).
// 결과는 오류든 이미지든 상관없고, 이미지면 크기와 버퍼가 맞아야 한다.
void corruptionTests(const TestContext &context)
{
    std::uint32_t state = 12345;
    const auto random = [&]()
    {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    };
    int attempts = 0;
    for (const auto &path : fixtureImages(context))
    {
        const auto original = readFile(path);
        const auto extension = path.extension().string();
        const auto decodeAndCheck = [&](std::span<const std::uint8_t> bytes)
        {
            const auto image = sr::codec::DecodeImage(bytes, extension);
            if (image)
                check(image->width > 0 && image->height > 0 &&
                          image->rgba.size() == static_cast<std::size_t>(image->width) * image->height * 4,
                      "corrupted decode returned consistent image");
            ++attempts;
        };
        // 잘림: 앞부분만 남긴 여러 길이.
        for (std::size_t length = 0; length < original.size();
             length += std::max<std::size_t>(1, original.size() / 23))
            decodeAndCheck(std::span(original).first(length));
        // 바이트 변조: 1~4바이트를 임의 값으로 바꾼다.
        for (int trial = 0; trial < 40; ++trial)
        {
            auto mutated = original;
            const int edits = 1 + static_cast<int>(random() % 4);
            for (int e = 0; e < edits; ++e)
                mutated[random() % mutated.size()] = static_cast<std::uint8_t>(random());
            decodeAndCheck(mutated);
        }
    }
    std::cout << "  " << attempts << " truncated/corrupted decodes without memory errors\n";
}

void limitTests()
{
    // IHDR 16777216×16777216: 할당 전에 거부해야 한다.
    std::vector<std::uint8_t> png{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n', 0, 0, 0,
                                  13,   'I', 'H', 'D', 'R',  1,    0,    0,    0, 1, 0,
                                  0,    0,   8,   2,   0,    0,    0,    0,    0, 0, 0};
    const std::array<std::uint8_t, 12> idat{0, 0, 0, 0, 'I', 'D', 'A', 'T', 0, 0, 0, 0};
    const std::array<std::uint8_t, 12> iend{0, 0, 0, 0, 'I', 'E', 'N', 'D', 0, 0, 0, 0};
    png.insert(png.end(), idat.begin(), idat.end());
    png.insert(png.end(), iend.begin(), iend.end());
    const auto huge = sr::codec::DecodePng(png);
    check(!huge, "oversized PNG rejected");

    // zlib: 출력 상한을 넘는 스트림(저장 블록 5바이트, 상한 4)은 오류다.
    const std::vector<std::uint8_t> stored{0x78, 0x01, 0x01, 0x05, 0x00, 0xFA, 0xFF, 'h',
                                           'e',  'l',  'l',  'o',  0x06, 0x2C, 0x02, 0x15};
    check(sr::codec::InflateZlib(stored, 5, 5).value_or(std::vector<std::uint8_t>{}) ==
              std::vector<std::uint8_t>{'h', 'e', 'l', 'l', 'o'},
          "stored zlib block");
    check(!sr::codec::InflateZlib(stored, 4, 4), "zlib output limit");
    auto badAdler = stored;
    badAdler.back() ^= 1;
    check(!sr::codec::InflateZlib(badAdler, 5, 5), "zlib adler-32 checked");
}

// 자체 PNG 인코더 → 자체 디코더가 같은 픽셀을 돌려줘야 한다. 무늬·잡음·단색으로 LZ77 경로를 고루 지난다.
void pngRoundTripTests()
{
    std::uint32_t state = 7;
    for (const auto &[width, height] :
         {std::pair{1, 1}, std::pair{3, 7}, std::pair{64, 1}, std::pair{320, 180}})
    {
        for (int kind = 0; kind < 3; ++kind)
        {
            std::vector<std::uint8_t> rgb(static_cast<std::size_t>(width) * height * 3);
            for (std::size_t i = 0; i < rgb.size(); ++i)
            {
                state = state * 1664525u + 1013904223u;
                rgb[i] = kind == 0   ? static_cast<std::uint8_t>(state >> 24)
                         : kind == 1 ? static_cast<std::uint8_t>((i / 3 % 17) * 15 + i % 3)
                                     : std::uint8_t{200};
            }
            const auto png = sr::codec::EncodePngRgb(width, height, rgb);
            const auto decoded = sr::codec::DecodePng(png);
            check(decoded && decoded->width == width && decoded->height == height, "encoded PNG decodes");
            for (std::size_t p = 0; p < rgb.size() / 3; ++p)
                check(decoded->rgba[p * 4] == rgb[p * 3] && decoded->rgba[p * 4 + 1] == rgb[p * 3 + 1] &&
                          decoded->rgba[p * 4 + 2] == rgb[p * 3 + 2] && decoded->rgba[p * 4 + 3] == 255,
                      "PNG round trip pixel");
        }
    }
}

void textureLoaderTests(const TestContext &context)
{
    const auto images = context.root / "tests/fixtures/images";
    for (const char *name :
         {"png-rgba8.png", "jpeg-420.jpg", "bmp-rgb24.bmp", "tga-rgb24-rle.tga", "dds-dx10-bc7-random.dds"})
    {
        const auto texture = TextureLoader::LoadImageFile(images / name);
        check(texture.has_value() && (*texture)->GetWidth() > 0,
              std::string("TextureLoader decodes ") + name);
    }
    const auto unsupported = TextureLoader::LoadImageFile(images / "jpeg-progressive-unsupported.jpg");
    check(!unsupported && unsupported.error().message.find("progressive") != std::string::npos,
          "TextureLoader reports decoder reason");
}
// map_Kd 옵션·공백 경로·역슬래시 경로, 같은 파일 공유, 없는 파일은 텍스처 없이 계속하는지 본다.
void mtlTextureTests(const TestContext &context)
{
    const auto directory = context.outputDirectory / "fixtures/map-kd";
    std::filesystem::create_directories(directory / "sub dir");
    std::filesystem::copy_file(context.root / "tests/fixtures/images/png-rgba8.png",
                               directory / "sub dir/a b.png",
                               std::filesystem::copy_options::overwrite_existing);
    const auto mtlPath = directory / "materials.mtl";
    {
        std::ofstream file(mtlPath, std::ios::binary);
        file << "newmtl plain\nmap_Kd sub dir/a b.png\n"
             << "newmtl options\nmap_Kd -o 0.5 0.5 1 -s 2 2 -bm 0.3 -clamp on sub dir/a b.png  \n"
             << "newmtl windows\r\nmap_Kd sub dir\\a b.png\r\n"
             << "newmtl missing\nKd 1 0 0\nmap_Kd absent.png\n";
    }
    const auto materials = TextureLoader::LoadMTLFile(mtlPath);
    check(materials.has_value(), "map_Kd MTL loads");
    const auto texture = [&](const char *name) { return materials->at(name).diffuseTexture; };
    check(texture("plain") && texture("plain")->GetWidth() == 37, "map_Kd path with spaces");
    check(texture("options") == texture("plain"), "map_Kd options skipped and texture shared");
    check(texture("windows") == texture("plain"), "map_Kd backslash path");
    check(!texture("missing") && approximatelyEqual(materials->at("missing").diffuse.x, 1.f),
          "missing map_Kd keeps material color");
    const auto bad = TextureLoader::LoadMTLFile(context.root / "tests/fixtures/invalid.mtl");
    check(!bad, "invalid MTL still rejected");

    // OBJ vt의 v는 아래→위이므로 텍스처 행 순서(위→아래)에 맞게 뒤집혀야 한다.
    const auto objPath = directory / "uv.obj";
    {
        std::ofstream file(objPath, std::ios::binary);
        file << "mtllib materials.mtl\nv 0 0 0\nv 1 0 0\nv 0 1 0\nvt 0 0\nvt 1 0.25\nvt 0 1\nusemtl plain\n"
             << "f 1/1 2/2 3/3\n";
    }
    const auto model = ModelLoader::LoadOBJ(objPath);
    check(model.has_value(), "textured OBJ loads");
    const auto &mesh = (*model)->GetMeshes().front();
    check(mesh.material.diffuseTexture != nullptr, "OBJ mesh gets map_Kd texture");
    check(approximatelyEqual(mesh.vertices[0].texcoord.y, 1.f) &&
              approximatelyEqual(mesh.vertices[1].texcoord.y, .75f) &&
              approximatelyEqual(mesh.vertices[2].texcoord.y, 0.f),
          "OBJ v flipped to top-left origin");

    // 데모 장면의 teapot은 assets/default.mtl로 teapot.png를 쓴다.
    const auto teapot = ModelLoader::LoadOBJ(context.root / "assets/teapot");
    check(teapot.has_value(), "teapot loads");
    for (const auto &teapotMesh : (*teapot)->GetMeshes())
        check(teapotMesh.material.diffuseTexture && teapotMesh.material.diffuseTexture->GetWidth() == 128,
              "teapot meshes are textured");
}
} // namespace

void sr::test::RegisterImageCodecTests(TestRegistry &registry)
{
    registry.Add("codecs.stbReference",
                 [](const TestContext &context) { referenceTests(context, "stb-reference.txt", 80); });
    registry.Add("codecs.ddsReference",
                 [](const TestContext &context) { referenceTests(context, "dds-reference.txt", 25); });
    registry.Add("codecs.referenceCoverage", referenceCoverageTests);
    registry.Add("codecs.unsupported", unsupportedTests);
    registry.Add("codecs.tgaOrientation", tgaOrientationTests);
    registry.Add("codecs.corruption", corruptionTests);
    registry.Add("codecs.limits", limitTests);
    registry.Add("codecs.pngRoundTrip", pngRoundTripTests);
    registry.Add("codecs.textureLoader", textureLoaderTests);
    registry.Add("codecs.mtlMapKd", mtlTextureTests);
}

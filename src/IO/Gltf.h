#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "Utils/AssetLoadError.h"

// 외부 라이브러리 없이 glTF 2.0(.gltf + .bin/data URI, .glb)을 읽어 형식 그대로의 문서 구조로 만든다.
// 렌더러 타입(Mesh, GameObject)은 모른다. 장면으로 바꾸는 일은 Graphics/GltfLoader가 한다.
// 입력은 끌어다 놓은 임의 파일일 수 있으므로 모든 인덱스·바이트 범위를 Load에서 미리 검사한다.
// 검사를 통과한 Document에서는 ReadAccessor가 범위 밖을 읽지 않는다.
namespace sr::gltf
{
// glTF는 JSON에서 정수 인덱스로 서로를 가리킨다. 없음은 -1이다.
inline constexpr int none = -1;

enum class ComponentType : std::uint32_t
{
    Byte = 5120,
    UnsignedByte = 5121,
    Short = 5122,
    UnsignedShort = 5123,
    UnsignedInt = 5125,
    Float = 5126
};

struct BufferView
{
    int buffer = none;
    std::size_t byteOffset = 0;
    std::size_t byteLength = 0;
    std::size_t byteStride = 0; // 0이면 요소가 빈틈없이 붙어 있다
};

struct AccessorSparse
{
    std::size_t count = 0;
    int indicesBufferView = none;
    std::size_t indicesByteOffset = 0;
    ComponentType indicesComponentType = ComponentType::UnsignedInt;
    int valuesBufferView = none;
    std::size_t valuesByteOffset = 0;
};

struct Accessor
{
    int bufferView = none; // none이면 모든 값이 0(sparse로 일부만 바뀔 수 있다)
    std::size_t byteOffset = 0;
    ComponentType componentType = ComponentType::Float;
    bool normalized = false;
    std::size_t count = 0;
    int componentCount = 1; // SCALAR 1, VEC2 2, VEC3 3, VEC4 4, MAT4 16 (MAT2/MAT3은 float만)
    std::optional<AccessorSparse> sparse;
};

enum class PrimitiveMode
{
    Points = 0,
    Lines = 1,
    LineLoop = 2,
    LineStrip = 3,
    Triangles = 4,
    TriangleStrip = 5,
    TriangleFan = 6
};

struct Attribute
{
    std::string name; // "POSITION", "NORMAL", "TEXCOORD_0", "JOINTS_0", ...
    int accessor = none;
};

struct Primitive
{
    std::vector<Attribute> attributes;
    int indices = none;
    int material = none;
    PrimitiveMode mode = PrimitiveMode::Triangles;
    bool hasMorphTargets = false; // 아직 쓰지 않는다. 경고만 낸다

    [[nodiscard]] int FindAttribute(std::string_view name) const noexcept;
};

struct Mesh
{
    std::string name;
    std::vector<Primitive> primitives;
};

struct TextureRef
{
    int texture = none;
    int texCoord = 0; // TEXCOORD_n
};

// metallic-roughness 모델의 값을 그대로 담는다. Phong으로 옮기는 근사는 GltfLoader가 한다.
struct Material
{
    std::string name;
    std::array<float, 4> baseColorFactor{1.f, 1.f, 1.f, 1.f};
    TextureRef baseColorTexture{};
    float metallicFactor = 1.f;
    float roughnessFactor = 1.f;
    std::array<float, 3> emissiveFactor{0.f, 0.f, 0.f};
    TextureRef emissiveTexture{}; // 아직 샘플링하지 않는다. 있으면 emissiveFactor도 쓰지 않는다
    bool doubleSided = false;
};

struct Image
{
    std::string name;
    std::string uri; // 비어 있으면 bufferView에서 읽는다
    int bufferView = none;
    std::string mimeType; // bufferView일 때 필수(image/png, image/jpeg)
};

// GL 상수 그대로: 33071 CLAMP_TO_EDGE, 33648 MIRRORED_REPEAT, 10497 REPEAT. 필터는 Texture::Sample이
// nearest뿐이라 읽지 않는다.
struct Sampler
{
    int wrapS = 10497;
    int wrapT = 10497;
};

struct Texture
{
    int source = none;  // images 인덱스
    int sampler = none; // none이면 REPEAT
};

struct Node
{
    std::string name;
    std::vector<int> children;
    int mesh = none;
    int skin = none;
    // matrix가 있으면 TRS 대신 쓴다. 둘 다 열 우선(column-major)이다.
    std::optional<std::array<float, 16>> matrix;
    std::array<float, 3> translation{0.f, 0.f, 0.f};
    std::array<float, 4> rotation{0.f, 0.f, 0.f, 1.f}; // 쿼터니언 x, y, z, w
    std::array<float, 3> scale{1.f, 1.f, 1.f};
    int parent = none; // Load가 채운다. 노드 그래프는 숲(부모 최대 하나, 순환 없음)으로 검증된다
};

struct Scene
{
    std::string name;
    std::vector<int> nodes; // 루트 노드
};

struct Skin
{
    std::string name;
    int inverseBindMatrices = none; // MAT4 float accessor. none이면 모두 단위 행렬
    int skeleton = none;
    std::vector<int> joints; // node 인덱스
};

enum class AnimationPath
{
    Translation,
    Rotation,
    Scale,
    Weights
};

enum class Interpolation
{
    Linear,
    Step,
    CubicSpline
};

struct AnimationSampler
{
    int input = none;  // 키 시간(초), SCALAR float
    int output = none; // 값. CUBICSPLINE이면 키마다 (in-tangent, value, out-tangent) 세 개
    Interpolation interpolation = Interpolation::Linear;
};

struct AnimationChannel
{
    int sampler = none;
    int node = none; // none이면 대상 없음(확장용). 로더가 건너뛴다
    AnimationPath path = AnimationPath::Translation;
};

struct Animation
{
    std::string name;
    std::vector<AnimationSampler> samplers;
    std::vector<AnimationChannel> channels;
};

struct Document
{
    std::filesystem::path baseDirectory;         // 상대 URI의 기준(.gltf/.glb가 있는 폴더)
    std::vector<std::vector<std::byte>> buffers; // URI·data URI·GLB BIN에서 읽은 실제 바이트
    std::vector<BufferView> bufferViews;
    std::vector<Accessor> accessors;
    std::vector<Mesh> meshes;
    std::vector<Material> materials;
    std::vector<Texture> textures;
    std::vector<Sampler> samplers;
    std::vector<Image> images;
    std::vector<Node> nodes;
    std::vector<Scene> scenes;
    int scene = none; // 기본 장면
    std::vector<Skin> skins;
    std::vector<Animation> animations;
    std::vector<std::string> warnings; // 무시한 기능(모프 타깃 등). 로드는 계속한다
};

struct LoadOptions
{
    std::uintmax_t maxFileBytes = 1ull << 30; // .gltf/.glb와 외부 .bin 각각의 상한
};

// 확장자가 아니라 앞 4바이트(glTF 매직)로 GLB와 JSON을 구분한다.
[[nodiscard]] std::expected<Document, AssetLoadError> LoadFile(const std::filesystem::path &path,
                                                               const LoadOptions &options = {});
// bytes는 .gltf 텍스트나 .glb 내용이다. 외부 파일 URI는 baseDirectory 기준으로 읽는다.
[[nodiscard]] std::expected<Document, AssetLoadError> LoadFromMemory(
    std::span<const std::byte> bytes, const std::filesystem::path &baseDirectory,
    const LoadOptions &options = {});

// accessor 값을 float로 펼친다(count * componentCount개). normalized 정수는 스펙대로 [0,1]/[-1,1]로,
// normalized가 아닌 정수(JOINTS 등)는 값 그대로 바꾼다. sparse 치환을 적용한다.
[[nodiscard]] std::expected<std::vector<float>, std::string> ReadAccessorFloats(const Document &document,
                                                                                int accessor);
// 인덱스 accessor(SCALAR, 부호 없는 정수)를 읽는다.
[[nodiscard]] std::expected<std::vector<std::uint32_t>, std::string> ReadAccessorIndices(
    const Document &document, int accessor);

// 이미지 바이트(data URI, bufferView, 외부 파일)를 돌려준다. 디코딩은 호출자가 한다.
[[nodiscard]] std::expected<std::vector<std::byte>, AssetLoadError> ReadImageBytes(
    const Document &document, int image, const LoadOptions &options = {});

// RFC 4648 base64(패딩 생략 허용). 공개하는 이유는 테스트와 이후 씬 파일 임베드 재사용이다.
[[nodiscard]] std::optional<std::vector<std::byte>> DecodeBase64(std::string_view text);
} // namespace sr::gltf

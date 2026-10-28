#include "Graphics/GltfLoader.h"
#include "Graphics/Material.h"
#include "Graphics/Mesh.h"
#include "Graphics/Model.h"
#include "Graphics/ModelLoader.h"
#include "Graphics/Texture.h"
#include "Scene/GameObject.h"

#include <algorithm>
#include <iterator>
#include <cmath>
#include <numeric>
#include <optional>
#include <utility>

#include "Graphics/Codecs/ImageCodecs.h"

namespace sr::gltf
{
namespace
{
using Error = std::string;

// GameObject::Update가 재귀이므로 지나치게 깊은 계층은 거부한다.
constexpr std::size_t maxHierarchyDepth = 512;

std::string where(std::string_view array, std::size_t index)
{
    return std::string(array) + "[" + std::to_string(index) + "]";
}

float finiteOrZero(float value) noexcept
{
    return std::isfinite(value) ? value : 0.f;
}

// glTF 이미지는 PNG/JPEG이고 시그니처로 판별되므로 mimeType은 쓰지 않는다.
std::expected<std::shared_ptr<::Texture>, AssetLoadError> decodeWithCodecs(std::span<const std::byte> bytes,
                                                                           std::string_view)
{
    auto image = sr::codec::DecodeImage(
        std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size()));
    if (!image)
        return std::unexpected(AssetLoadError{"Unable to decode image (" + image.error() + ")"});
    auto texture = std::make_shared<::Texture>(image->width, image->height, std::move(image->rgba));
    texture->GenerateMipmaps();
    return texture;
}

// 렌더러는 Phong이다. metallic-roughness를 보기 좋은 쪽으로 근사한다:
//   diffuse = baseColor(금속도 무시: 금속을 검게 그리지 않도록), specular = mix(0.04, baseColor, metallic) *
//   (1 - roughness), shininess = 2 / roughness^4 - 2 (Blinn-Phong 근사, [1, 256]), ambient = baseColor * 0.1
//   + emissive.
// emissiveTexture가 있으면 emissiveFactor는 그 텍스처의 배율이라(보통 1) 텍스처 없이 더하면 물체 전체가
// 하얗게 된다. 그때는 뺀다.
::Material convertMaterial(const Material &source)
{
    ::Material material;
    material.name = source.name;
    const auto &base = source.baseColorFactor;
    material.diffuse = {base[0], base[1], base[2]};
    const bool emissiveSampled = source.emissiveTexture.texture != none;
    const auto ambient = [&](std::size_t channel)
    { return base[channel] * 0.1f + (emissiveSampled ? 0.f : source.emissiveFactor[channel]); };
    material.ambient = {ambient(0), ambient(1), ambient(2)};
    const float metallic = source.metallicFactor;
    const float glossy = 1.f - source.roughnessFactor;
    const auto specular = [&](float channel) { return (0.04f + (channel - 0.04f) * metallic) * glossy; };
    material.specular = {specular(base[0]), specular(base[1]), specular(base[2])};
    const float roughness2 = source.roughnessFactor * source.roughnessFactor;
    material.shininess = std::clamp(2.f / std::max(roughness2 * roughness2, 1e-4f) - 2.f, 1.f, 256.f);
    material.opacity = base[3];
    material.normalScale = source.normalScale;
    return material;
}

// node 인스턴스마다 Mesh를 새로 만들 수 있도록 Octree 없는 원본을 보관한다.
struct PrimitiveSource
{
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices; // 렌더러 winding(OBJ 로더와 같게 뒤집음)
    ::Material material;
    VertexSkinWeights skin;
};

class SceneBuilder
{
  public:
    SceneBuilder(const Document &document, const SceneLoadOptions &options)
        : m_document(document), m_options(options),
          m_decode(options.decodeImage ? options.decodeImage : ImageDecoder{decodeWithCodecs})
    {
        m_meshSources.resize(document.meshes.size());
        m_textures.resize(document.images.size());
        m_textureTried.resize(document.images.size(), false);
    }

    std::expected<LoadedScene, AssetLoadError> Build()
    {
        LoadedScene result;
        result.warnings = m_document.warnings;
        result.nodes.resize(m_document.nodes.size());
        for (std::size_t i = 0; i < m_document.nodes.size(); ++i)
            result.nodes[i] = {nullptr, m_document.nodes[i].mesh, m_document.nodes[i].skin};

        auto roots = chooseRoots();
        if (!roots)
            return fail(roots.error());
        result.root = std::make_shared<GameObject>(SRMath::vec3{0.f, 0.f, 0.f}, SRMath::vec3{0.f, 0.f, 0.f},
                                                   SRMath::vec3{1.f, 1.f, 1.f}, nullptr);

        // 부모를 먼저 만들고 붙인다. AddChild가 자식 행렬을 바로 갱신하므로 위에서 아래로 가야 한다.
        struct Pending
        {
            int node;
            std::shared_ptr<GameObject> parent;
            std::size_t depth;
        };
        std::vector<Pending> stack;
        for (auto it = roots->rbegin(); it != roots->rend(); ++it)
            stack.push_back({*it, result.root, 1});
        while (!stack.empty())
        {
            const Pending pending = std::move(stack.back());
            stack.pop_back();
            if (pending.depth > maxHierarchyDepth)
                return fail("glTF node hierarchy is too deep");
            const Node &node = m_document.nodes[static_cast<std::size_t>(pending.node)];
            auto object = createObject(node, static_cast<std::size_t>(pending.node));
            if (!object)
                return fail(object.error());
            if (!pending.parent->AddChild(*object))
                return fail("Failed to attach " + where("nodes", static_cast<std::size_t>(pending.node)));
            result.nodes[static_cast<std::size_t>(pending.node)].object = *object;
            for (auto it = node.children.rbegin(); it != node.children.rend(); ++it)
                stack.push_back({*it, *object, pending.depth + 1});
        }

        for (const auto &binding : result.nodes)
            if (binding.object)
                result.bounds.Encapsulate(binding.object->GetWorldAABB());

        result.meshSkinWeights.resize(m_meshSources.size());
        for (std::size_t m = 0; m < m_meshSources.size(); ++m)
            if (m_meshSources[m])
                for (const auto &primitive : *m_meshSources[m])
                    result.meshSkinWeights[m].push_back(primitive.skin);

        if (auto skins = buildSkins(); !skins)
            return fail(skins.error());
        else
            result.skins = std::move(*skins);
        result.animations = buildAnimations();
        std::ranges::move(m_warnings, std::back_inserter(result.warnings));
        return result;
    }

  private:
    const Document &m_document;
    const SceneLoadOptions &m_options;
    ImageDecoder m_decode;
    std::vector<std::optional<std::vector<PrimitiveSource>>> m_meshSources;
    std::vector<std::shared_ptr<::Texture>> m_textures;
    std::vector<bool> m_textureTried;
    std::vector<std::pair<int, std::shared_ptr<::Texture>>> m_wrapVariants; // (image, wrap이 다른 복사본)
    std::vector<std::string> m_warnings;

    static std::unexpected<AssetLoadError> fail(std::string message)
    {
        return std::unexpected(AssetLoadError{std::move(message)});
    }

    std::expected<std::vector<int>, Error> chooseRoots() const
    {
        int sceneIndex = m_options.scene != none ? m_options.scene : m_document.scene;
        if (sceneIndex == none && !m_document.scenes.empty())
            sceneIndex = 0;
        if (sceneIndex != none)
        {
            if (static_cast<std::size_t>(sceneIndex) >= m_document.scenes.size())
                return std::unexpected("scene " + std::to_string(sceneIndex) + " does not exist");
            return m_document.scenes[static_cast<std::size_t>(sceneIndex)].nodes;
        }
        std::vector<int> roots;
        for (std::size_t i = 0; i < m_document.nodes.size(); ++i)
            if (m_document.nodes[i].parent == none)
                roots.push_back(static_cast<int>(i));
        return roots;
    }

    std::expected<std::shared_ptr<GameObject>, Error> createObject(const Node &node, std::size_t index)
    {
        std::array<float, 3> translation = node.translation;
        std::array<float, 4> rotation = node.rotation;
        std::array<float, 3> scale = node.scale;
        if (node.matrix)
            decompose(*node.matrix, translation, rotation, scale, index);

        std::unique_ptr<Model> model;
        if (node.mesh != none)
        {
            auto sources = meshSources(node.mesh);
            if (!sources)
                return std::unexpected(sources.error());
            if ((*sources)->empty())
                m_warnings.push_back(where("meshes", static_cast<std::size_t>(node.mesh)) +
                                     ": no triangle primitives to draw");
            else
            {
                std::vector<::Mesh> meshes;
                meshes.reserve((*sources)->size());
                for (const auto &source : **sources)
                {
                    ::Mesh mesh;
                    mesh.vertices = source.vertices;
                    mesh.indices = source.indices;
                    mesh.material = source.material;
                    meshes.push_back(std::move(mesh));
                }
                model = ModelLoader::FromMeshes(std::move(meshes));
            }
        }
        return std::make_shared<GameObject>(SRMath::vec3{translation[0], translation[1], translation[2]},
                                            QuaternionToAxisAngle(rotation),
                                            SRMath::vec3{scale[0], scale[1], scale[2]}, std::move(model));
    }

    // 열 우선 4x4를 T * R * S로 나눈다. 전단(shear)은 표현할 수 없어 버려지고, 음수 행렬식은 x 크기의 부호로
    // 옮긴다.
    void decompose(const std::array<float, 16> &m, std::array<float, 3> &translation,
                   std::array<float, 4> &rotation, std::array<float, 3> &scale, std::size_t index)
    {
        translation = {m[12], m[13], m[14]};
        SRMath::vec3 columns[3] = {{m[0], m[1], m[2]}, {m[4], m[5], m[6]}, {m[8], m[9], m[10]}};
        for (int c = 0; c < 3; ++c)
            scale[static_cast<std::size_t>(c)] = SRMath::length(columns[c]);
        if (SRMath::dot(SRMath::cross(columns[0], columns[1]), columns[2]) < 0.f)
            scale[0] = -scale[0];
        if (m[3] != 0.f || m[7] != 0.f || m[11] != 0.f || m[15] != 1.f)
            m_warnings.push_back(where("nodes", index) + ": projective matrix treated as affine");
        rotation = {0.f, 0.f, 0.f, 1.f};
        if (std::abs(scale[0]) < 1e-12f || scale[1] < 1e-12f || scale[2] < 1e-12f)
            return;
        float r[3][3]; // r[열][행]
        for (int c = 0; c < 3; ++c)
        {
            const float inverse = 1.f / scale[static_cast<std::size_t>(c)];
            r[c][0] = columns[c].x * inverse;
            r[c][1] = columns[c].y * inverse;
            r[c][2] = columns[c].z * inverse;
        }
        // 회전 행렬 → 쿼터니언(Shepperd). 원소 R(row, col) = r[col][row].
        const float trace = r[0][0] + r[1][1] + r[2][2];
        float x, y, z, w;
        if (trace > 0.f)
        {
            const float s = std::sqrt(trace + 1.f) * 2.f;
            w = 0.25f * s;
            x = (r[1][2] - r[2][1]) / s;
            y = (r[2][0] - r[0][2]) / s;
            z = (r[0][1] - r[1][0]) / s;
        }
        else if (r[0][0] > r[1][1] && r[0][0] > r[2][2])
        {
            const float s = std::sqrt(1.f + r[0][0] - r[1][1] - r[2][2]) * 2.f;
            w = (r[1][2] - r[2][1]) / s;
            x = 0.25f * s;
            y = (r[1][0] + r[0][1]) / s;
            z = (r[2][0] + r[0][2]) / s;
        }
        else if (r[1][1] > r[2][2])
        {
            const float s = std::sqrt(1.f + r[1][1] - r[0][0] - r[2][2]) * 2.f;
            w = (r[2][0] - r[0][2]) / s;
            x = (r[1][0] + r[0][1]) / s;
            y = 0.25f * s;
            z = (r[2][1] + r[1][2]) / s;
        }
        else
        {
            const float s = std::sqrt(1.f + r[2][2] - r[0][0] - r[1][1]) * 2.f;
            w = (r[0][1] - r[1][0]) / s;
            x = (r[2][0] + r[0][2]) / s;
            y = (r[2][1] + r[1][2]) / s;
            z = 0.25f * s;
        }
        rotation = {x, y, z, w};
    }

    std::expected<const std::vector<PrimitiveSource> *, Error> meshSources(int meshIndex)
    {
        auto &cached = m_meshSources[static_cast<std::size_t>(meshIndex)];
        if (cached)
            return &*cached;
        const Mesh &mesh = m_document.meshes[static_cast<std::size_t>(meshIndex)];
        std::vector<PrimitiveSource> sources;
        for (std::size_t p = 0; p < mesh.primitives.size(); ++p)
        {
            const std::string context =
                where("meshes", static_cast<std::size_t>(meshIndex)) + "." + where("primitives", p);
            auto source = buildPrimitive(mesh.primitives[p], context);
            if (!source)
                return std::unexpected(source.error());
            if (*source)
                sources.push_back(std::move(**source));
        }
        cached = std::move(sources);
        return &*cached;
    }

    // accessor를 읽고 요소 개수·성분 수를 확인한다.
    std::expected<std::vector<float>, Error> readAttribute(int accessor, int components,
                                                           std::size_t expectedCount,
                                                           std::string_view context,
                                                           std::string_view name) const
    {
        const auto &info = m_document.accessors[static_cast<std::size_t>(accessor)];
        if (info.componentCount != components)
            return std::unexpected(std::string(context) + ": " + std::string(name) + " has the wrong type");
        if (expectedCount != 0 && info.count != expectedCount)
            return std::unexpected(std::string(context) + ": " + std::string(name) +
                                   " count differs from POSITION");
        auto values = ReadAccessorFloats(m_document, accessor);
        if (!values)
            return std::unexpected(std::string(context) + ": " + values.error());
        return values;
    }

    // 그릴 수 없는 primitive(점·선, POSITION 없음)는 경고 후 nullopt.
    std::expected<std::optional<PrimitiveSource>, Error> buildPrimitive(const Primitive &primitive,
                                                                        const std::string &context)
    {
        if (primitive.mode != PrimitiveMode::Triangles && primitive.mode != PrimitiveMode::TriangleStrip &&
            primitive.mode != PrimitiveMode::TriangleFan)
        {
            m_warnings.push_back(context + ": point/line primitives are not drawn");
            return std::nullopt;
        }
        const int positionAccessor = primitive.FindAttribute("POSITION");
        if (positionAccessor == none)
        {
            m_warnings.push_back(context + ": no POSITION, skipped");
            return std::nullopt;
        }
        auto positions = readAttribute(positionAccessor, 3, 0, context, "POSITION");
        if (!positions)
            return std::unexpected(positions.error());
        const std::size_t vertexCount = positions->size() / 3;
        if (!std::ranges::all_of(*positions, [](float value) { return std::isfinite(value); }))
            return std::unexpected(context + ": POSITION contains NaN or infinity");

        PrimitiveSource source;
        source.vertices.resize(vertexCount);
        for (std::size_t v = 0; v < vertexCount; ++v)
            source.vertices[v].position = {(*positions)[v * 3], (*positions)[v * 3 + 1],
                                           (*positions)[v * 3 + 2]};

        if (const int normalAccessor = primitive.FindAttribute("NORMAL"); normalAccessor != none)
        {
            auto normals = readAttribute(normalAccessor, 3, vertexCount, context, "NORMAL");
            if (!normals)
                return std::unexpected(normals.error());
            for (float &value : *normals)
                value = finiteOrZero(value); // 0 법선은 buildSpatialData가 다시 만든다
            for (std::size_t v = 0; v < vertexCount; ++v)
                source.vertices[v].normal = {(*normals)[v * 3], (*normals)[v * 3 + 1], (*normals)[v * 3 + 2]};
        }

        if (primitive.material != none)
            source.material =
                convertMaterial(m_document.materials[static_cast<std::size_t>(primitive.material)]);
        else
            source.material = convertMaterial(Material{}); // glTF 기본 재질: 흰색, metallic 1, roughness 1

        // glTF UV 원점은 이미지 왼쪽 위다. Texture::Sample도 v=0이 첫 행이라 뒤집지 않는다.
        int texCoordSet = 0;
        if (primitive.material != none)
        {
            const auto &matDoc = m_document.materials[static_cast<std::size_t>(primitive.material)];
            const auto &textureRef = matDoc.baseColorTexture;
            texCoordSet = textureRef.texCoord;
            if (textureRef.texture != none)
                source.material.diffuseTexture = texture(textureRef.texture);
            if (matDoc.normalTexture.texture != none)
            {
                if (textureRef.texture == none)
                    texCoordSet = matDoc.normalTexture.texCoord;
                source.material.normalTexture = texture(matDoc.normalTexture.texture);
            }
        }
        if (const int uvAccessor = primitive.FindAttribute("TEXCOORD_" + std::to_string(texCoordSet));
            uvAccessor != none)
        {
            auto uvs = readAttribute(uvAccessor, 2, vertexCount, context, "TEXCOORD");
            if (!uvs)
                return std::unexpected(uvs.error());
            for (float &value : *uvs)
                value = finiteOrZero(value); // 무한대 UV는 wrap 계산에서 NaN이 된다
            for (std::size_t v = 0; v < vertexCount; ++v)
                source.vertices[v].texcoord = {(*uvs)[v * 2], (*uvs)[v * 2 + 1]};
        }
        else if (source.material.diffuseTexture || source.material.normalTexture)
        {
            m_warnings.push_back(context + ": texture without TEXCOORD_" + std::to_string(texCoordSet) +
                                 " ignored");
            source.material.diffuseTexture.reset();
            source.material.normalTexture.reset();
        }

        const int jointsAccessor = primitive.FindAttribute("JOINTS_0");
        const int weightsAccessor = primitive.FindAttribute("WEIGHTS_0");
        if (jointsAccessor != none && weightsAccessor != none)
        {
            auto joints = readAttribute(jointsAccessor, 4, vertexCount, context, "JOINTS_0");
            if (!joints)
                return std::unexpected(joints.error());
            auto weights = readAttribute(weightsAccessor, 4, vertexCount, context, "WEIGHTS_0");
            if (!weights)
                return std::unexpected(weights.error());
            source.skin.joints.resize(vertexCount);
            source.skin.weights.resize(vertexCount);
            for (std::size_t v = 0; v < vertexCount; ++v)
                for (std::size_t k = 0; k < 4; ++k)
                {
                    const float joint = (*joints)[v * 4 + k];
                    if (joint < 0.f || joint > 65535.f)
                        return std::unexpected(context + ": JOINTS_0 value out of range");
                    source.skin.joints[v][k] = static_cast<std::uint16_t>(joint);
                    source.skin.weights[v][k] = (*weights)[v * 4 + k];
                }
        }

        std::vector<std::uint32_t> indices;
        if (primitive.indices != none)
        {
            auto read = ReadAccessorIndices(m_document, primitive.indices);
            if (!read)
                return std::unexpected(context + ": " + read.error());
            indices = std::move(*read);
            if (std::ranges::any_of(indices, [&](std::uint32_t index) { return index >= vertexCount; }))
                return std::unexpected(context + ": index refers to a missing vertex");
        }
        else
        {
            indices.resize(vertexCount);
            std::iota(indices.begin(), indices.end(), 0u);
        }

        // glTF 앞면은 반시계(CCW)다. OBJ 로더와 같은 화면 컬링 규약으로 저장하려고 (a, c, b) 순서로 뒤집는다.
        const auto addTriangle = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c)
        {
            source.indices.push_back(a);
            source.indices.push_back(c);
            source.indices.push_back(b);
        };
        if (primitive.mode == PrimitiveMode::Triangles)
        {
            if (indices.size() % 3 != 0)
                m_warnings.push_back(context + ": trailing indices that do not form a triangle ignored");
            for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
                addTriangle(indices[i], indices[i + 1], indices[i + 2]);
        }
        else if (primitive.mode == PrimitiveMode::TriangleStrip)
        {
            for (std::size_t i = 0; i + 2 < indices.size(); ++i)
                if (i % 2 == 0)
                    addTriangle(indices[i], indices[i + 1], indices[i + 2]);
                else
                    addTriangle(indices[i + 1], indices[i], indices[i + 2]);
        }
        else
        {
            for (std::size_t i = 1; i + 1 < indices.size(); ++i)
                addTriangle(indices[0], indices[i], indices[i + 1]);
        }
        if (source.indices.empty())
        {
            m_warnings.push_back(context + ": no triangles, skipped");
            return std::nullopt;
        }
        return source;
    }

    static TextureWrap toWrap(int mode) noexcept
    {
        switch (mode)
        {
        case 33071:
            return TextureWrap::ClampToEdge;
        case 33648:
            return TextureWrap::MirroredRepeat;
        default:
            return TextureWrap::Repeat;
        }
    }

    // glTF texture = 이미지 + sampler(wrap). 이미지는 한 번만 디코딩하고, wrap이 다른 조합만 복사본을 만든다.
    std::shared_ptr<::Texture> texture(int textureIndex)
    {
        const auto &info = m_document.textures[static_cast<std::size_t>(textureIndex)];
        const Sampler sampler =
            info.sampler != none ? m_document.samplers[static_cast<std::size_t>(info.sampler)] : Sampler{};
        const TextureWrap wrapU = toWrap(sampler.wrapS), wrapV = toWrap(sampler.wrapT);
        auto decoded = decodedImage(info.source, textureIndex);
        if (!decoded)
            return nullptr;
        if (decoded->GetWrapU() == wrapU && decoded->GetWrapV() == wrapV)
            return decoded;
        for (const auto &variant : m_wrapVariants)
            if (variant.first == info.source && variant.second->GetWrapU() == wrapU &&
                variant.second->GetWrapV() == wrapV)
                return variant.second;
        auto copy = std::make_shared<::Texture>(*decoded);
        copy->SetWrap(wrapU, wrapV);
        m_wrapVariants.emplace_back(info.source, copy);
        return copy;
    }

    // 같은 이미지는 한 번만 디코딩해 여러 재질이 공유한다. 실패는 경고로 남기고 텍스처 없이 그린다.
    // 처음 디코딩할 때 그 이미지를 처음 쓴 texture의 wrap을 넣는다(보통 모든 texture가 같다).
    std::shared_ptr<::Texture> decodedImage(int image, int textureIndex)
    {
        if (image == none)
        {
            m_warnings.push_back(where("textures", static_cast<std::size_t>(textureIndex)) +
                                 ": no source image (extension-only textures are not supported)");
            return nullptr;
        }
        const auto slot = static_cast<std::size_t>(image);
        if (m_textureTried[slot])
            return m_textures[slot];
        m_textureTried[slot] = true;
        auto bytes = ReadImageBytes(m_document, image, m_options.file);
        if (!bytes)
        {
            m_warnings.push_back(where("images", slot) + ": " + bytes.error().message);
            return nullptr;
        }
        auto decoded = m_decode(*bytes, m_document.images[slot].mimeType);
        if (!decoded || !*decoded)
        {
            m_warnings.push_back(
                where("images", slot) + ": " +
                (decoded ? std::string("decoder returned no texture") : decoded.error().message));
            return nullptr;
        }
        const auto &info = m_document.textures[static_cast<std::size_t>(textureIndex)];
        const Sampler sampler =
            info.sampler != none ? m_document.samplers[static_cast<std::size_t>(info.sampler)] : Sampler{};
        (*decoded)->SetWrap(toWrap(sampler.wrapS), toWrap(sampler.wrapT));
        m_textures[slot] = std::move(*decoded);
        return m_textures[slot];
    }

    std::expected<std::vector<SkinData>, Error> buildSkins() const
    {
        std::vector<SkinData> skins;
        for (std::size_t i = 0; i < m_document.skins.size(); ++i)
        {
            const Skin &skin = m_document.skins[i];
            SkinData data{skin.name, skin.joints, {}, skin.skeleton};
            data.inverseBindMatrices.assign(skin.joints.size(), SRMath::mat4::identity());
            if (skin.inverseBindMatrices != none)
            {
                auto values = ReadAccessorFloats(m_document, skin.inverseBindMatrices);
                if (!values)
                    return std::unexpected(where("skins", i) + ": " + values.error());
                for (std::size_t j = 0; j < skin.joints.size(); ++j)
                    for (std::size_t e = 0; e < 16; ++e)
                        data.inverseBindMatrices[j].data[e] = (*values)[j * 16 + e]; // 둘 다 열 우선
            }
            skins.push_back(std::move(data));
        }
        return skins;
    }

    // 키 개수가 맞지 않거나 시간이 거꾸로 가는 트랙은 경고 후 버린다(메시는 그대로 쓸 수 있다).
    std::vector<AnimationClip> buildAnimations()
    {
        std::vector<AnimationClip> clips;
        for (std::size_t a = 0; a < m_document.animations.size(); ++a)
        {
            const Animation &animation = m_document.animations[a];
            AnimationClip clip;
            clip.name = animation.name;
            for (std::size_t c = 0; c < animation.channels.size(); ++c)
            {
                const AnimationChannel &channel = animation.channels[c];
                const std::string context = where("animations", a) + "." + where("channels", c);
                if (channel.node == none)
                    continue;
                const AnimationSampler &sampler =
                    animation.samplers[static_cast<std::size_t>(channel.sampler)];
                auto times = ReadAccessorFloats(m_document, sampler.input);
                auto values = ReadAccessorFloats(m_document, sampler.output);
                if (!times || !values)
                {
                    m_warnings.push_back(context + ": " + (!times ? times.error() : values.error()));
                    continue;
                }
                const std::size_t keys = times->size();
                const std::size_t perKey = sampler.interpolation == Interpolation::CubicSpline ? 3 : 1;
                std::size_t components = 0;
                switch (channel.path)
                {
                case AnimationPath::Translation:
                case AnimationPath::Scale:
                    components = 3;
                    break;
                case AnimationPath::Rotation:
                    components = 4;
                    break;
                case AnimationPath::Weights:
                    components = values->size() / (keys * perKey);
                    break;
                }
                const bool ordered = std::ranges::all_of(*times, [](float t) { return std::isfinite(t); }) &&
                                     std::ranges::is_sorted(*times);
                if (components == 0 || values->size() != keys * perKey * components || !ordered)
                {
                    m_warnings.push_back(context + ": keyframe data is inconsistent, track skipped");
                    continue;
                }
                clip.duration = std::max(clip.duration, times->back());
                clip.tracks.push_back({channel.node, channel.path, sampler.interpolation,
                                       static_cast<int>(components), std::move(*times), std::move(*values)});
            }
            clips.push_back(std::move(clip));
        }
        return clips;
    }
};
} // namespace

SRMath::vec3 QuaternionToAxisAngle(const std::array<float, 4> &quaternion) noexcept
{
    float x = quaternion[0], y = quaternion[1], z = quaternion[2], w = quaternion[3];
    const float norm = std::sqrt(x * x + y * y + z * z + w * w);
    if (!(norm > 1e-12f) || !std::isfinite(norm))
        return {0.f, 0.f, 0.f};
    x /= norm, y /= norm, z /= norm, w /= norm;
    // q와 -q는 같은 회전이다. w >= 0 쪽을 골라 각도를 [0, pi]로 둔다.
    if (w < 0.f)
        x = -x, y = -y, z = -z, w = -w;
    const float sinHalf = std::sqrt(x * x + y * y + z * z);
    if (sinHalf < 1e-7f)
        return {0.f, 0.f, 0.f};
    const float angle = 2.f * std::atan2(sinHalf, w);
    const float factor = angle / sinHalf;
    return {x * factor, y * factor, z * factor};
}

std::expected<LoadedScene, AssetLoadError> BuildScene(const Document &document,
                                                      const SceneLoadOptions &options)
{
    return SceneBuilder{document, options}.Build();
}

std::expected<LoadedScene, AssetLoadError> LoadScene(const std::filesystem::path &path,
                                                     const SceneLoadOptions &options)
{
    auto document = LoadFile(path, options.file);
    if (!document)
        return std::unexpected(std::move(document.error()));
    auto scene = BuildScene(*document, options);
    if (!scene && scene.error().path.empty())
        scene.error().path = path;
    return scene;
}
} // namespace sr::gltf

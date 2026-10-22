#include "Graphics/ModelLoader.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <compare>
#include <fstream>
#include <map>
#include <ranges>
#include <string_view>
#include <unordered_map>
#include "Utils/Jobs.h"

#include "Graphics/TextureLoader.h"
#include "Graphics/Model.h"
#include "Graphics/AssetParsing.h"
#include "Graphics/Octree.h"
#include "Graphics/Material.h"
#include "Math/AABB.h"

namespace
{
struct VertexKey
{
    int positionIndex = -1;
    int texcoordIndex = -1;
    int normalIndex = -1;

    auto operator<=>(const VertexKey &) const = default;
};

using sr::asset::parseNumber;
using sr::asset::takeToken;
using sr::asset::trimLeft;

[[nodiscard]] std::expected<SRMath::vec3, std::string_view> parseVec3(std::string_view input) noexcept
{
    auto x = parseNumber<float>(takeToken(input));
    auto y = parseNumber<float>(takeToken(input));
    auto z = parseNumber<float>(takeToken(input));
    if (!x)
        return std::unexpected(x.error());
    if (!y)
        return std::unexpected(y.error());
    if (!z)
        return std::unexpected(z.error());
    return SRMath::vec3{*x, *y, *z};
}

[[nodiscard]] std::expected<SRMath::vec2, std::string_view> parseVec2(std::string_view input) noexcept
{
    auto x = parseNumber<float>(takeToken(input));
    auto y = parseNumber<float>(takeToken(input));
    if (!x)
        return std::unexpected(x.error());
    if (!y)
        return std::unexpected(y.error());
    return SRMath::vec2{*x, *y};
}

[[nodiscard]] std::expected<int, std::string_view> parseObjIndex(std::string_view token) noexcept
{
    if (token.empty())
        return -1;
    auto parsed = parseNumber<int>(token);
    if (!parsed || *parsed <= 0)
        return std::unexpected(token);
    return *parsed - 1;
}

// OBJ face 토큰(v, v/vt, v//vn, v/vt/vn)을 예외 없이 해석한다.
// 토큰 오류를 호출자에 반환해 해당 파일과 행 번호를 덧붙일 수 있게 한다.
[[nodiscard]] std::expected<VertexKey, std::string_view> parseVertexKey(std::string_view token) noexcept
{
    VertexKey key;
    const auto firstSlash = token.find('/');
    const auto secondSlash =
        firstSlash == std::string_view::npos ? std::string_view::npos : token.find('/', firstSlash + 1);

    auto position = parseObjIndex(token.substr(0, firstSlash));
    if (!position)
        return std::unexpected(position.error());
    key.positionIndex = *position;

    if (firstSlash == std::string_view::npos)
        return key;

    const auto texToken = token.substr(
        firstSlash + 1, secondSlash == std::string_view::npos ? secondSlash : secondSlash - firstSlash - 1);
    if (!texToken.empty())
    {
        auto texture = parseObjIndex(texToken);
        if (!texture)
            return std::unexpected(texture.error());
        key.texcoordIndex = *texture;
    }

    if (secondSlash != std::string_view::npos)
    {
        auto normal = parseObjIndex(token.substr(secondSlash + 1));
        if (!normal)
            return std::unexpected(normal.error());
        key.normalIndex = *normal;
    }
    return key;
}

[[nodiscard]] AssetLoadError malformedObj(const std::filesystem::path &path, std::size_t line,
                                          std::string_view token)
{
    return {"Malformed OBJ token: " + std::string(token), path, line};
}

// 파싱 중 상태는 한 파일에만 속한다. 완성된 mesh만 Model에 이동한다.
class ObjParser
{
  public:
    explicit ObjParser(std::filesystem::path path)
        : m_filename(std::move(path)), m_directoryPath(m_filename.parent_path())
    {
    }
    std::expected<std::vector<Mesh>, AssetLoadError> Parse()
    {
        std::ifstream file(m_filename);
        if (!file)
            return std::unexpected(
                AssetLoadError{"Unable to open OBJ file: " + m_filename.string(), m_filename});
        m_meshes.reserve(1000);
        m_positions.reserve(65000);
        m_texcoords.reserve(65000);
        m_normals.reserve(65000);
        std::string line;
        while (std::getline(file, line))
        {
            ++m_lineNumber;
            sr::asset::trimLineEnd(line);
            if (auto result = parseStatement(line); !result)
                return std::unexpected(result.error());
        }
        return std::move(m_meshes);
    }

  private:
    std::filesystem::path m_filename, m_directoryPath;
    std::size_t m_lineNumber = 0;
    std::vector<Mesh> m_meshes;
    std::vector<SRMath::vec3> m_positions, m_normals;
    std::vector<SRMath::vec2> m_texcoords;
    std::unordered_map<std::string, Material> m_materials;
    std::string m_currentMaterialName;
    bool m_newGroupStarted = false;
    std::map<VertexKey, unsigned int> m_vertexCache;

    std::expected<void, AssetLoadError> parseStatement(std::string_view arguments)
    {
        const auto prefix = takeToken(arguments);
        if (prefix.empty() || prefix.starts_with('#'))
            return {};
        // 위치 벡터(v)
        if (prefix == "v")
        {
            auto position = parseVec3(arguments);
            if (!position)
                return std::unexpected(malformedObj(m_filename, m_lineNumber, position.error()));
            m_positions.emplace_back(*position);
        }

        // 텍스처 좌표(vt)
        else if (prefix == "vt")
        {
            auto texcoord = parseVec2(arguments);
            if (!texcoord)
                return std::unexpected(malformedObj(m_filename, m_lineNumber, texcoord.error()));
            // OBJ의 v는 아래에서 위로 증가한다. Texture 행은 위에서 아래 순서이므로 왼쪽 위 원점으로 바꿔 둔다.
            m_texcoords.emplace_back(texcoord->x, 1.0f - texcoord->y);
        }

        // 법선 벡터(vn)
        else if (prefix == "vn")
        {
            auto normal = parseVec3(arguments);
            if (!normal)
                return std::unexpected(malformedObj(m_filename, m_lineNumber, normal.error()));
            m_normals.emplace_back(*normal);
        }

        // 머티리얼 라이브러리 참조(mtllib)
        else if (prefix == "mtllib")
        {
            const std::string mtlFilename(takeToken(arguments));
            const auto mtlPath = m_directoryPath / mtlFilename;
            // OBJ의 MTL 참조는 선택 사항이다. 누락된 라이브러리는 기본 재질로 계속 로드한다.
            if (!std::filesystem::exists(mtlPath))
                return {};

            // 디렉터리 기준 경로를 사용하여 MTL 파싱
            auto loadedMaterials = TextureLoader::LoadMTLFile(mtlPath);
            if (!loadedMaterials)
                return std::unexpected(std::move(loadedMaterials.error()));
            m_materials = std::move(*loadedMaterials);
        }
        // 머티리얼 선택(usemtl)
        else if (prefix == "usemtl")
        {
            m_currentMaterialName = std::string(takeToken(arguments));
        }
        // 그룹 시작(g)
        else if (prefix == "g")
        {
            // 새로운 g 태그를 만나면 플래그를 설정
            m_newGroupStarted = true;
        }
        // 면(face) 정의(f)
        else if (prefix == "f")
        {
            return parseFace(arguments);
        }
        return {};
    }
    void beginMesh()
    {
        // 새로운 메시 그룹이 시작되었거나, 현재 메시가 없거나, 머티리얼이 바뀐 경우
        if (m_meshes.empty() || m_meshes.back().material.name != m_currentMaterialName || m_newGroupStarted)
        {
            m_newGroupStarted = false; // 새로운 그룹 시작 플래그 초기화
            // 새로운 메시 그룹이 시작될 때 m_vertexCache 초기화 ---
            m_vertexCache.clear();

            // 재질과 정점 캐시는 메시 단위이므로 새 그룹은 별도 메시로 만든다.
            m_meshes.emplace_back();
            auto &newMesh = m_meshes.back();

            // 현재 머티리얼 이름으로 머티리얼 할당 (없으면 기본값 / 콜론 분리 폴백)
            if (const auto material = m_materials.find(m_currentMaterialName); material != m_materials.end())
            {
                newMesh.material = material->second;
            }
            else
            {
                size_t colonPosition = m_currentMaterialName.find_last_of(":");

                if (colonPosition != std::string::npos)
                {
                    // 콜론 뒤의 부분 문자열을 잘라냅니다. (예: "Iron_man_leg:red" -> "red")
                    std::string baseMaterialName = m_currentMaterialName.substr(colonPosition + 1);

                    auto fallback = m_materials.find(baseMaterialName);
                    if (fallback != m_materials.end())
                    {
                        // 2차 시도 성공: 잘라낸 이름으로 찾은 재질을 할당합니다.
                        newMesh.material = fallback->second;
                    }
                    else
                    {
                        // 최종 실패: 기본 재질을 할당합니다.
                        newMesh.material = Material{};
                    }
                }
                else
                {
                    // 재질이 정의되지 않은 경우 기본 재질을 사용
                    newMesh.material = Material{};
                }
            }

            // 최근 머테리얼 이름을 현재 매시의 머테리얼 이름으로 변경
            newMesh.material.name = m_currentMaterialName;
        }
    }
    std::expected<unsigned int, AssetLoadError> resolveVertex(const VertexKey &key,
                                                              std::string_view faceToken)
    {
        auto &meshToAddTo = m_meshes.back();
        if (static_cast<std::size_t>(key.positionIndex) >= m_positions.size())
        {
            return std::unexpected(malformedObj(m_filename, m_lineNumber, faceToken));
        }

        // 동일 v/vt/vn 조합이 이미 생성된 적이 있으면 캐시 재사용
        auto it = m_vertexCache.find(key);
        if (it != m_vertexCache.end())
        {
            return it->second;
        }
        else
        {
            Vertex newVertex{};
            newVertex.position = m_positions[static_cast<std::size_t>(key.positionIndex)];
            if (key.texcoordIndex >= 0)
            {
                if (static_cast<std::size_t>(key.texcoordIndex) >= m_texcoords.size())
                    return std::unexpected(malformedObj(m_filename, m_lineNumber, faceToken));
                newVertex.texcoord = m_texcoords[static_cast<std::size_t>(key.texcoordIndex)];
            }
            if (key.normalIndex >= 0)
            {
                if (static_cast<std::size_t>(key.normalIndex) >= m_normals.size())
                    return std::unexpected(malformedObj(m_filename, m_lineNumber, faceToken));
                newVertex.normal = m_normals[static_cast<std::size_t>(key.normalIndex)];
            }

            meshToAddTo.vertices.emplace_back(newVertex);
            const auto newIndex = static_cast<unsigned int>(meshToAddTo.vertices.size() - 1);
            m_vertexCache.emplace(key, newIndex);
            return newIndex;
        }
    }
    std::expected<void, AssetLoadError> parseFace(std::string_view arguments)
    {

        beginMesh();
        auto &meshToAddTo = m_meshes.back();
        std::size_t vertexCount = 0;
        std::array<unsigned int, 4> faceIndices{}; // 기존 기능과 동일하게 삼각형/쿼드 지원

        while (!arguments.empty() && vertexCount < faceIndices.size())
        {
            const auto faceToken = takeToken(arguments);
            if (faceToken.empty())
                break;

            auto key = parseVertexKey(faceToken);
            if (!key)
            {
                // 오류 표시 정책은 Framework가 정한다. 파서는 파일과 행 정보만 전달한다.
                return std::unexpected(malformedObj(m_filename, m_lineNumber, key.error()));
            }

            auto index = resolveVertex(*key, faceToken);
            if (!index)
                return std::unexpected(index.error());
            faceIndices[vertexCount] = *index;
            ++vertexCount;
        }

        // 삼각형/쿼드만 지원한다. 나머지 정점을 버리면 형상이 바뀌므로 n-gon은 오류다.
        if (!trimLeft(arguments).empty())
        {
            return std::unexpected(malformedObj(m_filename, m_lineNumber, "face has more than 4 vertices"));
        }

        if (vertexCount < 3)
        {
            return std::unexpected(malformedObj(m_filename, m_lineNumber, "f"));
        }

        // 기존 화면 컬링 규약에 맞춰 OBJ winding을 뒤집은 삼각 팬을 저장한다.
        for (std::size_t i = 0; i < vertexCount - 2; ++i)
        {
            meshToAddTo.indices.emplace_back(faceIndices[0]);
            meshToAddTo.indices.emplace_back(faceIndices[i + 2]);
            meshToAddTo.indices.emplace_back(faceIndices[i + 1]);
        }
        return {};
    }
};

void generateMissingNormals(Mesh &mesh)
{
    // OBJ의 모든 정점에 유효한 vn이 있는지 확인한다.
    // 하나라도 빠졌다면 일관된 조명을 위해 전체 메시 법선을 생성한다.
    const bool hasNormals =
        std::ranges::all_of(mesh.vertices, [](const Vertex &vertex)
                            { return SRMath::dot(vertex.normal, vertex.normal) > 1e-12f; });

    if (!hasNormals)
    {
        // 각 정점의 법선을 0으로 초기화
        for (auto &vertex : mesh.vertices)
        {
            vertex.normal = SRMath::vec3(0.0f, 0.0f, 0.0f);
        }

        // 파싱 단계에서 검증한 인덱스 세 개가 삼각형 하나를 구성한다.
        for (const auto triangle : mesh.indices | std::views::chunk(3))
        {
            const unsigned int i0 = triangle[0];
            const unsigned int i1 = triangle[1];
            const unsigned int i2 = triangle[2];

            const SRMath::vec3 &v0 = mesh.vertices[i0].position;
            const SRMath::vec3 &v1 = mesh.vertices[i1].position;
            const SRMath::vec3 &v2 = mesh.vertices[i2].position;

            // 렌더링 인덱스는 화면 컬링을 위해 winding을 반전해 저장된다.
            // 자동 법선은 원본 OBJ의 바깥쪽을 향하도록 교차곱 순서를 반대로 사용한다.
            SRMath::vec3 faceNormal = SRMath::cross(v2 - v0, v1 - v0);

            // 0벡터 방지 후 정규화
            float length = SRMath::length(faceNormal);
            if (length > 1e-6f) // 0벡터 방지
            {
                faceNormal = SRMath::normalize(faceNormal);
            }
            else
            {
                faceNormal = SRMath::vec3(0.0f, 0.0f, 0.0f);
            }

            // 정점 법선에 면 법선 누적 (스무딩)
            mesh.vertices[i0].normal = mesh.vertices[i0].normal + faceNormal;
            mesh.vertices[i1].normal = mesh.vertices[i1].normal + faceNormal;
            mesh.vertices[i2].normal = mesh.vertices[i2].normal + faceNormal;
        }

        // 각 정점의 법선을 정규화하여 부드러운 법선(Smooth Normal) 생성
        for (auto &vertex : mesh.vertices)
        {
            float length = SRMath::length(vertex.normal);
            if (length > 1e-6f)
                vertex.normal = SRMath::normalize(vertex.normal);
            else
                vertex.normal = SRMath::vec3(0.0f, 1.0f, 0.0f); // 기본 위쪽 방향
        }
    }
}
AABB buildSpatialData(std::span<Mesh> meshes)
{
    sr::jobs::PerWorker<AABB> localBounds;
    sr::jobs::ParallelFor(std::size_t{0}, meshes.size(),
                          [&](std::size_t first, std::size_t last)
                          {
                              for (auto index = first; index != last; ++index)
                              {
                                  auto &mesh = meshes[index];
                                  generateMissingNormals(mesh);
                                  mesh.localAABB = ComputeLocalAABB(mesh);
                                  localBounds.Local().Encapsulate(mesh.localAABB);
                                  mesh.octree = std::make_unique<Octree>();
                                  mesh.octree->Build(mesh);
                              }
                          });
    AABB bounds;
    for (const AABB &local : localBounds)
        bounds.Encapsulate(local);
    return bounds;
}
} // namespace

std::expected<std::unique_ptr<Model>, AssetLoadError> ModelLoader::LoadOBJ(
    const std::filesystem::path &inputPath)
{
    auto path = inputPath;
    if (!path.has_extension())
        path += ".obj";
    auto meshes = ObjParser{path}.Parse();
    if (!meshes)
        return std::unexpected(meshes.error());

    return FromMeshes(std::move(*meshes));
}

std::unique_ptr<Model> ModelLoader::FromMeshes(std::vector<Mesh> meshes)
{
    auto model = std::make_unique<Model>();
    model->m_meshes = std::move(meshes);
    model->m_localAABB = buildSpatialData(model->m_meshes);
    return model;
}

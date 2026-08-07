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
#include <tbb/blocked_range.h>
#include <tbb/combinable.h>
#include <tbb/parallel_for.h>

#include "Graphics/TextureLoader.h"
#include "Graphics/Model.h"
#include "Graphics/Octree.h"
#include "Graphics/Material.h"
#include "Math/AABB.h"

namespace
{
    // v/vt/vn 조합을 하나의 정점 키로 사용한다. C++20의 defaulted
    // spaceship은 C++11식 수동 사전식 비교 코드를 대체하고 실수를 줄인다.
    struct VertexKey
    {
        int pos_idx = -1;
        int tex_idx = -1;
        int nrm_idx = -1;

        auto operator<=>(const VertexKey&) const = default;
    };

    [[nodiscard]] constexpr std::string_view trim_left(std::string_view text) noexcept
    {
        while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r'))
        {
            text.remove_prefix(1);
        }
        return text;
    }

    [[nodiscard]] constexpr std::string_view take_token(std::string_view& input) noexcept
    {
        input = trim_left(input);
        const auto separator = input.find_first_of(" \t\r");
        const auto token = input.substr(0, separator);
        input = separator == std::string_view::npos ? std::string_view{} : input.substr(separator);
        return token;
    }

    template <typename Number>
    [[nodiscard]] std::expected<Number, std::string_view> parse_number(std::string_view token) noexcept
    {
        if (token.empty()) return std::unexpected(token);
        Number value{};
        const auto [end, error] = std::from_chars(token.data(), token.data() + token.size(), value);
        if (error != std::errc{} || end != token.data() + token.size())
        {
            return std::unexpected(token);
        }
        return value;
    }

    [[nodiscard]] std::expected<SRMath::vec3, std::string_view> parse_vec3(std::string_view input) noexcept
    {
        auto x = parse_number<float>(take_token(input));
        auto y = parse_number<float>(take_token(input));
        auto z = parse_number<float>(take_token(input));
        if (!x) return std::unexpected(x.error());
        if (!y) return std::unexpected(y.error());
        if (!z) return std::unexpected(z.error());
        return SRMath::vec3{ *x, *y, *z };
    }

    [[nodiscard]] std::expected<SRMath::vec2, std::string_view> parse_vec2(std::string_view input) noexcept
    {
        auto x = parse_number<float>(take_token(input));
        auto y = parse_number<float>(take_token(input));
        if (!x) return std::unexpected(x.error());
        if (!y) return std::unexpected(y.error());
        return SRMath::vec2{ *x, *y };
    }

    [[nodiscard]] std::expected<int, std::string_view> parse_obj_index(std::string_view token) noexcept
    {
        if (token.empty()) return -1;
        auto parsed = parse_number<int>(token);
        if (!parsed || *parsed <= 0) return std::unexpected(token);
        return *parsed - 1;
    }

    // OBJ face 토큰(v, v/vt, v//vn, v/vt/vn)을 예외 없이 해석한다.
    // stoi 예외 대신 expected를 써서 파일/행 번호를 로더 계층에서 보존한다.
    [[nodiscard]] std::expected<VertexKey, std::string_view> parse_vertex_key(std::string_view token) noexcept
    {
        VertexKey key;
        const auto firstSlash = token.find('/');
        const auto secondSlash = firstSlash == std::string_view::npos
            ? std::string_view::npos
            : token.find('/', firstSlash + 1);

        auto position = parse_obj_index(token.substr(0, firstSlash));
        if (!position) return std::unexpected(position.error());
        key.pos_idx = *position;

        if (firstSlash == std::string_view::npos) return key;

        const auto texToken = token.substr(firstSlash + 1,
            secondSlash == std::string_view::npos ? secondSlash : secondSlash - firstSlash - 1);
        if (!texToken.empty())
        {
            auto texture = parse_obj_index(texToken);
            if (!texture) return std::unexpected(texture.error());
            key.tex_idx = *texture;
        }

        if (secondSlash != std::string_view::npos)
        {
            auto normal = parse_obj_index(token.substr(secondSlash + 1));
            if (!normal) return std::unexpected(normal.error());
            key.nrm_idx = *normal;
        }
        return key;
    }

    [[nodiscard]] AssetLoadError malformed_obj(const std::filesystem::path& path,
                                                std::size_t line,
                                                std::string_view token)
    {
        return { "Malformed OBJ token: " + std::string(token), path, line };
    }
}

// OBJ 로더: 파일 경로(확장자 없는 베이스 경로)를 받아 Model 구성
std::expected<std::unique_ptr<Model>, AssetLoadError> ModelLoader::LoadOBJ(const std::filesystem::path& inputPath)
{
    auto filename = inputPath;
    if (!filename.has_extension())
        filename += ".obj";
    std::unique_ptr<Model> outModel = std::make_unique<Model>(); // 출력 모델
	outModel->m_meshes.reserve(1000); // 초기 용량 예약

    std::ifstream file(filename); // .obj 파일 열기
    if (!file.is_open())
        return std::unexpected(AssetLoadError{ "Unable to open OBJ file: " + filename.string(), filename });

    // 텍스처/MTL 상대 경로 기반 디렉터리 계산
    // 예: "path\to\model" → "path\to\"
    const auto directoryPath = filename.parent_path();

    // 파일에서 모든 속성(v, vt, vn)을 임시 버퍼에 읽어들입니다.
    std::vector<SRMath::vec3> temp_positions; // v
    std::vector<SRMath::vec2> temp_texcoords; // vt
    std::vector<SRMath::vec3> temp_normals;   // vn

	temp_positions.reserve(65000); // 초기 용량 예약
	temp_texcoords.reserve(65000); // 초기 용량 예약
    temp_normals.reserve(65000);   // 초기 용량 예약
	
    // MTL 파일에서 재질을 읽어들입니다.
	std::unordered_map<std::string, Material> materials;    // MTL 파일에서 읽은 재질들
	std::string currentMaterialName;                        // 현재 사용 중인 재질 이름

	bool newGroupStarted = false; // 새로운 g 태그가 시작되었는지 여부
    
    // 정점 중복 제거를 위한 맵
    // Key: v/vt/vn 인덱스 조합, Value: 최종 정점 버퍼의 인덱스
    std::map<VertexKey, unsigned int> vertexCache;

    std::string line; // 한 줄 버퍼
    std::size_t lineNumber = 0;
    
    // OBJ 파일을 한 줄씩 읽어 파싱
    while (std::getline(file, line))
    {
        ++lineNumber;
        std::string_view arguments = line;
        const std::string_view prefix = take_token(arguments);
        if (prefix.empty() || prefix.starts_with('#')) continue;

        // 위치 벡터(v)
        if (prefix == "v")
        {
            auto position = parse_vec3(arguments);
            if (!position) return std::unexpected(malformed_obj(filename, lineNumber, position.error()));
            temp_positions.emplace_back(*position);
        }

        // 텍스처 좌표(vt)
        else if (prefix == "vt")
        {
            auto texcoord = parse_vec2(arguments);
            if (!texcoord) return std::unexpected(malformed_obj(filename, lineNumber, texcoord.error()));
            temp_texcoords.emplace_back(*texcoord);

        }

        // 법선 벡터(vn)
        else if (prefix == "vn")
        {
            auto normal = parse_vec3(arguments);
            if (!normal) return std::unexpected(malformed_obj(filename, lineNumber, normal.error()));
            temp_normals.emplace_back(*normal);
            
        }

        // 머티리얼 라이브러리 참조(mtllib)
        else if (prefix == "mtllib")
        {
            const std::string mtlFilename(take_token(arguments));
            const auto mtlPath = directoryPath / mtlFilename;
            // OBJ의 MTL 참조는 선택 사항이다. 누락된 라이브러리는 기본 재질로 계속 로드한다.
            if (!std::filesystem::exists(mtlPath))
                continue;

            // 디렉터리 기준 경로를 사용하여 MTL 파싱
            auto loadedMaterials = TextureLoader::LoadMTLFile(mtlPath);
            if (!loadedMaterials)
                return std::unexpected(std::move(loadedMaterials.error()));
            materials = std::move(*loadedMaterials);
        }
        // 머티리얼 선택(usemtl)
        else if (prefix == "usemtl")
        {
            currentMaterialName = std::string(take_token(arguments));
        }
        // 그룹 시작(g)
        else if (prefix == "g")
        {
            // 새로운 g 태그를 만나면 플래그를 설정
            newGroupStarted = true;
        }
        // 면(face) 정의(f)
        else if(prefix == "f")
        {
			// 새로운 메시 그룹이 시작되었거나, 현재 메시가 없거나, 머티리얼이 바뀐 경우
            if (outModel->m_meshes.empty()
                || outModel->m_meshes.back().material.name != currentMaterialName
                || newGroupStarted)
            {
				newGroupStarted = false; // 새로운 그룹 시작 플래그 초기화
                // 새로운 메시 그룹이 시작될 때 vertexCache 초기화 ---
                vertexCache.clear();

                // 모델에 새로운 메시 추가 및 currentMesh 포인터 갱신
                outModel->m_meshes.emplace_back();
                auto& newMesh = outModel->m_meshes.back();

                // 현재 머티리얼 이름으로 머티리얼 할당 (없으면 기본값 / 콜론 분리 폴백)
                if (const auto material = materials.find(currentMaterialName); material != materials.end())
                {
                    // C++17 if-initializer는 조회 iterator의 수명을 분기 안으로
                    // 제한하고 operator[]의 불필요한 두 번째 해시 탐색을 없앤다.
                    newMesh.material = material->second;
                }
                else
                {
                    size_t colon_pos = currentMaterialName.find_last_of(":");

                    if (colon_pos != std::string::npos)
                    {
                        // 콜론 뒤의 부분 문자열을 잘라냅니다. (예: "Iron_man_leg:red" -> "red")
                        std::string baseMaterialName = currentMaterialName.substr(colon_pos + 1);

                        auto fallback_it = materials.find(baseMaterialName);
                        if (fallback_it != materials.end())
                        {
                            // 2차 시도 성공: 잘라낸 이름으로 찾은 재질을 할당합니다.
                            newMesh.material = fallback_it->second;
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
                newMesh.material.name = currentMaterialName;
            }

			auto& meshToAddTo = outModel->m_meshes.back();
            std::size_t vertexCount = 0;
            std::array<unsigned int, 4> faceIndices{}; // 기존 기능과 동일하게 삼각형/쿼드 지원

            while (!arguments.empty() && vertexCount < faceIndices.size())
            {
                const auto faceToken = take_token(arguments);
                if (faceToken.empty()) break;

                auto key = parse_vertex_key(faceToken);
                if (!key)
                {
                    // 로더가 MessageBox를 직접 띄우던 UI 결합을 제거했다. 오류는
                    // expected로 Framework까지 전달되어 한 곳에서 사용자 메시지가 된다.
                    return std::unexpected(malformed_obj(filename, lineNumber, key.error()));
                }

                if (static_cast<std::size_t>(key->pos_idx) >= temp_positions.size())
                {
                    return std::unexpected(malformed_obj(filename, lineNumber, faceToken));
                }

                // 동일 v/vt/vn 조합이 이미 생성된 적이 있으면 캐시 재사용
                auto it = vertexCache.find(*key);
                if (it != vertexCache.end())
                {
                    faceIndices[vertexCount] = it->second;
                }
                else
                {
                    Vertex newVertex{};
                    newVertex.position = temp_positions[static_cast<std::size_t>(key->pos_idx)];
                    if (key->tex_idx >= 0)
                    {
                        if (static_cast<std::size_t>(key->tex_idx) >= temp_texcoords.size())
                            return std::unexpected(malformed_obj(filename, lineNumber, faceToken));
                        newVertex.texcoord = temp_texcoords[static_cast<std::size_t>(key->tex_idx)];
                    }
                    if (key->nrm_idx >= 0)
                    {
                        if (static_cast<std::size_t>(key->nrm_idx) >= temp_normals.size())
                            return std::unexpected(malformed_obj(filename, lineNumber, faceToken));
                        newVertex.normal = temp_normals[static_cast<std::size_t>(key->nrm_idx)];
                    }

                    meshToAddTo.vertices.emplace_back(newVertex);
                    const auto newIndex = static_cast<unsigned int>(meshToAddTo.vertices.size() - 1);
                    vertexCache.emplace(*key, newIndex);
                    faceIndices[vertexCount] = newIndex;
                }
                ++vertexCount;
            }

            // 이 renderer의 고정 face buffer는 삼각형/쿼드 계약이다. 과거에는
            // 다섯 번째 이후 정점을 조용히 버려 손상된 geometry를 만들었으므로
            // 지원하지 않는 n-gon을 행 번호가 있는 오류로 명시한다.
            if (!trim_left(arguments).empty())
            {
                return std::unexpected(
                    malformed_obj(filename, lineNumber, "face has more than 4 vertices"));
            }

            if (vertexCount < 3)
            {
                return std::unexpected(malformed_obj(filename, lineNumber, "f"));
            }

            // 삼각 팬으로 CCW winding을 유지한다.
            for (std::size_t i = 0; i < vertexCount - 2; ++i)
            {
                meshToAddTo.indices.emplace_back(faceIndices[0]);
                meshToAddTo.indices.emplace_back(faceIndices[i + 2]);
                meshToAddTo.indices.emplace_back(faceIndices[i + 1]);
            }
        }
    }
    
    AABB modelAABB; // 모델 전체 AABB (모든 메시 통합용)

    // 메시별 후처리(법선 생성, AABB 계산, 옥트리 빌드)를 병렬 처리
    tbb::combinable<AABB> localAABB([] { return AABB(); }); // 스레드 로컬 AABB

    tbb::parallel_for(tbb::blocked_range<std::size_t>(0, outModel->m_meshes.size()),
        [&](const tbb::blocked_range<std::size_t>& r) {
        for (std::size_t i = r.begin(); i != r.end(); ++i)
        {
            auto& mesh = outModel->m_meshes[i];

            // OBJ의 모든 정점에 유효한 vn이 있는지 확인한다.
            // 하나라도 빠졌다면 일관된 조명을 위해 전체 메시 법선을 생성한다.
            const bool hasNormals = std::ranges::all_of(mesh.vertices, [](const Vertex& vertex) {
                return SRMath::dot(vertex.normal, vertex.normal) > 1e-12f;
                });

            // If there is no Normal vector in OBJ File
            if (!hasNormals)
            {
                // 각 정점의 법선을 0으로 초기화
                for (auto& vertex : mesh.vertices)
                {
                    vertex.normal = SRMath::vec3(0.0f, 0.0f, 0.0f);
                }

                // 인덱스는 세 개가 한 삼각형이라는 구조를 chunk view로 직접
                // 표현한다. C++11식 i += 3과 idx+n 수동 계산을 제거한다.
                for (const auto triangle : mesh.indices | std::views::chunk(3))
                {
                    const unsigned int i0 = triangle[0];
                    const unsigned int i1 = triangle[1];
                    const unsigned int i2 = triangle[2];

                    const SRMath::vec3& v0 = mesh.vertices[i0].position;
                    const SRMath::vec3& v1 = mesh.vertices[i1].position;
                    const SRMath::vec3& v2 = mesh.vertices[i2].position;

                    // 렌더링 인덱스는 화면 컬링을 위해 winding을 반전해 저장된다.
                    // 자동 법선은 원본 OBJ의 바깥쪽을 향하도록 교차곱 순서를 반대로 사용한다.
                    SRMath::vec3 face_normal = SRMath::cross(v2 - v0, v1 - v0);

                    // 0벡터 방지 후 정규화
                    float length = SRMath::length(face_normal);
                    if (length > 1e-6f) // 0벡터 방지
                    {
                        face_normal = SRMath::normalize(face_normal);
                    }
                    else
                    {
                        face_normal = SRMath::vec3(0.0f, 0.0f, 0.0f);
                    }

                    // 정점 법선에 면 법선 누적 (스무딩)
                    mesh.vertices[i0].normal = mesh.vertices[i0].normal + face_normal;
                    mesh.vertices[i1].normal = mesh.vertices[i1].normal + face_normal;
                    mesh.vertices[i2].normal = mesh.vertices[i2].normal + face_normal;
                }

                // 각 정점의 법선을 정규화하여 부드러운 법선(Smooth Normal) 생성
                for (auto& vertex : mesh.vertices)
                {
                    float length = SRMath::length(vertex.normal);
                    if (length > 1e-6f)
                        vertex.normal = SRMath::normalize(vertex.normal);
                    else
                        vertex.normal = SRMath::vec3(0.0f, 1.0f, 0.0f); // 기본 위쪽 방향
                }
            }

            // 메시 AABB 계산 및 모델 전체 통합
            AABB meshAABB = AABB::CreateFromMesh(mesh);
            mesh.localAABB = meshAABB;

			localAABB.local().Encapsulate(meshAABB); // 스레드 로컬 AABB에 통합

            // Octree 생성 및 빌드 (가시화/프러스텀 컬링 최적화)
            mesh.octree = std::make_unique<Octree>();
            mesh.octree->Build(mesh);
		}});

    // 최종적으로 thread-local AABB들을 병합
    localAABB.combine_each([&](const AABB& aabb) {
        modelAABB.Encapsulate(aabb);
        });
    // 최종 계산된 AABB 모델에 저장
    
    outModel->m_localAABB = modelAABB;

    // ifstream은 RAII로 닫힌다. C++11식 명시적 close는 조기 반환 경로를
    // 놓치기 쉬워 제거했다.
    return outModel; // 완성된 모델 반환
}

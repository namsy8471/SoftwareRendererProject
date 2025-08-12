#include "Graphics/ModelLoader.h"
#include <fstream>
#include <sstream>
#include <map>
#include <unordered_map>
#include <omp.h>

#include "Graphics/TextureLoader.h"
#include "Graphics/Model.h"
#include "Graphics/Octree.h"
#include "Graphics/Material.h"
#include "Math/AABB.h"

int face_lines_read = 0; // 디버깅용: 읽은 면(face) 라인 수
int triangles_generated = 0; // 디버깅용: 생성된 삼각형 수

struct VertexKey
{
    int pos_idx = -1;	// v
    int tex_idx = -1;	// vt
    int nrm_idx = -1;	// vn

    // map의 key로 사용하기 위한 비교 연산자
    bool operator<(const VertexKey& other) const
    {
        if (pos_idx < other.pos_idx) return true;
        if (pos_idx > other.pos_idx) return false;
        if (tex_idx < other.tex_idx) return true;
        if (tex_idx > other.tex_idx) return false;
        return nrm_idx < other.nrm_idx;
    }
};

std::unique_ptr<Model> ModelLoader::LoadOBJ(const std::string& filename)
{
    std::unique_ptr<Model> outModel = std::make_unique<Model>();

    std::ifstream file(filename + ".obj");
    if (!file.is_open()) return nullptr;

    // 이 'directoryPath'가 모든 상대 경로의 기준이 됩니다.
    std::string directoryPath = "";
    size_t last_slash_idx = filename.find_last_of("/\\");
    if (std::string::npos != last_slash_idx)
    {
        directoryPath = filename.substr(0, last_slash_idx + 1);
    }

    // 1. 파일에서 모든 속성(v, vt, vn)을 임시 버퍼에 읽어들입니다.
    std::vector<SRMath::vec3> temp_positions;
    std::vector<SRMath::vec2> temp_texcoords;
    std::vector<SRMath::vec3> temp_normals;

	std::unordered_map<std::string, Material> materials;    // MTL 파일에서 읽은 재질들
	std::string currentMaterialName;                        // 현재 사용 중인 재질 이름

	bool newGroupStarted = false; // 새로운 g 태그가 시작되었는지 여부
    
    // 정점 중복 제거를 위한 맵
    // Key: v/vt/vn 인덱스 조합, Value: 최종 정점 버퍼의 인덱스
    std::map<VertexKey, unsigned int> vertexCache;

    std::string line;
    
    while (std::getline(file, line))
    {
        std::stringstream ss(line);
        std::string prefix;
        ss >> prefix; // Read the Prefix(v, vt, vn, f)

        if (prefix == "v")
        {
            SRMath::vec3 pos;
            ss >> pos.x >> pos.y >> pos.z;
            temp_positions.push_back(pos);
        }

        else if (prefix == "vt")
        {
            SRMath::vec2 uv;
            ss >> uv.x >> uv.y;
            temp_texcoords.push_back(uv);

        }

        else if (prefix == "vn")
        {
            SRMath::vec3 nrm;
            ss >> nrm.x >> nrm.y >> nrm.z;
            temp_normals.push_back(nrm);
            
        }

        else if (prefix == "mtllib")
        {
            std::string mtlFilename;
            ss >> mtlFilename;
            materials = TextureLoader::LoadMTLFile(directoryPath + mtlFilename);
        }
        else if (prefix == "usemtl")
        {
            ss >> currentMaterialName;
        }

        else if (prefix == "g")
        {
            // 새로운 g 태그를 만나면 플래그를 설정
            newGroupStarted = true;
        }

        else if(prefix == "f")
        {
			// 새로운 메시 그룹이 시작되었거나, 현재 메시가 없을 때
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

                if(materials.find(currentMaterialName) != materials.end())
                {
                    newMesh.material = materials[currentMaterialName];
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
                        newMesh.material = Material();
                    }
                }

                // 최근 머테리얼 이름을 현재 매시의 머테리얼 이름으로 변경
                newMesh.material.name = currentMaterialName;
            }

			auto& meshToAddTo = outModel->m_meshes.back();
            // 여기서부터 파싱되는 면(face)들은 이 메시 그룹에 속하게 됨
            std::string face_data;
            int vertex_count_in_face = 0;
            unsigned int face_indices[4]; // 쿼드까지 지원한다고 가정

            while (ss >> face_data && vertex_count_in_face < 4)
            {
                VertexKey key;
                try {
                    // 슬래시('/')의 위치를 찾아서 형식을 판별합니다.
                    size_t first_slash = face_data.find('/');
                    size_t second_slash = face_data.find('/', first_slash + 1);

                    if (first_slash == std::string::npos)
                    {
                        // 형식: "v" (예: "123")
                        key.pos_idx = std::stoi(face_data) - 1;
                    }
                    else
                    {
                        // v 인덱스는 항상 첫 슬래시 앞에 있습니다.
                        key.pos_idx = std::stoi(face_data.substr(0, first_slash)) - 1;

                        if (second_slash == std::string::npos)
                        {
                            // 형식: "v/vt" (예: "123/456")
                            key.tex_idx = std::stoi(face_data.substr(first_slash + 1)) - 1;
                        }
                        else
                        {
                            // 두 번째 슬래시가 첫 슬래시 바로 뒤에 있다면 "v//vn" 형식입니다.
                            if (second_slash == first_slash + 1)
                            {
                                // 형식: "v//vn" (예: "123//789")
                                key.nrm_idx = std::stoi(face_data.substr(second_slash + 1)) - 1;
                            }
                            else
                            {
                                // 형식: "v/vt/vn" (예: "123/456/789")
                                key.tex_idx = std::stoi(face_data.substr(first_slash + 1, second_slash - first_slash - 1)) - 1;
                                key.nrm_idx = std::stoi(face_data.substr(second_slash + 1)) - 1;
                            }
                        }
                    }
                }
                catch (const std::exception& e) {
                    // stoi 변환 중 오류가 발생하면 (예: 파일 손상), 해당 면은 건너뜁니다.
                    // std::cerr << "Face parse error: " << e.what() << " on data: " << face_vertex_str << std::endl;
                    continue;
                }

                auto it = vertexCache.find(key);
                if (it != vertexCache.end())
                {
                    face_indices[vertex_count_in_face] = it->second;
                }
                else
                {
                    // 새로운 정점 조합: 최종 정점 버퍼에 새로 추가
                    Vertex new_vertex;
                    if(key.pos_idx >= 0 && key.pos_idx < temp_positions.size()) {
                        new_vertex.position = temp_positions[key.pos_idx];
                    }
                    if (key.tex_idx >= 0 && key.tex_idx < temp_texcoords.size()) {
                        new_vertex.texcoord = temp_texcoords[key.tex_idx];
                    }
                    if (key.nrm_idx >= 0 && key.nrm_idx < temp_normals.size()) {
                        new_vertex.normal = temp_normals[key.nrm_idx];
                    }

                    meshToAddTo.vertices.push_back(new_vertex);
                    unsigned int new_index = meshToAddTo.vertices.size() - 1;
                    vertexCache[key] = new_index;
                    face_indices[vertex_count_in_face] = new_index;
                }
                vertex_count_in_face++;
            }

            // 4. 삼각 분할(Triangulation)하여 최종 인덱스 버퍼에 추가
            // CCW 방향
            for (int i = 0; i < vertex_count_in_face - 2; ++i)
            {
                meshToAddTo.indices.push_back(face_indices[0]);
                meshToAddTo.indices.push_back(face_indices[i + 2]);
                meshToAddTo.indices.push_back(face_indices[i + 1]);
            }
        }
    }
 
    AABB modelAABB;

#pragma omp parallel for
    for (int i = 0 ; i < outModel->m_meshes.size(); i++)
    {
        auto& mesh = outModel->m_meshes[i];

        bool hasNormals = false;
        for(const auto& vertex : mesh.vertices)
        {
            if (vertex.normal != SRMath::vec3(0.0f, 0.0f, 0.0f))
            {
                hasNormals = true;
                break;
            }
		}

        // If there is no Normal vector in OBJ File
        if (!hasNormals || true)
        {
            // 각 정점의 법선을 0으로 초기화
            for (auto& vertex : mesh.vertices)
            {
                vertex.normal = SRMath::vec3(0.0f, 0.0f, 0.0f);
            }

            // 모든 면을 순회하며 면의 법선을 계산하고, 면을 구성하는 정점들에 더해줌
            for (size_t idx = 0; idx < mesh.indices.size(); idx += 3)
            {
                unsigned int i0 = mesh.indices[idx];
                unsigned int i1 = mesh.indices[idx + 1];
                unsigned int i2 = mesh.indices[idx + 2];

                const SRMath::vec3& v0 = mesh.vertices[i0].position;
                const SRMath::vec3& v1 = mesh.vertices[i1].position;
                const SRMath::vec3& v2 = mesh.vertices[i2].position;

                SRMath::vec3 face_normal = SRMath::cross(v1 - v0, v2 - v0);

                float length = SRMath::length(face_normal);
                if (length > 1e-6f) // 0벡터 방지
                {
                    face_normal = SRMath::normalize(face_normal);
                }
                else
                {
                    face_normal = SRMath::vec3(0.0f, 0.0f, 0.0f);
                }


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

        #pragma omp critical
        {
            modelAABB.Encapsulate(meshAABB);
        }

        // Octree 생성 및 빌드
        mesh.octree = std::make_unique<Octree>();
        mesh.octree->Build(mesh);
    }

    // 최종 계산된 AABB 모델에 저장
    outModel->m_localAABB = modelAABB;

    file.close();
    return outModel;
}


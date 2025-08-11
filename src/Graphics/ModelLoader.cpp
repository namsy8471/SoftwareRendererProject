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

struct VertexKey
{
    int pos_idx = -1;	// v
    int tex_idx = -1;	// vt
    int nrm_idx = -1;	// vn

    // map�� key�� ����ϱ� ���� �� ������
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

    // �� 'directoryPath'�� ��� ��� ����� ������ �˴ϴ�.
    std::string directoryPath = "";
    size_t last_slash_idx = filename.find_last_of("/\\");
    if (std::string::npos != last_slash_idx)
    {
        directoryPath = filename.substr(0, last_slash_idx + 1);
    }

    // 1. ���Ͽ��� ��� �Ӽ�(v, vt, vn)�� �ӽ� ���ۿ� �о���Դϴ�.
    std::vector<SRMath::vec3> temp_positions;
    std::vector<SRMath::vec2> temp_texcoords;
    std::vector<SRMath::vec3> temp_normals;

    // ���� ó�� ���� �޽� �׷��� ����Ű�� ������
    Mesh* currentMesh = nullptr;
    std::unordered_map<std::string, Material> materials;
    std::string currentMaterialName;

	bool newGroupStarted = false; // ���ο� g �±װ� ���۵Ǿ����� ����
    
    // ���� �ߺ� ���Ÿ� ���� ��
    // Key: v/vt/vn �ε��� ����, Value: ���� ���� ������ �ε���
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
            // ���ο� g �±׸� ������ �÷��׸� ����
            newGroupStarted = true;
        }

        else if(prefix == "f")
        {
            if (currentMesh == nullptr
                || currentMesh->material.name != currentMaterialName
                || newGroupStarted)
            {
				newGroupStarted = false; // ���ο� �׷� ���� �÷��� �ʱ�ȭ
                // ���ο� �޽� �׷��� ���۵� �� vertexCache �ʱ�ȭ --- 
                vertexCache.clear();

                // �𵨿� ���ο� �޽� �߰� �� currentMesh ������ ����
                outModel->m_meshes.emplace_back();
                currentMesh = &outModel->m_meshes.back();

                if(materials.find(currentMaterialName) != materials.end())
                {
                    currentMesh->material = materials[currentMaterialName];
                }
                else
                {
                    size_t colon_pos = currentMaterialName.find_last_of(":");

                    if (colon_pos != std::string::npos)
                    {
                        // �ݷ� ���� �κ� ���ڿ��� �߶���ϴ�. (��: "Iron_man_leg:red" -> "red")
                        std::string baseMaterialName = currentMaterialName.substr(colon_pos + 1);

                        auto fallback_it = materials.find(baseMaterialName);
                        if (fallback_it != materials.end())
                        {
                            // 2�� �õ� ����: �߶� �̸����� ã�� ������ �Ҵ��մϴ�.
                            currentMesh->material = fallback_it->second;
                        }
                        else
                        {
                            // ���� ����: �⺻ ������ �Ҵ��մϴ�.
                            currentMesh->material = Material{};
                        }
                    }
                    else
                    {
                        // ������ ���ǵ��� ���� ��� �⺻ ������ ���
                        currentMesh->material = Material();
                    }
                }

                // �ֱ� ���׸��� �̸��� ���� �Ž��� ���׸��� �̸����� ����
				currentMesh->material.name = currentMaterialName;
            }

            // ���⼭���� �Ľ̵Ǵ� ��(face)���� �� �޽� �׷쿡 ���ϰ� ��
            std::string face_data;
            int vertex_count_in_face = 0;
            unsigned int face_indices[4]; // ������� �����Ѵٰ� ����

            while (ss >> face_data && vertex_count_in_face < 4)
            {
                VertexKey key;
                std::stringstream face_ss(face_data);
                std::string token;

                // "v/vt/vn" form parcing
                // Position indice
                std::getline(face_ss, token, '/');
                key.pos_idx = std::stoi(token) - 1; // 1 based -> 0 based

                if (std::getline(face_ss, token, '/'))
                {
                    if (!token.empty())
                        key.tex_idx = std::stoi(token) - 1;
                }

                // vn parcer
                if (std::getline(face_ss, token, '/'))
                {
                    if (!token.empty())
                        key.nrm_idx = std::stoi(token) - 1;
                }

                auto it = vertexCache.find(key);
                if (it != vertexCache.end())
                {
                    face_indices[vertex_count_in_face] = it->second;
                }
                else
                {
                    // ���ο� ���� ����: ���� ���� ���ۿ� ���� �߰�
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

                    currentMesh->vertices.push_back(new_vertex);
                    unsigned int new_index = currentMesh->vertices.size() - 1;
                    vertexCache[key] = new_index;
                    face_indices[vertex_count_in_face] = new_index;
                }
                vertex_count_in_face++;
            }

            // 4. �ﰢ ����(Triangulation)�Ͽ� ���� �ε��� ���ۿ� �߰�
            for (int i = 0; i < vertex_count_in_face - 2; ++i)
            {
                currentMesh->indices.push_back(face_indices[0]);
                currentMesh->indices.push_back(face_indices[i + 1]);
                currentMesh->indices.push_back(face_indices[i + 2]);
            }
        }
    }
 
    AABB modelAABB;
    modelAABB.min = SRMath::vec3(std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
    modelAABB.max = SRMath::vec3(std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest());

#pragma omp parallel for
    for (int i = 0 ; i < outModel->m_meshes.size(); i++)
    {
        auto& mesh = outModel->m_meshes[i];

        // If there is no Normal vector in OBJ File
        if (temp_normals.empty())
        {
            // �� ������ ������ 0���� �ʱ�ȭ
            std::vector<SRMath::vec3> face_normals(mesh.vertices.size(), { 0.0f, 0.0f, 0.0f });

            // ��� ���� ��ȸ�ϸ� ���� ������ ����ϰ�, ���� �����ϴ� �����鿡 ������
            for (size_t i = 0; i < mesh.indices.size(); i += 3)
            {
                unsigned int i0 = mesh.indices[i];
                unsigned int i1 = mesh.indices[i + 1];
                unsigned int i2 = mesh.indices[i + 2];

                const SRMath::vec3& v0 = mesh.vertices[i0].position;
                const SRMath::vec3& v1 = mesh.vertices[i1].position;
                const SRMath::vec3& v2 = mesh.vertices[i2].position;

                SRMath::vec3 face_normal = SRMath::normalize(SRMath::cross(v1 - v0, v2 - v0));

                mesh.vertices[i0].normal = mesh.vertices[i0].normal + face_normal;
                mesh.vertices[i1].normal = mesh.vertices[i1].normal + face_normal;
                mesh.vertices[i2].normal = mesh.vertices[i2].normal + face_normal;
            }

            // �� ������ ������ ����ȭ�Ͽ� �ε巯�� ����(Smooth Normal) ����
            for (auto& vertex : mesh.vertices)
            {
                vertex.normal = SRMath::normalize(vertex.normal);
            }
        }

        // �޽� AABB ��� �� �� ��ü ����
        AABB meshAABB = AABB::CreateFromMesh(mesh);
        
        #pragma omp critical
        {
            modelAABB.Encapsulate(meshAABB);
        }

        // Octree ���� �� ����
        mesh.octree = std::make_unique<Octree>();
        mesh.octree->Build(mesh);
    }

    // ���� ���� AABB �𵨿� ����
    outModel->m_localAABB = modelAABB;

    file.close();
    return outModel;
}


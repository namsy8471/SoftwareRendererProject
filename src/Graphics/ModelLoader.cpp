#include "Graphics/ModelLoader.h"
#include <fstream>
#include <sstream>
#include <map>
#include <unordered_map>
#include <tbb/tbb.h>

#include "Core/pch.h"
#include "Graphics/TextureLoader.h"
#include "Graphics/Model.h"
#include "Graphics/Octree.h"
#include "Graphics/Material.h"
#include "Math/AABB.h"

// v/vt/vn ������ �ϳ��� ���� Ű�� ����ϱ� ���� ����ü
struct VertexKey
{
    int pos_idx = -1;	// v: ��ġ �ε��� (1 ��� �� 0 ��� ��ȯ)
    int tex_idx = -1;	// vt: �ؽ�ó ��ǥ �ε���
    int nrm_idx = -1;	// vn: ���� �ε���

    // map�� key�� ����ϱ� ���� �� ������
    // ���� ����: pos_idx �� tex_idx �� nrm_idx
    bool operator<(const VertexKey& other) const
    {
        if (pos_idx < other.pos_idx) return true;
        if (pos_idx > other.pos_idx) return false;
        if (tex_idx < other.tex_idx) return true;
        if (tex_idx > other.tex_idx) return false;
        return nrm_idx < other.nrm_idx;
    }
};

// OBJ �δ�: ���� ���(Ȯ���� ���� ���̽� ���)�� �޾� Model ����
std::unique_ptr<Model> ModelLoader::LoadOBJ(const std::string& filename)
{
    std::unique_ptr<Model> outModel = std::make_unique<Model>(); // ��� ��
	outModel->m_meshes.reserve(1000); // �ʱ� �뷮 ����

    std::ifstream file(filename + ".obj"); // .obj ���� ����
    if (!file.is_open()) return nullptr;   // ���� �� null ��ȯ

    // �ؽ�ó/MTL ��� ��� ��� ���͸� ���
    // ��: "path\to\model" �� "path\to\"
    std::string directoryPath = "";
    size_t last_slash_idx = filename.find_last_of("/\\");
    if (std::string::npos != last_slash_idx)
    {
        directoryPath = filename.substr(0, last_slash_idx + 1);
    }

    // ���Ͽ��� ��� �Ӽ�(v, vt, vn)�� �ӽ� ���ۿ� �о���Դϴ�.
    std::vector<SRMath::vec3> temp_positions; // v
    std::vector<SRMath::vec2> temp_texcoords; // vt
    std::vector<SRMath::vec3> temp_normals;   // vn

	temp_positions.reserve(65000); // �ʱ� �뷮 ����
	temp_texcoords.reserve(65000); // �ʱ� �뷮 ����
    temp_normals.reserve(65000);   // �ʱ� �뷮 ����
	
    // MTL ���Ͽ��� ������ �о���Դϴ�.
	std::unordered_map<std::string, Material> materials;    // MTL ���Ͽ��� ���� ������
	std::string currentMaterialName;                        // ���� ��� ���� ���� �̸�

	bool newGroupStarted = false; // ���ο� g �±װ� ���۵Ǿ����� ����
    
    // ���� �ߺ� ���Ÿ� ���� ��
    // Key: v/vt/vn �ε��� ����, Value: ���� ���� ������ �ε���
    std::map<VertexKey, unsigned int> vertexCache;

    std::string line; // �� �� ����
    
    // OBJ ������ �� �پ� �о� �Ľ�
    while (std::getline(file, line))
    {
        std::stringstream ss(line);
        std::string prefix;
        ss >> prefix; // Read the Prefix(v, vt, vn, f)

        // ��ġ ����(v)
        if (prefix == "v")
        {
            SRMath::vec3 pos;
            ss >> pos.x >> pos.y >> pos.z;
            temp_positions.emplace_back(pos);
        }

        // �ؽ�ó ��ǥ(vt)
        else if (prefix == "vt")
        {
            SRMath::vec2 uv;
            ss >> uv.x >> uv.y;
            temp_texcoords.emplace_back(uv);

        }

        // ���� ����(vn)
        else if (prefix == "vn")
        {
            SRMath::vec3 nrm;
            ss >> nrm.x >> nrm.y >> nrm.z;
            temp_normals.emplace_back(nrm);
            
        }

        // ��Ƽ���� ���̺귯�� ����(mtllib)
        else if (prefix == "mtllib")
        {
            std::string mtlFilename;
            ss >> mtlFilename;
            // ���͸� ���� ��θ� ����Ͽ� MTL �Ľ�
            materials = TextureLoader::LoadMTLFile(directoryPath + mtlFilename);
        }
        // ��Ƽ���� ����(usemtl)
        else if (prefix == "usemtl")
        {
            ss >> currentMaterialName;
        }
        // �׷� ����(g)
        else if (prefix == "g")
        {
            // ���ο� g �±׸� ������ �÷��׸� ����
            newGroupStarted = true;
        }
        // ��(face) ����(f)
        else if(prefix == "f")
        {
			// ���ο� �޽� �׷��� ���۵Ǿ��ų�, ���� �޽ð� ���ų�, ��Ƽ������ �ٲ� ���
            if (outModel->m_meshes.empty()
                || outModel->m_meshes.back().material.name != currentMaterialName
                || newGroupStarted)
            {
				newGroupStarted = false; // ���ο� �׷� ���� �÷��� �ʱ�ȭ
                // ���ο� �޽� �׷��� ���۵� �� vertexCache �ʱ�ȭ ---
                vertexCache.clear();

                // �𵨿� ���ο� �޽� �߰� �� currentMesh ������ ����
                outModel->m_meshes.emplace_back();
                auto& newMesh = outModel->m_meshes.back();

                // ���� ��Ƽ���� �̸����� ��Ƽ���� �Ҵ� (������ �⺻�� / �ݷ� �и� ����)
                if(materials.find(currentMaterialName) != materials.end())
                {
                    newMesh.material = materials[currentMaterialName];
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
                            newMesh.material = fallback_it->second;
                        }
                        else
                        {
                            // ���� ����: �⺻ ������ �Ҵ��մϴ�.
                            newMesh.material = Material{};
                        }
                    }
                    else
                    {
                        // ������ ���ǵ��� ���� ��� �⺻ ������ ���
                        newMesh.material = Material();
                    }
                }

                // �ֱ� ���׸��� �̸��� ���� �Ž��� ���׸��� �̸����� ����
                newMesh.material.name = currentMaterialName;
            }

			auto& meshToAddTo = outModel->m_meshes.back(); // ���� ���� �߰��� Ÿ�� �޽�
            // ���⼭���� �Ľ̵Ǵ� ��(face)���� �� �޽� �׷쿡 ���ϰ� ��
            std::string face_data;
            int vertex_count_in_face = 0;     // �ϳ��� f ���ο� ���Ե� ���� �� (3~4)
            unsigned int face_indices[4];     // ������� �����Ѵٰ� ����

            // f ������ �� ��ū(��: "v", "v/vt", "v//vn", "v/vt/vn") �Ľ�
            while (ss >> face_data && vertex_count_in_face < 4)
            {
                VertexKey key;
                try {
                    // ������('/')�� ��ġ�� ã�Ƽ� ������ �Ǻ��մϴ�.
                    size_t first_slash = face_data.find('/');
                    size_t second_slash = face_data.find('/', first_slash + 1);

                    if (first_slash == std::string::npos)
                    {
                        // ����: "v" (��: "123")
                        key.pos_idx = std::stoi(face_data) - 1;
                    }
                    else
                    {
                        // v �ε����� �׻� ù ������ �տ� �ֽ��ϴ�.
                        key.pos_idx = std::stoi(face_data.substr(0, first_slash)) - 1;

                        if (second_slash == std::string::npos)
                        {
                            // ����: "v/vt" (��: "123/456")
                            key.tex_idx = std::stoi(face_data.substr(first_slash + 1)) - 1;
                        }
                        else
                        {
                            // �� ��° �����ð� ù ������ �ٷ� �ڿ� �ִٸ� "v//vn" �����Դϴ�.
                            if (second_slash == first_slash + 1)
                            {
                                // ����: "v//vn" (��: "123//789")
                                key.nrm_idx = std::stoi(face_data.substr(second_slash + 1)) - 1;
                            }
                            else
                            {
                                // ����: "v/vt/vn" (��: "123/456/789")
                                key.tex_idx = std::stoi(face_data.substr(first_slash + 1, second_slash - first_slash - 1)) - 1;
                                key.nrm_idx = std::stoi(face_data.substr(second_slash + 1)) - 1;
                            }
                        }
                    }
                }
                catch (const std::exception& e) {
                    // stoi ��ȯ �� ������ �߻��ϸ� (��: ���� �ջ�), �ش� ���� �ǳʶݴϴ�.
                    std::wstringstream wss;
                    wss << L"��(Face) �����͸� ó���ϴ� �� ������ �߻��߽��ϴ�.\n\n"
                        << L"���� ����: " << e.what() << L"\n"
                        << L"���� ������: " << face_data.c_str(); // std::string�� const char*�� ��ȯ

                    // �޽��� �ڽ��� ȭ�鿡 ǥ���մϴ�.
                    MessageBoxW(
                        nullptr,             // �θ� ������ �ڵ� (������ nullptr)
                        wss.str().c_str(),   // ǥ���� �޽���
                        L"������ ó�� ����", // �޽��� �ڽ� ����
                        MB_OK | MB_ICONERROR // Ȯ�� ��ư�� ���� ������ ǥ��
                    );
                    continue;
                }

                // ���� v/vt/vn ������ �̹� ������ ���� ������ ĳ�� ����
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

                    meshToAddTo.vertices.emplace_back(new_vertex);
                    unsigned int new_index = static_cast<unsigned int>(meshToAddTo.vertices.size() - 1);
                    vertexCache[key] = new_index;
                    face_indices[vertex_count_in_face] = new_index;
                }
                vertex_count_in_face++;
            }

            // 4. �ﰢ ����(Triangulation)�Ͽ� ���� �ε��� ���ۿ� �߰�
            // CCW ����
            for (int i = 0; i < vertex_count_in_face - 2; ++i)
            {
                meshToAddTo.indices.emplace_back(face_indices[0]);
                meshToAddTo.indices.emplace_back(face_indices[i + 2]);
                meshToAddTo.indices.emplace_back(face_indices[i + 1]);
            }
        }
    }
    
	outModel->m_meshes.shrink_to_fit(); // ���ʿ��� �뷮 ����

    AABB modelAABB; // �� ��ü AABB (��� �޽� ���տ�)

    // �޽ú� ��ó��(���� ����, AABB ���, ��Ʈ�� ���)�� ���� ó��
    tbb::combinable<AABB> localAABB([] { return AABB(); }); // ������ ���� AABB

    tbb::parallel_for(tbb::blocked_range<int>(0, static_cast<int>(outModel->m_meshes.size())),
        [&](const tbb::blocked_range<int>& r) {
        for (int i = r.begin(); i != r.end(); i++)
        {
            auto& mesh = outModel->m_meshes[i];

            // OBJ�� vn�� ������ �����ϴ��� ���� üũ
            bool hasNormals = true;
            for (const auto& vertex : mesh.vertices)
            {
                if (vertex.normal != SRMath::vec3(0.0f, 0.0f, 0.0f))
                {
                    hasNormals = false;
                    break;
                }
            }

            // If there is no Normal vector in OBJ File
            if (!hasNormals)
            {
                // �� ������ ������ 0���� �ʱ�ȭ
                for (auto& vertex : mesh.vertices)
                {
                    vertex.normal = SRMath::vec3(0.0f, 0.0f, 0.0f);
                }

                // ��� ���� ��ȸ�ϸ� ���� ������ ����ϰ�, ���� �����ϴ� �����鿡 ������
                for (size_t idx = 0; idx < mesh.indices.size(); idx += 3)
                {
                    unsigned int i0 = mesh.indices[idx];
                    unsigned int i1 = mesh.indices[idx + 1];
                    unsigned int i2 = mesh.indices[idx + 2];

                    const SRMath::vec3& v0 = mesh.vertices[i0].position;
                    const SRMath::vec3& v1 = mesh.vertices[i1].position;
                    const SRMath::vec3& v2 = mesh.vertices[i2].position;

                    // �ﰢ�� �� ���� (v0->v1) �� (v0->v2)
                    SRMath::vec3 face_normal = SRMath::cross(v1 - v0, v2 - v0);

                    // 0���� ���� �� ����ȭ
                    float length = SRMath::length(face_normal);
                    if (length > 1e-6f) // 0���� ����
                    {
                        face_normal = SRMath::normalize(face_normal);
                    }
                    else
                    {
                        face_normal = SRMath::vec3(0.0f, 0.0f, 0.0f);
                    }

                    // ���� ������ �� ���� ���� (������)
                    mesh.vertices[i0].normal = mesh.vertices[i0].normal + face_normal;
                    mesh.vertices[i1].normal = mesh.vertices[i1].normal + face_normal;
                    mesh.vertices[i2].normal = mesh.vertices[i2].normal + face_normal;
                }

                // �� ������ ������ ����ȭ�Ͽ� �ε巯�� ����(Smooth Normal) ����
                for (auto& vertex : mesh.vertices)
                {
                    float length = SRMath::length(vertex.normal);
                    if (length > 1e-6f)
                        vertex.normal = SRMath::normalize(vertex.normal);
                    else
                        vertex.normal = SRMath::vec3(0.0f, 1.0f, 0.0f); // �⺻ ���� ����
                }
            }

            // �޽� AABB ��� �� �� ��ü ����
            AABB meshAABB = AABB::CreateFromMesh(mesh);
            mesh.localAABB = meshAABB;

			localAABB.local().Encapsulate(meshAABB); // ������ ���� AABB�� ����

            // Octree ���� �� ��� (����ȭ/�������� �ø� ����ȭ)
            mesh.octree = std::make_unique<Octree>();
            mesh.octree->Build(mesh);
		}});

    // ���������� thread-local AABB���� ����
    localAABB.combine_each([&](const AABB& aabb) {
        modelAABB.Encapsulate(aabb);
        });
    // ���� ���� AABB �𵨿� ����
    
    outModel->m_localAABB = modelAABB;

    file.close(); // ���� �ݱ�
    return outModel; // �ϼ��� �� ��ȯ
}

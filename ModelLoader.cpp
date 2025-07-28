#include "ModelLoader.h"
#include <fstream>
#include <sstream>
#include "Model.h"

struct VertexIndex
{
    int pos = -1;	// v
    int tex = -1;	// vt
    int nrm = -1;	// vn
};

std::unique_ptr<Model> ModelLoader::LoadOBJ(const std::string& filepath)
{
    std::unique_ptr<Model> outModel = std::make_unique<Model>();

    std::ifstream file(filepath);
    if (!file.is_open())
    {
        return nullptr;
    }

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
            outModel->m_positions.push_back(pos);
        }

        else if (prefix == "vt")
        {
            SRMath::vec2 uv;
            ss >> uv.x >> uv.y;
            outModel->m_texcoords.push_back(uv);

        }

        else if (prefix == "vn")
        {
            SRMath::vec3 nrm;
            ss >> nrm.x >> nrm.y >> nrm.z;
            outModel->m_normals.push_back(nrm);
            
        }

        else if (prefix == "f")
        {
            std::vector<VertexIndex> faceIndices;

            std::string face_data;
            while (ss >> face_data)
            {
                std::stringstream face_ss(face_data);
                std::string token;
                VertexIndex v_idx;

                // "v/vt/vn" form parcing
                // Position indice
                std::getline(face_ss, token, '/');
                v_idx.pos = std::stoi(token) - 1; // 1 based -> 0 based

                if (std::getline(face_ss, token, '/'))
                {
                    if (!token.empty())
                        v_idx.tex = std::stoi(token) - 1;
                }

                // vn parcer
                if (std::getline(face_ss, token, '/'))
                {
                    if (!token.empty())
                        v_idx.nrm = std::stoi(token) - 1;
                }

                faceIndices.push_back(v_idx);
            }

            for (int i = 0; i < faceIndices.size() - 2; i++)
            {
                outModel->m_pos_indices.push_back(faceIndices[0].pos);
                outModel->m_pos_indices.push_back(faceIndices[i + 1].pos);
                outModel->m_pos_indices.push_back(faceIndices[i + 2].pos);

                if (faceIndices[0].tex != -1)
                {
                    outModel->m_tex_indices.push_back(faceIndices[0].tex);
                    outModel->m_tex_indices.push_back(faceIndices[i + 1].tex);
                    outModel->m_tex_indices.push_back(faceIndices[i + 2].tex);
                }

                if (faceIndices[0].nrm != -1)
                {
                    outModel->m_nrm_indices.push_back(faceIndices[0].nrm);
                    outModel->m_nrm_indices.push_back(faceIndices[i + 1].nrm);
                    outModel->m_nrm_indices.push_back(faceIndices[i + 2].nrm);
                }
            }
        }
    }

    // If there is no Normal vector in OBJ File
    if (outModel->m_normals.empty())
    {
        outModel->m_normals.resize(outModel->m_positions.size(), { 0.0f, 0.0f, 0.0f });
        
        for (size_t i = 0; i < outModel->m_pos_indices.size(); i += 3)
        {
            int idx0 = outModel->m_pos_indices[i];
            int idx1 = outModel->m_pos_indices[i + 1];
            int idx2 = outModel->m_pos_indices[i + 2];
        
            const SRMath::vec3 v0 = outModel->m_positions[idx0];
            const SRMath::vec3 v1 = outModel->m_positions[idx1];
            const SRMath::vec3 v2 = outModel->m_positions[idx2];

            SRMath::vec3 face_nrm = SRMath::normalize(SRMath::cross(v1 - v0, v2 - v0));

            outModel->m_normals[idx0] = outModel->m_normals[idx0] + face_nrm;
            outModel->m_normals[idx1] = outModel->m_normals[idx1] + face_nrm;
            outModel->m_normals[idx2] = outModel->m_normals[idx2] + face_nrm;
        }

        for (size_t i = 0; i < outModel->m_normals.size(); i++)
            outModel->m_normals[i] = SRMath::normalize(outModel->m_normals[i]);

        outModel->m_nrm_indices = outModel->m_pos_indices;
    }

    file.close();
    return outModel;
}


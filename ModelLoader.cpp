#include "ModelLoader.h"
#include <fstream>
#include <sstream>
#include "Model.h"

bool ModelLoader::LoadOBJ(const std::string& filepath, Model& outModel)
{
    std::ifstream file(filepath);
    if (!file.is_open())
    {
        return false;
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
            outModel.positions.push_back(pos);
        }

        else if (prefix == "vn")
        {
            SRMath::vec3 nrm;
            ss >> nrm.x >> nrm.y >> nrm.z;
            outModel.normals.push_back(nrm);
            
        }

        else if (prefix == "f")
        {
            std::string face_data;
            while (ss >> face_data)
            {
                std::stringstream face_ss(face_data);
                std::string token;

                // "v/vt/vn" form parcing
                // Position indice
                std::getline(face_ss, token, '/');
                outModel.pos_indices.push_back(std::stoi(token) - 1); // 1 based -> 0 based

                if (std::getline(face_ss, token, '/'))
                {
                    if (!token.empty())
                        outModel.tex_indices.push_back(std::stoi(token) - 1);
                }

                if (std::getline(face_ss, token, '/'))
                {
                    if (!token.empty())
                        outModel.nrm_indices.push_back(std::stoi(token) - 1);
                }
            }
        }
        
    }

    file.close();
    return true;
}

#pragma once
#include <expected>
#include <filesystem>
#include <memory>
#include <vector>
#include "Utils/AssetLoadError.h"

class Model;
struct Mesh;

class ModelLoader
{
  public:
    // 확장자가 없으면 .obj를 붙인다. 결과는 완성된 mesh를 독점 소유한다.
    // 파일/구문 오류는 경로와 행 번호를 반환하며, 누락 MTL은 기본 재질로 처리한다.
    [[nodiscard]] static std::expected<std::unique_ptr<Model>, AssetLoadError> LoadOBJ(
        const std::filesystem::path &filepath);
    // 이미 삼각형 인덱스를 OBJ와 같은 winding으로 만든 mesh 묶음(glTF 등)에 LoadOBJ와 같은
    // 후처리(누락 법선 생성, mesh/모델 AABB, Octree)를 한다.
    [[nodiscard]] static std::unique_ptr<Model> FromMeshes(std::vector<Mesh> meshes);
};

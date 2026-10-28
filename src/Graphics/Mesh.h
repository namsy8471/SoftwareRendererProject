#pragma once
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include "Math/SRMath.h"
#include "Graphics/Material.h"
#include "Math/AABB.h"

class Octree;

struct Vertex
{
    SRMath::vec3 position; // v
    SRMath::vec2 texcoord; // vt. 왼쪽 위 원점(이미지 첫 행이 v = 0)
    SRMath::vec3 normal;   // vn
};

struct Mesh
{
    std::vector<Vertex> vertices;      // 이 메시에 속한 고유 정점 데이터 (VBO용)
    std::vector<unsigned int> indices; // 정점을 연결해 삼각형을 만드는 방법 (IBO용)
    Material material;                 // 이 메시에 적용할 재질
    AABB localAABB;                    // 이 메시에 적용할 로컬 AABB
    std::unique_ptr<Octree> octree;

    Mesh();
    ~Mesh();
    Mesh(Mesh &&) noexcept;
    Mesh &operator=(Mesh &&) noexcept;

    [[nodiscard]] const AABB &GetLocalAABB() const noexcept { return localAABB; }
    [[nodiscard]] std::span<const SRMath::vec4> GetTangents() const noexcept { return m_tangents; }
    void SetTangents(std::vector<SRMath::vec4> tangents) noexcept { m_tangents = std::move(tangents); }
    void ComputeTangents();

  private:
    // xyz = 정규화 탄젠트, w = handedness(±1). 노멀 맵이 있는 메시에서만 채워 Vertex 크기를 보존한다.
    std::vector<SRMath::vec4> m_tangents;
};

// 정점 위치 전체를 감싸는 로컬 AABB. 정점이 없으면 원점 한 점짜리 상자다.
[[nodiscard]] AABB ComputeLocalAABB(const Mesh &mesh) noexcept;

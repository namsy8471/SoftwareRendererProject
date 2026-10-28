#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <vector>
#include "Graphics/Mesh.h"
#include "Math/AABB.h"
#include "Math/SRMath.h"

class GameObject;
class Model;

namespace sr::gltf
{
struct VertexSkinWeights;
}

// CPU 스켈레탈 스키닝. 프레임 순서(Scene::Update): 애니메이션 포즈 → 계층 transform → SkinnedMesh::Update
// → SceneRenderCollector. 변형 결과는 월드 공간이라 렌더 명령의 world 행렬은 단위 행렬이다.
// 정적 메시의 Vertex·Octree·Renderer 경로는 바뀌지 않는다: 스킨 메시는 Octree를 건너뛰고(정적 정점으로 만든
// 트리라 포즈가 바뀌면 틀린다) 이번 프레임 변형 정점의 AABB 하나로 통째 컬링한다.
namespace sr::anim
{
// 정점 하나의 관절 4개와 가중치. joints는 SkinnedMesh 관절 목록 안의 인덱스다. 생성 시 범위를 검사하고
// 가중치 합을 1로 맞춘다(합이 0이면 관절 0에 1).
struct SkinInfluence
{
    std::array<std::uint16_t, 4> joints{};
    std::array<float, 4> weights{};
};

namespace detail
{
// out[i] = (Σ w_k · M[j_k]) · in[i]. 위치는 w=1, 법선은 w=0으로 같은 섞인 행렬을 쓴다(정규화는 렌더러가 한다.
// 관절에 비균등 크기가 있으면 법선이 조금 기울 수 있다). texcoord는 건드리지 않는다. bounds를 위치까지 넓힌다.
// 스칼라(SSE) 기준 구현과 AVX 구현은 같은 연산 순서라 결과 비트가 같다.
void SkinVerticesScalar(std::span<const Vertex> in, std::span<const SkinInfluence> influences,
                        std::span<const SRMath::mat4> jointMatrices, std::span<Vertex> out, AABB &bounds) noexcept;
void SkinVerticesAVX(std::span<const Vertex> in, std::span<const SkinInfluence> influences,
                     std::span<const SRMath::mat4> jointMatrices, std::span<Vertex> out, AABB &bounds) noexcept;
} // namespace detail

// AVX가 있으면 AVX 구현, 아니면 스칼라. 정점이 많으면 sr::jobs로 나눠 병렬 처리한다. 결과는 병렬 여부와
// 무관하게 같다.
void SkinVertices(std::span<const Vertex> in, std::span<const SkinInfluence> influences,
                  std::span<const SRMath::mat4> jointMatrices, std::span<Vertex> out, AABB &bounds);

class SkinnedMesh
{
  public:
    struct Primitive
    {
        const Mesh *source = nullptr; // 소유 GameObject의 Model 안 메시(인덱스·재질·바인드 정점)
        Mesh deformed;                // 이번 프레임 월드 공간 정점. indices/octree는 비어 있다
        std::vector<SkinInfluence> influences; // 비어 있으면 관절 없이 메시 node의 world 행렬로만 옮긴다
        AABB worldBounds;
    };

    // model의 메시 i와 weights[i]가 대응한다(GltfLoader의 meshSkinWeights 한 줄). joints와 inverseBind는 같은
    // 길이이며 joints는 같은 계층 안의 GameObject다(소유하지 않는다). 관절 인덱스가 범위 밖이면 오류다.
    [[nodiscard]] static std::expected<std::unique_ptr<SkinnedMesh>, std::string> Create(
        const Model &model, std::span<const sr::gltf::VertexSkinWeights> weights,
        std::vector<std::weak_ptr<GameObject>> joints, std::vector<SRMath::mat4> inverseBind);

    // 관절 행렬(관절 world × inverse bind)을 만들고 모든 primitive를 변형한다. 관절 transform이 이미
    // 갱신된 뒤(Scene::Update의 계층 갱신 뒤) 프레임 스레드에서 부른다. 사라진 관절은 단위 행렬로 본다.
    void Update(const SRMath::mat4 &ownerWorld);

    [[nodiscard]] std::span<const Primitive> GetPrimitives() const noexcept { return m_primitives; }
    [[nodiscard]] std::span<const Vertex> GetDeformedVertices(std::size_t primitiveIndex = 0) const noexcept
    {
        return primitiveIndex < m_primitives.size()
                   ? std::span<const Vertex>{m_primitives[primitiveIndex].deformed.vertices}
                   : std::span<const Vertex>{};
    }
    [[nodiscard]] const AABB &GetWorldAABB() const noexcept { return m_worldBounds; }
    [[nodiscard]] std::size_t GetJointCount() const noexcept { return m_joints.size(); }
    [[nodiscard]] std::size_t GetVertexCount() const noexcept;

  private:
    std::vector<Primitive> m_primitives;
    std::vector<std::weak_ptr<GameObject>> m_joints;
    std::vector<SRMath::mat4> m_inverseBind;
    std::vector<SRMath::mat4> m_jointMatrices;
    AABB m_worldBounds;

    void updatePrimitive(Primitive &primitive, const SRMath::mat4 &ownerWorld) const;
};
} // namespace sr::anim

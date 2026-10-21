#include "Animation/Skinning.h"
#include "Graphics/GltfLoader.h"
#include "Graphics/Model.h"
#include "Math/SIMD.h"
#include "Scene/GameObject.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <tbb/blocked_range.h>
#include <tbb/combinable.h>
#include <tbb/parallel_for.h>

namespace sr::anim
{
namespace detail
{
void SkinVerticesScalar(std::span<const Vertex> in, std::span<const SkinInfluence> influences,
                        std::span<const SRMath::mat4> jointMatrices, std::span<Vertex> out, AABB &bounds) noexcept
{
    __m128 boundsMin = bounds.min.m128, boundsMax = bounds.max.m128;
    const __m128 xyzMask = _mm_castsi128_ps(_mm_set_epi32(0, -1, -1, -1));
    for (std::size_t i = 0; i < in.size(); ++i)
    {
        const SkinInfluence &influence = influences[i];
        // 4개 관절 행렬을 가중치로 섞는다. 순서(k = 0..3, 열마다 0에서 누적)는 AVX 구현과 같아야 한다.
        __m128 c0 = _mm_setzero_ps(), c1 = _mm_setzero_ps(), c2 = _mm_setzero_ps(), c3 = _mm_setzero_ps();
        for (std::size_t k = 0; k < 4; ++k)
        {
            // 가중치 0 슬롯은 건너뛴다(대부분 정점은 관절 1~2개). 두 구현이 같은 슬롯을 건너뛰어야 비트가 같다.
            if (influence.weights[k] == 0.f)
                continue;
            const SRMath::mat4 &m = jointMatrices[influence.joints[k]];
            const __m128 w = _mm_set1_ps(influence.weights[k]);
            c0 = _mm_add_ps(c0, _mm_mul_ps(m.m128[0], w));
            c1 = _mm_add_ps(c1, _mm_mul_ps(m.m128[1], w));
            c2 = _mm_add_ps(c2, _mm_mul_ps(m.m128[2], w));
            c3 = _mm_add_ps(c3, _mm_mul_ps(m.m128[3], w));
        }
        const Vertex &source = in[i];
        // 위치 = (c0·x + c1·y) + (c2·z + c3). AVX 구현의 lo/hi 합과 같은 묶음이다.
        const __m128 positionXY = _mm_add_ps(_mm_mul_ps(c0, _mm_set1_ps(source.position.x)),
                                             _mm_mul_ps(c1, _mm_set1_ps(source.position.y)));
        const __m128 positionZW = _mm_add_ps(_mm_mul_ps(c2, _mm_set1_ps(source.position.z)), c3);
        const __m128 position = _mm_and_ps(_mm_add_ps(positionXY, positionZW), xyzMask);
        const __m128 normalXY = _mm_add_ps(_mm_mul_ps(c0, _mm_set1_ps(source.normal.x)),
                                           _mm_mul_ps(c1, _mm_set1_ps(source.normal.y)));
        const __m128 normalZW =
            _mm_add_ps(_mm_mul_ps(c2, _mm_set1_ps(source.normal.z)), _mm_mul_ps(c3, _mm_setzero_ps()));
        const __m128 normal = _mm_and_ps(_mm_add_ps(normalXY, normalZW), xyzMask);
        out[i].position.m128 = position;
        out[i].normal.m128 = normal;
        boundsMin = _mm_min_ps(boundsMin, position);
        boundsMax = _mm_max_ps(boundsMax, position);
    }
    bounds.min.m128 = _mm_and_ps(boundsMin, xyzMask);
    bounds.max.m128 = _mm_and_ps(boundsMax, xyzMask);
}
} // namespace detail

namespace
{
using SkinFunction = void (*)(std::span<const Vertex>, std::span<const SkinInfluence>,
                              std::span<const SRMath::mat4>, std::span<Vertex>, AABB &) noexcept;

SkinFunction selectSkinFunction() noexcept
{
    static const SkinFunction function =
        SRMath::SIMD::avx_available() ? &detail::SkinVerticesAVX : &detail::SkinVerticesScalar;
    return function;
}

// 이보다 작은 메시는 한 스레드로 처리한다. 작업 분배 비용이 변형 비용보다 크다.
constexpr std::size_t parallelGrain = 2048;
} // namespace

void SkinVertices(std::span<const Vertex> in, std::span<const SkinInfluence> influences,
                  std::span<const SRMath::mat4> jointMatrices, std::span<Vertex> out, AABB &bounds)
{
    const SkinFunction skin = selectSkinFunction();
    const std::size_t count = std::min({in.size(), influences.size(), out.size()});
    if (count <= parallelGrain)
    {
        skin(in.first(count), influences.first(count), jointMatrices, out.first(count), bounds);
        return;
    }
    // min/max는 순서와 무관하므로 조각을 어떤 worker가 처리해도 결과가 같다.
    tbb::combinable<AABB> partial;
    tbb::parallel_for(tbb::blocked_range<std::size_t>{0, count, parallelGrain},
                      [&](const tbb::blocked_range<std::size_t> &range)
                      {
                          const std::size_t size = range.size();
                          skin(in.subspan(range.begin(), size), influences.subspan(range.begin(), size),
                               jointMatrices, out.subspan(range.begin(), size), partial.local());
                      });
    partial.combine_each([&](const AABB &local) { bounds.Encapsulate(local); });
}

std::expected<std::unique_ptr<SkinnedMesh>, std::string> SkinnedMesh::Create(
    const Model &model, std::span<const sr::gltf::VertexSkinWeights> weights,
    std::vector<std::weak_ptr<GameObject>> joints, std::vector<SRMath::mat4> inverseBind)
{
    if (joints.empty() || joints.size() != inverseBind.size())
        return std::unexpected("skin needs one inverse bind matrix per joint");
    if (joints.size() > 65536)
        return std::unexpected("skin has too many joints");

    auto result = std::make_unique<SkinnedMesh>();
    const std::span<const Mesh> meshes = model.GetMeshes();
    result->m_primitives.resize(meshes.size());
    for (std::size_t m = 0; m < meshes.size(); ++m)
    {
        Primitive &primitive = result->m_primitives[m];
        primitive.source = &meshes[m];
        primitive.deformed.vertices = meshes[m].vertices; // texcoord는 바뀌지 않으므로 한 번만 복사한다
        if (m >= weights.size() || weights[m].joints.empty())
            continue;
        const auto &source = weights[m];
        if (source.joints.size() != meshes[m].vertices.size() || source.weights.size() != source.joints.size())
            return std::unexpected("skin weights do not match the mesh vertices");
        primitive.influences.resize(source.joints.size());
        for (std::size_t v = 0; v < source.joints.size(); ++v)
        {
            SkinInfluence &influence = primitive.influences[v];
            float sum = 0.f;
            for (std::size_t k = 0; k < 4; ++k)
            {
                float weight = source.weights[v][k];
                if (!std::isfinite(weight) || weight < 0.f)
                    weight = 0.f;
                const std::uint16_t joint = source.joints[v][k];
                if (weight > 0.f && joint >= joints.size())
                    return std::unexpected("skin joint index " + std::to_string(joint) + " is out of range");
                // 가중치 0인 슬롯은 범위 밖 인덱스여도 무시해도 되지만, 커널이 분기 없이 읽도록 0번으로 바꾼다.
                influence.joints[k] = weight > 0.f ? joint : std::uint16_t{0};
                influence.weights[k] = weight;
                sum += weight;
            }
            if (sum > 0.f)
                for (float &weight : influence.weights)
                    weight /= sum;
            else
                influence.weights = {1.f, 0.f, 0.f, 0.f};
        }
    }
    result->m_joints = std::move(joints);
    result->m_inverseBind = std::move(inverseBind);
    result->m_jointMatrices.assign(result->m_joints.size(), SRMath::mat4::identity());
    return result;
}

void SkinnedMesh::Update(const SRMath::mat4 &ownerWorld)
{
    for (std::size_t j = 0; j < m_joints.size(); ++j)
    {
        const auto joint = m_joints[j].lock();
        m_jointMatrices[j] = joint ? SRMath::mat4(joint->GetWorldMatrix() * m_inverseBind[j]) : m_inverseBind[j];
    }

    // primitive끼리는 독립이다. 작은 primitive가 많은 모델(BrainStem: 59개, 각 2천 정점 미만)도 코어를 나눠
    // 쓰도록 primitive 단위로 병렬화하고, 큰 primitive는 SkinVertices 안에서 다시 나뉜다.
    tbb::parallel_for(tbb::blocked_range<std::size_t>{0, m_primitives.size(), 1},
                      [&](const tbb::blocked_range<std::size_t> &range)
                      {
                          for (std::size_t p = range.begin(); p != range.end(); ++p)
                              updatePrimitive(m_primitives[p], ownerWorld);
                      });
    // 합치기는 primitive 순서대로 해서 결과가 worker 배치와 무관하다.
    m_worldBounds = AABB{};
    for (const Primitive &primitive : m_primitives)
        if (primitive.worldBounds.IsValid())
            m_worldBounds.Encapsulate(primitive.worldBounds);
}

void SkinnedMesh::updatePrimitive(Primitive &primitive, const SRMath::mat4 &ownerWorld) const
{
    primitive.worldBounds = AABB{};
    if (!primitive.influences.empty())
    {
        SkinVertices(primitive.source->vertices, primitive.influences, m_jointMatrices, primitive.deformed.vertices,
                     primitive.worldBounds);
        return;
    }
    // 관절 정보가 없는 primitive는 메시 node에 고정된 강체로 그린다.
    const SRMath::mat4 normalMatrix = SRMath::inverse_transpose(ownerWorld).value_or(SRMath::mat4::identity());
    for (std::size_t v = 0; v < primitive.source->vertices.size(); ++v)
    {
        const Vertex &source = primitive.source->vertices[v];
        Vertex &target = primitive.deformed.vertices[v];
        target.position = SRMath::vec3(ownerWorld * SRMath::vec4(source.position, 1.f));
        target.normal = SRMath::vec3(normalMatrix * SRMath::vec4(source.normal, 0.f));
        primitive.worldBounds.Encapsulate(target.position);
    }
}

std::size_t SkinnedMesh::GetVertexCount() const noexcept
{
    std::size_t count = 0;
    for (const auto &primitive : m_primitives)
        count += primitive.deformed.vertices.size();
    return count;
}
} // namespace sr::anim

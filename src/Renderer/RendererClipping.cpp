#include "Renderer/Renderer.h"
#include <utility>

// 속성(Attribute) 보간 함수
ShadedVertex Renderer::interpolate(const ShadedVertex &v0, const ShadedVertex &v1, float t)
{
    // 입력 정점은 다음 변에서도 재사용한다. Win32 Release의 복합 vec 임시값 경로에서
    // 입력 훼손이 재현되어, 읽기 전용 SIMD 값으로 sub → mul → add 순서를 명시한다.
    const __m128 amount = _mm_set1_ps(t);
    const auto interpolateLanes = [amount](__m128 from, __m128 to)
    { return _mm_add_ps(from, _mm_mul_ps(_mm_sub_ps(to, from), amount)); };
    ShadedVertex result;
    // 원근 나누기 전의 동차 좌표이므로 w도 함께 보간한다.
    result.posClip.m128 = interpolateLanes(v0.posClip.m128, v1.posClip.m128);

    // t는 클립 평면까지의 동차 좌표 거리에서 구한 선분 매개변수다.
    // 이 단계에서는 모든 속성을 선형 보간하고, 1/w 보정은 래스터 단계에서만 한다.
    result.posWorld.m128 = interpolateLanes(v0.posWorld.m128, v1.posWorld.m128);
    result.normalWorld.m128 = interpolateLanes(v0.normalWorld.m128, v1.normalWorld.m128);
    result.texcoord.m128 = interpolateLanes(v0.texcoord.m128, v1.texcoord.m128);

    return result;
}

// 하나의 평면으로 폴리곤을 클리핑하는 함수
void Renderer::clipPolygonAgainstPlane(ClipBuffer &outVertices, const ClipBuffer &inVertices,
                                       const __m128 &plane)
{
    outVertices.clear();
    if (inVertices.empty())
        return;

    // SIMD 최적화를 위해 0 벡터를 미리 생성
    const __m128 zero = _mm_setzero_ps();

    for (size_t i = 0; i < inVertices.size(); ++i)
    {
        const ShadedVertex &currentVertex = inVertices[i];
        const ShadedVertex &previousVertex = inVertices[i == 0 ? inVertices.size() - 1 : i - 1];

        // 각 정점의 w 값에 plane_sign을 곱한 값과, 특정 축(axis) 값을 비교합니다.
        // 예: Left Plane (w + x >= 0) -> axis=0(x), sign=1. dist = w + x

        // --- SIMD 거리 계산 ---
        __m128 currentPosPs = _mm_load_ps(&currentVertex.posClip.x);
        __m128 prevPosPs = _mm_load_ps(&previousVertex.posClip.x);

        __m128 distsPs = _mm_dp_ps(currentPosPs, plane, 0xF1); // dists_ps = [current_dist, ?, ?, ?]
        distsPs = _mm_add_ps(distsPs,
                             _mm_dp_ps(prevPosPs, plane, 0xF2)); // dists_ps = [current_dist, prev_dist, ?, ?]

        // 현재/직전 정점의 평면 안쪽 여부를 같은 SIMD 값에서 얻는다.
        __m128 insideMaskPs = _mm_cmpge_ps(distsPs, zero); // [c_inside_mask, p_inside_mask, ?, ?]

        // 하위 두 비트가 현재/직전 정점의 분류다.
        int insideMask = _mm_movemask_ps(insideMaskPs);

        bool isCurrentInside = insideMask & 1;
        bool isPrevInside = insideMask & 2;

        if (isCurrentInside != isPrevInside)
        {
            // 교차점 계산은 스칼라로 수행 (SIMD->스칼라 변환이 필요하므로)
            float currentDist = _mm_cvtss_f32(distsPs);
            float prevDist = _mm_cvtss_f32(_mm_shuffle_ps(distsPs, distsPs, _MM_SHUFFLE(1, 1, 1, 1)));

            // 두 정점이 평면을 기준으로 서로 다른 쪽에 있으면 교차점을 계산
            float t = prevDist / (prevDist - currentDist);
            ShadedVertex intersection = interpolate(previousVertex, currentVertex, t);
            outVertices.emplace_back(intersection);
        }

        if (isCurrentInside)
        {
            // 현재 정점이 평면 안쪽에 있으면 결과에 추가
            outVertices.emplace_back(currentVertex);
        }
    }
}

enum class ClipPlane : std::uint8_t
{
    left = 1u << 0,
    right = 1u << 1,
    bottom = 1u << 2,
    top = 1u << 3,
    nearPlane = 1u << 4,
    farPlane = 1u << 5
};

// 절두체 평면에 대한 아웃코드 계산 함수
// computeOutcode is inlined in Renderer.h

// 삼각형을 6개의 절두체 평면으로 클리핑하는 메인 함수
void Renderer::clipTriangle(ClipBuffer &outVertices, const ShadedVertex &v0, const ShadedVertex &v1,
                            const ShadedVertex &v2, ClipBuffer &buffer1, ClipBuffer &buffer2)
{
    outVertices.clear();
    buffer1.clear();
    buffer1.emplace_back(v0);
    buffer1.emplace_back(v1);
    buffer1.emplace_back(v2);

    auto *input = &buffer1;
    auto *output = &buffer2;

    // 6개의 절두체 평면에 대해 클리핑을 수행합니다.
    // _mm_set_ps의 인자 순서는 (w, z, y, x)다.
    const __m128 leftPlane = _mm_set_ps(1.0f, 0.0f, 0.0f, 1.0f);   //  x + w >= 0
    const __m128 rightPlane = _mm_set_ps(1.0f, 0.0f, 0.0f, -1.0f); // -x + w >= 0
    const __m128 bottomPlane = _mm_set_ps(1.0f, 0.0f, 1.0f, 0.0f); //  y + w >= 0
    const __m128 topPlane = _mm_set_ps(1.0f, 0.0f, -1.0f, 0.0f);   // -y + w >= 0
    const __m128 nearPlane = _mm_set_ps(1.0f, 1.0f, 0.0f, 0.0f);   //  z + w >= 0
    const __m128 farPlane = _mm_set_ps(1.0f, -1.0f, 0.0f, 0.0f);   // -z + w >= 0

    // Near Plane  ( w + z >= 0 )
    clipPolygonAgainstPlane(*output, *input, nearPlane);
    if (output->empty())
        return; // 클리핑 결과가 비어있으면 더 이상 진행하지 않습니다.
    std::swap(input, output);

    // Far Plane   ( w - z >= 0 )
    clipPolygonAgainstPlane(*output, *input, farPlane);
    if (output->empty())
        return;
    std::swap(input, output);

    // Left Plane  ( w + x >= 0 )
    clipPolygonAgainstPlane(*output, *input, leftPlane);
    if (output->empty())
        return;
    std::swap(input, output);

    // Right Plane ( w - x >= 0 )
    clipPolygonAgainstPlane(*output, *input, rightPlane);
    if (output->empty())
        return;
    std::swap(input, output);

    // Bottom Plane( w + y >= 0 )
    clipPolygonAgainstPlane(*output, *input, bottomPlane);
    if (output->empty())
        return;
    std::swap(input, output);

    // Top Plane   ( w - y >= 0 )
    clipPolygonAgainstPlane(*output, *input, topPlane);
    if (output->empty())
        return;
    std::swap(input, output);

    outVertices = *input; // 최종 결과를 outVertices에 저장
}

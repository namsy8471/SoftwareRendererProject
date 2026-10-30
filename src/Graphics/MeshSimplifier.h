#pragma once
#include <cstdint>
#include <span>
#include <vector>

struct Vertex;
struct Mesh;
struct MeshLod;

namespace sr::lod
{
// 메시 LOD 사슬을 만드는 설정. 비율은 원본 삼각형 수 대비 목표이며 큰 값부터 준다.
struct LodChainSettings
{
    std::span<const float> triangleRatios;
    // 이 수보다 삼각형이 적은 메시는 LOD를 만들지 않는다(줄여도 이득이 작다).
    std::size_t minTriangles = 64;
    // 한 단계가 이전 단계보다 이 비율 이상 남으면(거의 못 줄였으면) 사슬을 멈춘다.
    float minReduction = 0.85f;
};

// Garland-Heckbert quadric 오차로 정점을 이웃 정점 위로 접는(half-edge collapse) 단순화.
// 정점 버퍼는 그대로 두고 인덱스만 새로 만든다. 그래서 LOD도 같은 Mesh::vertices를 가리키고,
// 렌더러의 정점 캐시·명령 형식이 바뀌지 않는다.
// 정점 종류:
// - 내부 다양체 정점: 이웃 아무 정점 위로나 접을 수 있다.
// - 열린 경계 정점(경계 고리 위 정점): 같은 경계 고리의 이웃 위로만 접는다. 경계선 모양은 변에 수직인
//   평면 quadric으로 지킨다. 그래서 경계가 안쪽으로 줄어들거나 구멍이 생기지 않는다.
// - 고정: UV/법선 이음새(같은 위치에 정점이 여러 개), 비다양체, lockedVertices로 받은 정점
//   (다른 메시와 같은 위치를 쓰는 정점). 두 메시가 서로 다른 LOD를 골라도 맞닿는 가장자리가 그대로라
//   틈(crack)이 생기지 않는다.
// 결과 오차는 메시 로컬 단위의 표면 편차(quadric 평균 제곱 거리의 제곱근)이며 단계마다 커진다.
[[nodiscard]] std::vector<MeshLod> BuildLodChain(std::span<const Vertex> vertices,
                                                 std::span<const unsigned int> indices,
                                                 const LodChainSettings &settings,
                                                 std::span<const std::uint8_t> lockedVertices = {});

// 메시마다 정점 수만큼의 0/1 배열. 1 = 다른 메시도 같은 위치에 정점을 둔다(두 메시가 맞닿는 가장자리).
// BuildLodChain의 lockedVertices로 넘겨 그 정점을 고정하면 메시마다 다른 LOD를 골라도 틈이 생기지 않는다.
[[nodiscard]] std::vector<std::vector<std::uint8_t>> FindSharedPositions(std::span<const Mesh> meshes);
} // namespace sr::lod

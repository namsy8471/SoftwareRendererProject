#pragma once

#include <array>
#include "Graphics/GltfLoader.h"

// glTF 키프레임 트랙 샘플링. 시간은 초, 범위 밖은 첫/마지막 키 값으로 고정한다(반복은 Animator가 처리).
//   - STEP: 직전 키 값.
//   - LINEAR: 성분별 선형. rotation은 짧은 호 slerp.
//   - CUBICSPLINE: glTF 부록 C의 Hermite 스플라인(접선 × 키 간격). rotation은 결과를 정규화한다.
namespace sr::anim
{
// translation/scale은 앞 3개, rotation은 쿼터니언 (x, y, z, w) 4개를 채운다. 나머지 성분은 0이다.
// componentCount가 4를 넘는 트랙(모프 weights)과 키가 없는 트랙은 0을 돌려준다.
[[nodiscard]] std::array<float, 4> SampleTrack(const sr::gltf::AnimationTrack &track, float time) noexcept;
} // namespace sr::anim

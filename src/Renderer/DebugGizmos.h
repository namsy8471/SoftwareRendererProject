#pragma once
#include <span>
struct DirectionalLight;
class RenderQueue;
namespace sr::debug
{
// 명령이 월드 좌표 정점을 소유한다. lights는 이 호출 중에만 참조한다.
void SubmitDirectionalLightGizmos(RenderQueue &queue, std::span<const DirectionalLight> lights);
} // namespace sr::debug

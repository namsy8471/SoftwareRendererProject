#pragma once
#include "Math/SRMath.h"

// 렌더러가 한 프레임에 필요한 시점 정보만 담은 값이다. Renderer가 Scene/Camera를
// 몰라도 되도록 Camera::GetViewParams()가 이 값을 만들어 넘긴다.
struct ViewParams
{
    SRMath::mat4 view;
    SRMath::mat4 projection;
    SRMath::vec3 position; // 월드 공간 카메라 위치(스페큘러 계산용)
};

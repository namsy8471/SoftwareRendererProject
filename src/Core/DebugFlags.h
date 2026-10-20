#pragma once

// 앱의 디버그 표시 토글. 장면(Scene/Graphics)은 이 값을 모르고, SceneRenderCollector만
// 렌더 명령을 만들 때 읽는다. Win32 메뉴와 srcli --mode가 같은 값을 채운다.
struct DebugFlags
{
    bool bShowNormal = false;         // 법선 벡터 표시 여부
    bool bShowAABB = false;           // AABB 표시 여부
    bool bShowWireframe = false;      // 와이어프레임 표시 여부
    bool bShowLightDirection = false; // 방향광 진행/셰이딩 방향 표시 여부
};

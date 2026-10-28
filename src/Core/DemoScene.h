#pragma once

#include <expected>
#include <filesystem>
#include "Math/AABB.h"
#include "Scene/Scene.h"
#include "Utils/AssetLoadError.h"

// 벤치마크·골든 이미지 전용 고정 장면. Win32 앱, srcli, 테스트가 같은 모델·카메라·광원을
// 쓰도록 구성은 DemoScene.cpp의 표 하나에만 둔다. 표를 바꾸면 벤치 기준과 골든이 바뀐다.
namespace sr::demo
{
// assetDirectory/IronMan.obj, teapot.obj를 읽어 새 Scene을 만든다.
[[nodiscard]] std::expected<Scene, AssetLoadError> LoadScene(const std::filesystem::path &assetDirectory);

// 5x5 PBR 머티리얼 구체 그리드(LookDev 장면)를 절차적으로 생성한다.
[[nodiscard]] Scene CreatePbrSphereGridScene();

// 순수 C++ 코드로 절차적 생성되는 복합 PBR 메카/헬멧 쇼케이스 장면(금속/비금속/거칠기/발광체 복합).
[[nodiscard]] Scene CreatePbrShowcaseScene();

// 3. 고밀도 기하 스트레스 벤치마크 장면(5만+ 트라이앵글 프랙탈 지형).
[[nodiscard]] Scene CreatePolygonalTerrainScene();

// 4. 깊이 오버드로 & Hi-Z 컬링 벤치마크 장면(120개 중첩 소행성/다면체 군집).
[[nodiscard]] Scene CreateInstancedFieldScene();

// 5. 다중 동적 광원 및 섀도우 맵 PCF 스트레스 벤치마크 장면(방향광 + 4 점광원 + 스포트라이트 아레나).
[[nodiscard]] Scene CreateMultiLightArenaScene();

// 현재 물체 경계를 60도 수직 FOV에 맞춘 정면 카메라. 벤치마크와 골든 이미지의 기준 시점이다.
void FitCameraToScene(Scene &scene, float aspectRatio);
// 위와 같은 규칙으로 주어진 경계에 맞춘다(루트 AABB가 비는 glTF 장면용).
void FitCameraToBounds(Scene &scene, const AABB &bounds, float aspectRatio);
} // namespace sr::demo

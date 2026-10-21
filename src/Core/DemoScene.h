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

// 현재 물체 경계를 60도 수직 FOV에 맞춘 정면 카메라. 벤치마크와 골든 이미지의 기준 시점이다.
void FitCameraToScene(Scene &scene, float aspectRatio);
// 위와 같은 규칙으로 주어진 경계에 맞춘다(루트 AABB가 비는 glTF 장면용).
void FitCameraToBounds(Scene &scene, const AABB &bounds, float aspectRatio);
} // namespace sr::demo

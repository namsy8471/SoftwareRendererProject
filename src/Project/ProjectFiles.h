#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>
#include "Graphics/Light.h"
#include "IO/Json.h"
#include "Math/SRMath.h"
#include "Renderer/RenderTypes.h"
#include "Scene/Scene.h"
#include "Utils/AssetLoadError.h"

// 에디터식 프로젝트 폴더와 그 안의 파일 형식(자체 JSON).
//
//   MyProject/
//     MyProject.srproj     프로젝트: 이름, 렌더 설정, 시작 씬, 씬 목록, 에셋 표(id → 경로)
//     Scenes/Main.srscene  씬: 카메라, 광원, 엔티티(id, 이름, 부모 id, 로컬 변환, 메시 에셋 id)
//     Assets/              가져온 원본 파일(.obj와 그 .mtl, .gltf/.glb와 외부 버퍼·텍스처)
//     Cache/               이후 단계의 임포트 캐시(.srmesh 등). 지워도 다시 만든다
//
// 파일 안의 경로는 모두 프로젝트 폴더 기준 상대 경로이고 '/'로 구분한 UTF-8이다. 절대 경로와 '..'는
// 거부해 프로젝트 폴더를 통째로 옮기거나 복사해도 그대로 열린다. 씬은 에셋을 경로가 아니라 id로
// 참조하므로 에셋 파일을 옮길 때는 프로젝트의 에셋 표만 고치면 된다.
// 이 헤더의 함수는 스레드에 묶이지 않는다. Load/Save/Build는 파일을 읽고 쓰므로 프레임 루프에서는
// BackgroundLoader 작업 안에서 부르고, CaptureScene만 Scene을 읽으므로 프레임 루프 스레드에서 부른다.
namespace sr::project
{
// 이 코드가 쓰는 형식 버전. 읽을 때 더 큰 버전은 거부한다(새 렌더러로 저장한 파일).
inline constexpr int formatVersion = 1;
inline constexpr std::string_view projectExtension = ".srproj";
inline constexpr std::string_view sceneExtension = ".srscene";
inline constexpr std::string_view assetsFolder = "Assets";
inline constexpr std::string_view cacheFolder = "Cache";
inline constexpr std::string_view scenesFolder = "Scenes";

struct AssetEntry
{
    std::string id;
    std::string path;           // 프로젝트 폴더 기준 상대 경로
    std::string kind = "model"; // model(.obj, .gltf, .glb)

    friend bool operator==(const AssetEntry &, const AssetEntry &) = default;
};

struct ProjectDocument
{
    std::string name;
    std::string startupScene; // 상대 경로. scenes 안에 있어야 한다
    std::vector<std::string> scenes;
    std::vector<AssetEntry> assets;
    // collectStats는 측정용이라 저장하지 않는다.
    bool frontToBack = false;
    bool hiZ = false;
    bool hiddenWireframe = false;

    [[nodiscard]] const AssetEntry *FindAsset(std::string_view id) const noexcept;
    [[nodiscard]] RenderOptions GetRenderOptions(RenderOptions base = {}) const noexcept;
    void SetRenderOptions(const RenderOptions &options) noexcept;
    // 이미 있으면 아무것도 하지 않는다.
    void AddScene(std::string relativePath);

    friend bool operator==(const ProjectDocument &, const ProjectDocument &) = default;
};

struct EntityDesc
{
    std::string id{};
    std::string name{};
    std::string parent{}; // 빈 문자열이면 루트
    std::string mesh{};   // 에셋 id. 빈 문자열이면 그리지 않는 그룹 노드
    SRMath::vec3 position{0.f, 0.f, 0.f};
    SRMath::vec3 rotationEuler{0.f, 0.f, 0.f}; // 라디안, GameObject::SetRotation과 같은 규약
    std::optional<SRMath::quat> rotationQuat{};
    SRMath::vec3 scale{1.f, 1.f, 1.f};
};

struct CameraDesc
{
    SRMath::vec3 position{0.f, 0.f, -5.f};
    float yaw = 0.f;
    float pitch = 0.f;
};

struct SceneDocument
{
    std::string name;
    bool autoRotate = false; // Scene::IsRotateMode(데모의 Y축 자동 회전)
    CameraDesc camera;
    std::vector<DirectionalLight> lights;
    // 부모가 자식보다 앞에 오지 않아도 된다. 읽을 때 순서를 맞춘다.
    std::vector<EntityDesc> entities;
};

// --- JSON 변환. 형식 오류는 어느 항목인지 적은 문장으로 돌려준다. 모르는 키는 무시한다(앞으로의 확장). ---
[[nodiscard]] json::Value ToJson(const ProjectDocument &project);
[[nodiscard]] json::Value ToJson(const SceneDocument &scene);
[[nodiscard]] std::expected<ProjectDocument, std::string> ProjectFromJson(const json::Value &value);
[[nodiscard]] std::expected<SceneDocument, std::string> SceneFromJson(const json::Value &value);

// --- 파일. 저장은 FileIO의 원자적 쓰기(임시 파일 + rename)라 도중에 실패해도 기존 파일이 남는다. ---
[[nodiscard]] std::expected<ProjectDocument, AssetLoadError> LoadProjectFile(
    const std::filesystem::path &file);
[[nodiscard]] std::expected<void, AssetLoadError> SaveProjectFile(const std::filesystem::path &file,
                                                                  const ProjectDocument &project);
[[nodiscard]] std::expected<SceneDocument, AssetLoadError> LoadSceneFile(const std::filesystem::path &file);
[[nodiscard]] std::expected<void, AssetLoadError> SaveSceneFile(const std::filesystem::path &file,
                                                                const SceneDocument &scene);

// --- 경로 ---
// root 안의 경로를 '/' 구분 상대 경로로 바꾼다. root 밖이면 실패한다.
[[nodiscard]] std::expected<std::string, std::string> ToProjectPath(const std::filesystem::path &root,
                                                                    const std::filesystem::path &path);
// 파일에서 읽은 상대 경로를 root 기준 절대 경로로 바꾼다. 빈 경로, 절대 경로, '..'는 거부한다.
[[nodiscard]] std::expected<std::filesystem::path, std::string> ResolveProjectPath(
    const std::filesystem::path &root, std::string_view relative);
// 16자리 소문자 16진수 무작위 id. 에셋과 엔티티가 같이 쓴다.
[[nodiscard]] std::string NewId();

// --- 프로젝트 만들기 ---
// projectFile(…/MyProject/MyProject.srproj)의 폴더에 Assets/, Cache/, Scenes/Main.srscene(카메라와
// 광원 하나뿐인 빈 씬), Cache를 git에서 빼는 .gitignore, 프로젝트 파일을 만든다.
// 폴더에 이미 .srproj가 있으면 덮어쓰지 않고 실패한다. 이름은 파일 이름(확장자 제외)이다.
struct CreatedProject
{
    ProjectDocument project;
    SceneDocument startupScene;
};
[[nodiscard]] std::expected<CreatedProject, AssetLoadError> CreateProject(
    const std::filesystem::path &projectFile);
// 새 씬 파일 내용(카메라, 기본 방향광 하나). CreateProject와 "새 씬"이 같이 쓴다.
[[nodiscard]] SceneDocument MakeEmptyScene(std::string name);

// --- 에셋 가져오기 ---
// source가 이미 root 안에 있으면 복사하지 않고 그 경로를 쓴다. 아니면 Assets/<이름>/에 .obj(+ .mtl)
// 또는 .glb, .gltf(+ 외부 버퍼·이미지)를 복사한다(같은 이름의 폴더가 있으면 <이름>-2, -3 ...).
// 반환한 항목은 아직 프로젝트 표에 없다. 호출자가 프레임 루프 스레드에서 assets에 넣는다.
// 같은 경로가 표에 이미 있으면 그 항목을 그대로 돌려준다.
[[nodiscard]] std::expected<AssetEntry, AssetLoadError> ImportModelAsset(const std::filesystem::path &root,
                                                                         const ProjectDocument &project,
                                                                         const std::filesystem::path &source);

// --- 런타임 장면과 오가기 ---
// 씬 문서의 엔티티를 GameObject로 만들고 메시 에셋을 읽어 새 Scene을 만든다(작업 스레드용).
// 각 GameObject에는 EntityInfo(id, 이름, 메시 에셋 id)가 붙는다. 같은 에셋을 쓰는 엔티티는 각자 모델을
// 읽는다(GameObject가 Model을 독점 소유). 중복 id, 없는 부모, 부모 순환, 표에 없는 에셋, 읽기 실패는
// 오류다. stop이 요청되면 남은 엔티티를 읽지 않고 "Cancelled" 오류를 돌려준다.
[[nodiscard]] std::expected<Scene, AssetLoadError> BuildScene(const SceneDocument &scene,
                                                              const ProjectDocument &project,
                                                              const std::filesystem::path &root,
                                                              std::stop_token stop = {});

struct CapturedScene
{
    SceneDocument document;
    // EntityInfo id가 없는 물체(데모 장면, 프로젝트 없이 드롭한 모델)는 저장할 수 없어 뺐다.
    // 그 자식도 같이 빠지며 모두 센다.
    std::size_t skippedObjects = 0;
};
// 현재 Scene을 값으로 복사한다(프레임 루프 스레드). 루트부터 전위 순서라 부모가 항상 자식보다 앞이다.
[[nodiscard]] CapturedScene CaptureScene(const Scene &scene, std::string name);
} // namespace sr::project

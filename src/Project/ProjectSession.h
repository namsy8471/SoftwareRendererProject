#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include "Project/ProjectFiles.h"
#include "Utils/AssetLoadError.h"

class FrameRunner;
class GameObject;
class Model;

namespace sr::anim
{
class Animator;
}
namespace sr::gltf
{
struct LoadedScene;
}
namespace sr::io
{
class BackgroundLoader;
}

// 열린 프로젝트와 지금 편집 중인 씬. Win32 메뉴와 테스트가 같은 흐름을 쓰도록 창과 무관하게 둔다.
//   - 모든 공개 함수는 프레임 루프 스레드에서 부른다.
//   - 파일 읽기·쓰기·모델 파싱은 BackgroundLoader 작업 스레드에서 하고, 결과(장면 교체, 저장 완료 표시)는
//     loader.DrainCompleted를 부르는 프레임 시작 훅에서 반영한다. 작업은 값만 캡처하므로 이 객체가
//     먼저 파괴되어도 작업 스레드가 이 객체를 건드리지 않는다. 대신 이 객체가 살아 있는 동안에만
//     DrainCompleted를 불러야 한다(Win32App은 loader보다 뒤에 선언해 먼저 파괴한다).
//   - 나중에 시작한 열기/만들기가 앞의 결과를 이긴다. 앞의 결과는 도착해도 버린다.
namespace sr::project
{
class ProjectSession
{
  public:
    // 실패와 경고. 프레임 시작 훅 안에서 불릴 수 있으므로 대화상자를 바로 띄우지 말고 모아 둔다.
    using ErrorHandler = std::function<void(AssetLoadError)>;
    // 제목 표시줄처럼 열린 프로젝트·씬 이름이나 저장 상태가 바뀌면 부른다.
    using ChangedHandler = std::function<void()>;
    // 가져온 모델을 장면에 넣고 그 물체를 돌려준다(실패하면 null). 배치 규칙은 호출자 몫이다.
    using PlaceModel = std::function<std::shared_ptr<GameObject>(std::unique_ptr<Model>)>;
    // 가져온 glTF 장면과 애니메이터를 장면에 넣고 루트 물체를 돌려준다(실패하면 null).
    using PlaceGltf = std::function<std::shared_ptr<GameObject>(
        sr::gltf::LoadedScene &, const std::shared_ptr<sr::anim::Animator> &)>;

    ProjectSession(FrameRunner &runner, io::BackgroundLoader &loader, ErrorHandler onError,
                   ChangedHandler onChanged = {});
    ProjectSession(const ProjectSession &) = delete;
    ProjectSession &operator=(const ProjectSession &) = delete;

    [[nodiscard]] bool HasProject() const noexcept { return m_project.has_value(); }
    [[nodiscard]] const ProjectDocument *GetProject() const noexcept
    {
        return m_project ? &*m_project : nullptr;
    }
    [[nodiscard]] const std::filesystem::path &GetProjectFile() const noexcept { return m_projectFile; }
    [[nodiscard]] std::filesystem::path GetProjectRoot() const { return m_projectFile.parent_path(); }
    [[nodiscard]] const std::filesystem::path &GetSceneFile() const noexcept { return m_sceneFile; }
    // 마지막 저장(또는 열기) 뒤 MarkDirty가 불렸는지.
    [[nodiscard]] bool IsDirty() const noexcept { return m_isDirty; }
    // 이 세션이 넣은 작업 중 아직 반영되지 않은 것이 있는지.
    [[nodiscard]] bool IsBusy() const noexcept { return m_pendingJobs != 0; }
    // "프로젝트 - 씬*" 형태(UTF-8). 프로젝트가 없으면 빈 문자열.
    [[nodiscard]] std::string GetTitle() const;

    // projectFile 폴더에 새 프로젝트를 만들고 시작 씬을 연다(ProjectFiles의 CreateProject 규칙).
    void CreateProject(std::filesystem::path projectFile);
    // 프로젝트 파일과 그 시작 씬을 읽어 장면을 바꾼다. 렌더 설정도 프로젝트 값으로 바꾼다.
    void OpenProject(std::filesystem::path projectFile);
    // 열린 프로젝트 안의 씬 파일을 연다. 프로젝트 밖의 파일은 오류다.
    void OpenScene(std::filesystem::path sceneFile);
    // 열린 프로젝트 안에 빈 씬 파일을 만들고 연다. 이미 있으면 오류다.
    void NewScene(std::filesystem::path sceneFile);
    // 현재 장면을 지금 씬 파일에, 프로젝트(에셋 표, 씬 목록, 렌더 설정)를 프로젝트 파일에 저장한다.
    void SaveScene();
    // 지금 장면을 프로젝트 안의 다른 씬 파일로 저장하고 그 파일을 현재 씬으로 삼는다.
    void SaveSceneAs(std::filesystem::path sceneFile);
    // 종료 확인처럼 기다려야 할 때 쓰는 동기 저장. 프레임 루프를 멈추므로 다른 곳에서는 쓰지 않는다.
    // 같은 파일을 쓰는 비동기 저장과 겹치지 않도록 먼저 WaitIdle로 남은 작업을 끝낸다.
    [[nodiscard]] std::expected<void, AssetLoadError> SaveSceneNow();
    // 이 세션의 작업이 모두 반영될 때까지 loader.DrainCompleted를 직접 부르며 기다린다(다른 작업의
    // 결과도 같이 반영된다). 시간 안에 끝나면 true.
    bool WaitIdle(std::chrono::milliseconds timeout = std::chrono::seconds(30));
    // 모델 파일(.obj, .gltf, .glb)을 프로젝트 Assets로 가져오고(ImportModelAsset) 읽은 뒤 장면에 넣는다.
    void ImportModel(std::filesystem::path source);
    void ImportModel(std::filesystem::path source, PlaceModel place);
    void ImportModel(std::filesystem::path source, PlaceGltf place);
    // 장면을 바꾼 편집(물체 추가, 이동 등)이 있었음을 알린다.
    void MarkDirty();

  private:
    struct SaveSnapshot
    {
        std::filesystem::path projectFile{};
        std::filesystem::path sceneFile{};
        ProjectDocument project{};
        SceneDocument scene{};
        std::size_t skippedObjects = 0;
    };
    // 열기·만들기 작업의 결과. 성공하면 장면을 통째로 바꾼다.
    struct OpenResult;

    [[nodiscard]] std::expected<SaveSnapshot, AssetLoadError> takeSnapshot() const;
    void submitOpen(std::function<std::expected<OpenResult, AssetLoadError>(std::stop_token)> work);
    void applyOpen(OpenResult result);
    [[nodiscard]] std::expected<std::string, AssetLoadError> sceneInsideProject(
        const std::filesystem::path &sceneFile) const;
    void reportError(AssetLoadError error) const;
    void notifyChanged() const;

    FrameRunner &m_runner;
    io::BackgroundLoader &m_loader;
    ErrorHandler m_onError;
    ChangedHandler m_onChanged;

    std::optional<ProjectDocument> m_project;
    std::filesystem::path m_projectFile;
    std::filesystem::path m_sceneFile;
    std::string m_sceneName;
    bool m_isDirty = false;
    // 열기마다 올린다. 결과의 세대가 다르면 더 나중 요청이 있었으므로 버린다.
    std::uint64_t m_generation = 0;
    // MarkDirty마다 올린다. 저장 시작 때 값과 끝날 때 값이 같아야 깨끗한 상태로 돌린다.
    std::uint64_t m_editCount = 0;
    std::size_t m_pendingJobs = 0;
};
} // namespace sr::project

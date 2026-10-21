#include "Project/ProjectSession.h"
#include "Core/FrameRunner.h"
#include "Graphics/Model.h"
#include "Graphics/ModelLoader.h"
#include "IO/BackgroundLoader.h"
#include "Renderer/Renderer.h"
#include "Scene/GameObject.h"

#include <exception>
#include <thread>
#include <utility>

namespace fs = std::filesystem;

namespace sr::project
{
struct ProjectSession::OpenResult
{
    std::uint64_t generation = 0;
    fs::path projectFile;
    fs::path sceneFile;
    ProjectDocument project;
    std::string sceneName;
    Scene scene;
};

namespace
{
// 작업이 예외를 던지면 BackgroundLoader가 apply 대신 onError를 불러 대기 수가 맞지 않게 된다.
// 결과 형식(expected)으로 바꿔 항상 apply가 돌게 한다.
template <class Work> auto guarded(Work work)
{
    return [work = std::move(work)](std::stop_token stop) mutable -> decltype(work(stop))
    {
        try
        {
            return work(stop);
        }
        catch (const std::exception &error)
        {
            return std::unexpected(AssetLoadError{error.what()});
        }
    };
}

[[nodiscard]] fs::path withExtension(fs::path file, std::string_view extension)
{
    if (file.extension() != fs::path(extension))
        file += extension;
    return file;
}

[[nodiscard]] std::string stemUtf8(const fs::path &file)
{
    const auto text = file.stem().u8string();
    return {text.begin(), text.end()};
}

[[nodiscard]] std::expected<void, AssetLoadError> writeSnapshot(const fs::path &projectFile,
                                                                const fs::path &sceneFile,
                                                                const ProjectDocument &project,
                                                                const SceneDocument &scene)
{
    // 씬을 먼저 쓴다. 프로젝트 쓰기가 실패해도 새 씬 파일은 이미 안전하게 바뀌어 있다.
    if (auto saved = SaveSceneFile(sceneFile, scene); !saved)
        return saved;
    return SaveProjectFile(projectFile, project);
}
} // namespace

ProjectSession::ProjectSession(FrameRunner &runner, io::BackgroundLoader &loader, ErrorHandler onError,
                               ChangedHandler onChanged)
    : m_runner(runner), m_loader(loader), m_onError(std::move(onError)), m_onChanged(std::move(onChanged))
{
}

std::string ProjectSession::GetTitle() const
{
    if (!m_project)
        return {};
    return m_project->name + " - " + m_sceneName + (m_isDirty ? "*" : "");
}

void ProjectSession::reportError(AssetLoadError error) const
{
    if (m_onError)
        m_onError(std::move(error));
}

void ProjectSession::notifyChanged() const
{
    if (m_onChanged)
        m_onChanged();
}

void ProjectSession::MarkDirty()
{
    ++m_editCount;
    if (m_isDirty)
        return;
    m_isDirty = true;
    notifyChanged();
}

// --- 열기 ---
void ProjectSession::submitOpen(
    std::function<std::expected<OpenResult, AssetLoadError>(std::stop_token)> work)
{
    const std::uint64_t generation = ++m_generation;
    ++m_pendingJobs;
    m_loader.Submit(guarded(
                        [work = std::move(work), generation](std::stop_token stop)
                        {
                            auto result = work(stop);
                            if (result)
                                result->generation = generation;
                            return result;
                        }),
                    [this](std::expected<OpenResult, AssetLoadError> result)
                    {
                        --m_pendingJobs;
                        if (!result)
                            reportError(std::move(result.error()));
                        else
                            applyOpen(std::move(*result));
                    });
}

void ProjectSession::applyOpen(OpenResult result)
{
    if (result.generation != m_generation)
        return; // 더 나중에 연 것이 있다
    m_runner.SetScene(std::move(result.scene));
    Renderer &renderer = m_runner.GetRenderer();
    renderer.SetOptions(result.project.GetRenderOptions(renderer.GetOptions()));
    m_project = std::move(result.project);
    m_projectFile = std::move(result.projectFile);
    m_sceneFile = std::move(result.sceneFile);
    m_sceneName = std::move(result.sceneName);
    m_isDirty = false;
    notifyChanged();
}

void ProjectSession::CreateProject(fs::path projectFile)
{
    projectFile = withExtension(std::move(projectFile), projectExtension);
    submitOpen(
        [projectFile](std::stop_token stop) -> std::expected<OpenResult, AssetLoadError>
        {
            auto created = sr::project::CreateProject(projectFile);
            if (!created)
                return std::unexpected(std::move(created.error()));
            const fs::path root = projectFile.parent_path();
            auto scene = BuildScene(created->startupScene, created->project, root, stop);
            if (!scene)
                return std::unexpected(std::move(scene.error()));
            auto sceneFile = ResolveProjectPath(root, created->project.startupScene);
            return OpenResult{.projectFile = projectFile,
                              .sceneFile = sceneFile.value_or(fs::path{}),
                              .project = std::move(created->project),
                              .sceneName = std::move(created->startupScene.name),
                              .scene = std::move(*scene)};
        });
}

void ProjectSession::OpenProject(fs::path projectFile)
{
    submitOpen(
        [projectFile](std::stop_token stop) -> std::expected<OpenResult, AssetLoadError>
        {
            auto project = LoadProjectFile(projectFile);
            if (!project)
                return std::unexpected(std::move(project.error()));
            const std::string startup =
                !project->startupScene.empty()
                    ? project->startupScene
                    : (project->scenes.empty() ? std::string{} : project->scenes.front());
            if (startup.empty())
                return std::unexpected(AssetLoadError{"Project has no scene", projectFile});
            const fs::path root = projectFile.parent_path();
            auto sceneFile = ResolveProjectPath(root, startup);
            if (!sceneFile)
                return std::unexpected(AssetLoadError{sceneFile.error(), projectFile});
            auto document = LoadSceneFile(*sceneFile);
            if (!document)
                return std::unexpected(std::move(document.error()));
            auto scene = BuildScene(*document, *project, root, stop);
            if (!scene)
                return std::unexpected(std::move(scene.error()));
            std::string sceneName = document->name.empty() ? stemUtf8(*sceneFile) : document->name;
            return OpenResult{.projectFile = projectFile,
                              .sceneFile = std::move(*sceneFile),
                              .project = std::move(*project),
                              .sceneName = std::move(sceneName),
                              .scene = std::move(*scene)};
        });
}

std::expected<std::string, AssetLoadError> ProjectSession::sceneInsideProject(const fs::path &sceneFile) const
{
    if (!m_project)
        return std::unexpected(AssetLoadError{"No project is open", sceneFile});
    auto relative = ToProjectPath(GetProjectRoot(), sceneFile);
    if (!relative)
        return std::unexpected(
            AssetLoadError{"Scene must be inside the project folder: " + relative.error(), sceneFile});
    return std::move(*relative);
}

void ProjectSession::OpenScene(fs::path sceneFile)
{
    if (auto inside = sceneInsideProject(sceneFile); !inside)
        return reportError(std::move(inside.error()));
    submitOpen(
        [projectFile = m_projectFile, project = *m_project,
         sceneFile](std::stop_token stop) mutable -> std::expected<OpenResult, AssetLoadError>
        {
            auto document = LoadSceneFile(sceneFile);
            if (!document)
                return std::unexpected(std::move(document.error()));
            auto scene = BuildScene(*document, project, projectFile.parent_path(), stop);
            if (!scene)
                return std::unexpected(std::move(scene.error()));
            return OpenResult{.projectFile = std::move(projectFile),
                              .sceneFile = sceneFile,
                              .project = std::move(project),
                              .sceneName = document->name.empty() ? stemUtf8(sceneFile) : document->name,
                              .scene = std::move(*scene)};
        });
}

void ProjectSession::NewScene(fs::path sceneFile)
{
    sceneFile = withExtension(std::move(sceneFile), sceneExtension);
    auto relative = sceneInsideProject(sceneFile);
    if (!relative)
        return reportError(std::move(relative.error()));
    ProjectDocument project = *m_project;
    project.AddScene(std::move(*relative));
    project.SetRenderOptions(m_runner.GetRenderer().GetOptions());
    submitOpen(
        [projectFile = m_projectFile, project = std::move(project),
         sceneFile](std::stop_token stop) mutable -> std::expected<OpenResult, AssetLoadError>
        {
            std::error_code error;
            if (fs::exists(sceneFile, error))
                return std::unexpected(AssetLoadError{"Scene file already exists", sceneFile});
            SceneDocument document = MakeEmptyScene(stemUtf8(sceneFile));
            if (auto saved = writeSnapshot(projectFile, sceneFile, project, document); !saved)
                return std::unexpected(std::move(saved.error()));
            auto scene = BuildScene(document, project, projectFile.parent_path(), stop);
            if (!scene)
                return std::unexpected(std::move(scene.error()));
            return OpenResult{.projectFile = std::move(projectFile),
                              .sceneFile = sceneFile,
                              .project = std::move(project),
                              .sceneName = std::move(document.name),
                              .scene = std::move(*scene)};
        });
}

// --- 저장 ---
std::expected<ProjectSession::SaveSnapshot, AssetLoadError> ProjectSession::takeSnapshot() const
{
    if (!m_project)
        return std::unexpected(AssetLoadError{"No project is open"});
    SaveSnapshot snapshot{.projectFile = m_projectFile, .sceneFile = m_sceneFile, .project = *m_project};
    snapshot.project.SetRenderOptions(m_runner.GetRenderer().GetOptions());
    auto captured = CaptureScene(m_runner.GetScene(), m_sceneName);
    snapshot.scene = std::move(captured.document);
    snapshot.skippedObjects = captured.skippedObjects;
    return snapshot;
}

void ProjectSession::SaveScene()
{
    auto snapshot = takeSnapshot();
    if (!snapshot)
        return reportError(std::move(snapshot.error()));
    if (snapshot->skippedObjects != 0)
        reportError(
            {std::to_string(snapshot->skippedObjects) + " object(s) that are not project assets were not "
                                                        "saved. Import models while the project is open."});

    ++m_pendingJobs;
    m_loader.Submit(
        guarded(
            [snapshot = std::move(*snapshot)](std::stop_token) -> std::expected<void, AssetLoadError> {
                return writeSnapshot(snapshot.projectFile, snapshot.sceneFile, snapshot.project,
                                     snapshot.scene);
            }),
        [this, generation = m_generation, editCount = m_editCount](std::expected<void, AssetLoadError> saved)
        {
            --m_pendingJobs;
            if (!saved)
                return reportError(std::move(saved.error()));
            // 저장하는 동안 다른 프로젝트를 열었거나 더 편집했으면 여전히 저장할 것이 있다.
            if (generation == m_generation && editCount == m_editCount && m_isDirty)
            {
                m_isDirty = false;
                notifyChanged();
            }
        });
}

void ProjectSession::SaveSceneAs(fs::path sceneFile)
{
    sceneFile = withExtension(std::move(sceneFile), sceneExtension);
    auto relative = sceneInsideProject(sceneFile);
    if (!relative)
        return reportError(std::move(relative.error()));
    m_project->AddScene(std::move(*relative));
    m_sceneFile = std::move(sceneFile);
    m_sceneName = stemUtf8(m_sceneFile);
    // 새 파일은 아직 디스크에 없으므로 편집이 없어도 저장할 것이 있다.
    MarkDirty();
    SaveScene();
}

bool ProjectSession::WaitIdle(std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (m_pendingJobs != 0)
    {
        if (m_loader.DrainCompleted() == 0)
        {
            if (std::chrono::steady_clock::now() >= deadline)
                return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    return true;
}

std::expected<void, AssetLoadError> ProjectSession::SaveSceneNow()
{
    if (!WaitIdle())
        return std::unexpected(AssetLoadError{"Previous file operation is still running"});
    auto snapshot = takeSnapshot();
    if (!snapshot)
        return std::unexpected(std::move(snapshot.error()));
    if (auto saved =
            writeSnapshot(snapshot->projectFile, snapshot->sceneFile, snapshot->project, snapshot->scene);
        !saved)
        return saved;
    m_isDirty = false;
    notifyChanged();
    return {};
}

// --- 에셋 ---
void ProjectSession::ImportModel(fs::path source, PlaceModel place)
{
    if (!m_project)
        return reportError({"No project is open", source});
    struct Imported
    {
        AssetEntry asset;
        std::unique_ptr<Model> model;
    };
    using ImportResult = std::expected<Imported, AssetLoadError>;
    ++m_pendingJobs;
    m_loader.Submit(
        guarded(
            [root = GetProjectRoot(), project = *m_project, source](std::stop_token) -> ImportResult
            {
                auto asset = ImportModelAsset(root, project, source);
                if (!asset)
                    return std::unexpected(std::move(asset.error()));
                auto path = ResolveProjectPath(root, asset->path);
                if (!path)
                    return std::unexpected(AssetLoadError{path.error(), source});
                auto model = ModelLoader::LoadOBJ(*path);
                if (!model)
                    return std::unexpected(std::move(model.error()));
                return Imported{std::move(*asset), std::move(*model)};
            }),
        [this, generation = m_generation, name = stemUtf8(source),
         place = std::move(place)](ImportResult result)
        {
            --m_pendingJobs;
            if (!result)
                return reportError(std::move(result.error()));
            // 가져오는 동안 다른 프로젝트·씬을 열었으면 이 씬에는 넣지 않는다(복사한 파일은 남는다).
            if (generation != m_generation || !m_project)
                return;
            if (!m_project->FindAsset(result->asset.id))
                m_project->assets.push_back(result->asset);
            const auto object = place(std::move(result->model));
            if (!object)
                return reportError({"Failed to add the model to the scene"});
            object->SetEntityInfo({.id = NewId(), .name = name, .meshAsset = result->asset.id});
            MarkDirty();
        });
}
} // namespace sr::project

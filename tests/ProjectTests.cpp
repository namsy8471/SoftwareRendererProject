#include "Core/FrameRunner.h"
#include "IO/BackgroundLoader.h"
#include "IO/FileIO.h"
#include "IO/Json.h"
#include "Project/ProjectFiles.h"
#include "Project/ProjectSession.h"
#include "Renderer/Renderer.h"
#include "Scene/GameObject.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <stop_token>
#include <thread>
#include <vector>

#include "TestHarness.h"
#include "TestSupport.h"

using namespace sr::test;
namespace fs = std::filesystem;
namespace project = sr::project;

namespace
{
fs::path freshDirectory(const TestContext &context, std::string_view name)
{
    const auto directory = context.outputDirectory / "project" / name;
    std::error_code ignored;
    fs::remove_all(directory, ignored);
    fs::create_directories(directory, ignored);
    return directory;
}

bool sameVec3(const SRMath::vec3 &a, const SRMath::vec3 &b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

void checkSameScene(const project::SceneDocument &a, const project::SceneDocument &b)
{
    check(a.name == b.name && a.autoRotate == b.autoRotate, "scene header");
    check(sameVec3(a.camera.position, b.camera.position) && a.camera.yaw == b.camera.yaw &&
              a.camera.pitch == b.camera.pitch,
          "camera");
    check(a.lights.size() == b.lights.size(), "light count");
    for (std::size_t index = 0; index < a.lights.size(); ++index)
        check(sameVec3(a.lights[index].rayDirection, b.lights[index].rayDirection) &&
                  sameVec3(a.lights[index].color, b.lights[index].color),
              "light values");
    check(a.entities.size() == b.entities.size(), "entity count");
    for (std::size_t index = 0; index < a.entities.size(); ++index)
    {
        const auto &left = a.entities[index];
        const auto &right = b.entities[index];
        check(left.id == right.id && left.name == right.name && left.parent == right.parent &&
                  left.mesh == right.mesh,
              "entity fields");
        check(sameVec3(left.position, right.position) && sameVec3(left.rotationEuler, right.rotationEuler) &&
                  sameVec3(left.scale, right.scale),
              "entity transform bits");
    }
}

// 같은 장면이 같은 픽셀을 내는지 비교하려고 FrameRunner로 한 프레임을 그린다(골든과 같은 경로).
std::vector<unsigned int> renderPixels(FrameRunner &runner, int width = 160, int height = 120)
{
    MemorySurface surface(width, height);
    runner.Update(0.f, width, height);
    runner.Render(surface.target);
    return surface.pixels;
}

std::vector<unsigned int> renderScene(Scene scene)
{
    FrameRunner runner;
    runner.SetScene(std::move(scene));
    return renderPixels(runner);
}

std::size_t differingPixels(const std::vector<unsigned int> &a, const std::vector<unsigned int> &b)
{
    std::size_t count = 0;
    for (std::size_t index = 0; index < a.size() && index < b.size(); ++index)
        count += a[index] != b[index] ? 1 : 0;
    return count;
}

void jsonRoundTripTests()
{
    project::ProjectDocument projectDocument;
    projectDocument.name = "\xED\x95\x9C\xEA\xB8\x80 project"; // 한글
    projectDocument.startupScene = "Scenes/Main.srscene";
    projectDocument.AddScene("Scenes/Main.srscene");
    projectDocument.AddScene("Scenes/Second level.srscene");
    projectDocument.AddScene("Scenes/Main.srscene"); // 중복은 무시
    projectDocument.assets.push_back({.id = "0123456789abcdef", .path = "Assets/teapot/teapot.obj"});
    projectDocument.hiZ = true;
    projectDocument.hiddenWireframe = true;
    check(projectDocument.scenes.size() == 2, "AddScene keeps one copy");

    const auto projectText = sr::json::Write(project::ToJson(projectDocument));
    check(projectText.has_value(), "write project");
    const auto projectParsed = sr::json::Parse(*projectText);
    check(projectParsed.has_value(), "parse project");
    const auto projectBack = project::ProjectFromJson(*projectParsed);
    check(projectBack.has_value(), "project from json");
    check(*projectBack == projectDocument, "project round trip");
    check(projectBack->GetRenderOptions().hiZ && !projectBack->GetRenderOptions().frontToBack,
          "render options");

    project::SceneDocument scene = project::MakeEmptyScene("Main");
    scene.autoRotate = true;
    scene.camera = {{1.5f, -2.25f, 1e-7f}, 0.3f, -1.2f};
    scene.lights.push_back({{0.f, -1.f, 0.f}, {0.5f, 0.25f, 1.f}});
    // 자식이 부모보다 앞에 있어도 된다. 1/3처럼 짧게 안 써지는 float도 비트가 그대로 돌아와야 한다.
    scene.entities.push_back({.id = "child",
                              .name = "Lid",
                              .parent = "root",
                              .mesh = "0123456789abcdef",
                              .position = {0.f, 1.f / 3.f, 0.f},
                              .rotationEuler = {0.1f, 0.2f, 0.3f},
                              .scale = {0.04f, 0.04f, 0.04f}});
    scene.entities.push_back({.id = "root", .name = "Group"});

    const auto sceneText = sr::json::Write(project::ToJson(scene));
    check(sceneText.has_value(), "write scene");
    check(sceneText->find("0.04,") != std::string::npos, "float written in shortest form: " + *sceneText);
    check(sceneText->find("\"parent\": null") != std::string::npos, "root parent is null");
    const auto sceneParsed = sr::json::Parse(*sceneText);
    check(sceneParsed.has_value(), "parse scene");
    const auto sceneBack = project::SceneFromJson(*sceneParsed);
    check(sceneBack.has_value(), "scene from json: " + (sceneBack ? std::string{} : sceneBack.error()));
    checkSameScene(scene, *sceneBack);

    // 같은 문서는 항상 같은 텍스트(diff가 깨끗하다).
    check(sr::json::Write(project::ToJson(*sceneBack)).value() == *sceneText, "stable text");
}

void formatErrorTests()
{
    const auto projectError = [](std::string_view text) -> std::string
    {
        const auto parsed = sr::json::Parse(text);
        check(parsed.has_value(), "test JSON parses");
        const auto result = project::ProjectFromJson(*parsed);
        check(!result.has_value(), "project rejected: " + std::string(text));
        return result.error();
    };
    const auto sceneError = [](std::string_view text) -> std::string
    {
        const auto parsed = sr::json::Parse(text);
        check(parsed.has_value(), "test JSON parses");
        const auto result = project::SceneFromJson(*parsed);
        check(!result.has_value(), "scene rejected: " + std::string(text));
        return result.error();
    };

    projectError(R"([])");
    projectError(R"({"format":"srscene","version":1})");
    check(projectError(R"({"format":"srproj","version":2})").find("newer") != std::string::npos,
          "newer version");
    projectError(R"({"format":"srproj","version":1.5})");
    projectError(R"({"format":"srproj","version":1,"scenes":["../outside.srscene"]})");
    projectError(R"({"format":"srproj","version":1,"scenes":["/abs.srscene"]})");
    projectError(R"({"format":"srproj","version":1,"scenes":["Scenes\\Main.srscene"]})");
    projectError(
        R"({"format":"srproj","version":1,"assets":[{"id":"a","path":"x.obj"},{"id":"a","path":"y.obj"}]})");
    projectError(R"({"format":"srproj","version":1,"assets":[{"id":"a","kind":"sound","path":"x.wav"}]})");
    projectError(R"({"format":"srproj","version":1,"assets":[{"path":"x.obj"}]})");
    projectError(R"({"format":"srproj","version":1,"render":{"hiZ":1}})");

    check(
        sceneError(R"({"format":"srscene","version":1,"entities":[{"id":"a","transform":{"scale":[1,2]}}]})")
                .find("scene.entities[0].transform.scale") != std::string::npos,
        "error names the field");
    sceneError(R"({"format":"srscene","version":1,"entities":[{"name":"no id"}]})");
    sceneError(R"({"format":"srscene","version":1,"camera":{"yaw":"left"}})");
    sceneError(R"({"format":"srscene","version":1,"camera":{"position":[0,0,1e300]}})");
    sceneError(R"({"format":"srscene","version":1,"lights":[{"type":"spot"}]})");

    // 모르는 키는 무시하고, 빠진 항목은 기본값이다.
    const auto minimal = sr::json::Parse(R"({"format":"srscene","version":1,"future":{"x":1}})");
    const auto scene = project::SceneFromJson(*minimal);
    check(scene && scene->entities.empty() && scene->lights.empty() && scene->camera.position.z == -5.f,
          "defaults and unknown keys");

    check(!project::ResolveProjectPath("root", "").has_value(), "empty path");
    check(!project::ResolveProjectPath("root", "a/../../b").has_value(), "dot-dot path");
    check(project::ResolveProjectPath("root", "./Assets/a.obj").value() == fs::path("root/Assets/a.obj"),
          "dot segment normalized");

    const auto first = project::NewId();
    const auto second = project::NewId();
    check(first.size() == 16 && first.find_first_not_of("0123456789abcdef") == std::string::npos,
          "id format");
    check(first != second, "ids differ");
}

void createProjectTests(const TestContext &context)
{
    const auto directory = freshDirectory(context, "create");
    const auto created = project::CreateProject(directory / "MyGame" / "MyGame");
    check(created.has_value(), "create project: " + (created ? std::string{} : created.error().message));
    const auto root = directory / "MyGame";
    for (const auto *folder : {"Assets", "Cache", "Scenes"})
        check(fs::is_directory(root / folder), std::string("folder ") + folder);
    check(sr::io::ReadWholeFile(root / ".gitignore").value() == "Cache/\n", ".gitignore");
    check(created->project.name == "MyGame" && created->project.startupScene == "Scenes/Main.srscene",
          "project defaults");

    const auto loaded = project::LoadProjectFile(root / "MyGame.srproj");
    check(loaded && *loaded == created->project, "project file matches");
    const auto scene = project::LoadSceneFile(root / "Scenes/Main.srscene");
    check(scene && scene->entities.empty() && scene->lights.size() == 1, "startup scene file");

    // 같은 폴더에 두 번째 프로젝트는 만들지 않는다(기존 파일 보호).
    check(!project::CreateProject(root / "Other.srproj").has_value(), "second project refused");
    check(fs::exists(root / "MyGame.srproj"), "first project kept");

    // 깨진 파일은 줄 번호와 함께 실패한다.
    check(sr::io::WriteFileAtomically(root / "Broken.srscene",
                                      std::string("{\n  \"format\": \"srscene\",\n  oops\n}"))
              .has_value(),
          "write broken file");
    const auto broken = project::LoadSceneFile(root / "Broken.srscene");
    check(!broken && broken.error().line == 3, "syntax error line");
}

void importAndBuildTests(const TestContext &context)
{
    const auto directory = freshDirectory(context, "import");
    auto created = project::CreateProject(directory / "Proj/Proj.srproj");
    check(created.has_value(), "create");
    const auto root = directory / "Proj";
    auto &projectDocument = created->project;

    // 밖의 파일은 Assets/<이름>/으로 복사하고 mtllib도 같이 가져온다.
    const auto quad =
        project::ImportModelAsset(root, projectDocument, context.root / "tests/fixtures/quad.obj");
    check(quad.has_value(), "import quad: " + (quad ? std::string{} : quad.error().message));
    check(quad->path == "Assets/quad/quad.obj" && quad->kind == "model", "import path");
    check(fs::exists(root / "Assets/quad/quad.obj") && fs::exists(root / "Assets/quad/valid.mtl"),
          "obj + mtl copied");
    projectDocument.assets.push_back(*quad);

    const auto again =
        project::ImportModelAsset(root, projectDocument, context.root / "tests/fixtures/quad.obj");
    check(again && again->path == "Assets/quad-2/quad.obj" && again->id != quad->id,
          "same name gets new folder");

    // 이미 프로젝트 안의 파일은 복사하지 않고, 표에 있으면 같은 항목이다.
    const auto inside = project::ImportModelAsset(root, projectDocument, root / "Assets/quad/quad.obj");
    check(inside && *inside == *quad, "inside file reuses entry");
    check(!project::ImportModelAsset(root, projectDocument, context.root / "assets/teapot.png").has_value(),
          "non-obj refused");

    const auto teapot = project::ImportModelAsset(root, projectDocument, context.root / "assets/teapot.obj");
    check(teapot.has_value(), "import teapot");
    projectDocument.assets.push_back(*teapot);

    // 계층: 자식이 파일에서 먼저 나와도 부모 아래에 붙는다.
    project::SceneDocument document = project::MakeEmptyScene("Main");
    document.entities.push_back({.id = "pot",
                                 .name = "Teapot",
                                 .parent = "group",
                                 .mesh = teapot->id,
                                 .position = {1.f, 0.f, 0.f},
                                 .scale = {0.5f, 0.5f, 0.5f}});
    document.entities.push_back({.id = "group", .name = "Group", .position = {0.f, 2.f, 10.f}});
    document.entities.push_back({.id = "quad", .name = "Quad", .mesh = quad->id});
    auto scene = project::BuildScene(document, projectDocument, root);
    check(scene.has_value(), "build scene: " + (scene ? std::string{} : scene.error().message));
    const auto roots = scene->GetRootObjects();
    check(roots.size() == 2, "two roots");
    check(roots[0]->GetEntityInfo().id == "group" && roots[0]->GetModel() == nullptr, "group node first");
    check(roots[0]->GetChildren().size() == 1 &&
              roots[0]->GetChildren()[0]->GetEntityInfo().meshAsset == teapot->id,
          "child attached with asset id");
    check(roots[1]->GetEntityInfo().name == "Quad" && roots[1]->GetModel() != nullptr, "quad root");
    scene->Update(0.f, 4.f / 3.f, std::array<bool, Scene::keyCount>{});
    const auto &world = roots[0]->GetChildren()[0]->GetWorldMatrix();
    check(approximatelyEqual(world.cols[3][0], 1.f) && approximatelyEqual(world.cols[3][1], 2.f) &&
              approximatelyEqual(world.cols[3][2], 10.f),
          "child world = parent * local");

    const auto buildError = [&](project::SceneDocument broken)
    {
        const auto result = project::BuildScene(broken, projectDocument, root);
        check(!result.has_value(), "broken scene rejected");
        return result.error().message;
    };
    auto cycle = project::MakeEmptyScene("c");
    cycle.entities = {{.id = "a", .parent = "b"}, {.id = "b", .parent = "a"}};
    check(buildError(cycle).find("ancestor") != std::string::npos, "cycle");
    auto orphan = project::MakeEmptyScene("o");
    orphan.entities = {{.id = "a", .parent = "missing"}};
    check(buildError(orphan).find("unknown parent") != std::string::npos, "unknown parent");
    auto duplicate = project::MakeEmptyScene("d");
    duplicate.entities = {{.id = "a"}, {.id = "a"}};
    check(buildError(duplicate).find("Duplicated") != std::string::npos, "duplicate id");
    auto unknownAsset = project::MakeEmptyScene("u");
    unknownAsset.entities = {{.id = "a", .mesh = "nope"}};
    check(buildError(unknownAsset).find("unknown asset") != std::string::npos, "unknown asset");

    // 취소 요청이면 모델을 읽지 않고 멈춘다.
    std::stop_source stop;
    stop.request_stop();
    const auto cancelled = project::BuildScene(document, projectDocument, root, stop.get_token());
    check(!cancelled && cancelled.error().message == "Cancelled", "cancelled build");
}

// 런타임 장면 → 씬 파일 → 런타임 장면이 같은 픽셀을 그려야 한다.
void captureRoundTripTests(const TestContext &context)
{
    const auto directory = freshDirectory(context, "capture");
    auto created = project::CreateProject(directory / "Proj/Proj.srproj");
    check(created.has_value(), "create");
    const auto root = directory / "Proj";
    auto &projectDocument = created->project;
    const auto teapot = project::ImportModelAsset(root, projectDocument, context.root / "assets/teapot.obj");
    check(teapot.has_value(), "import");
    projectDocument.assets.push_back(*teapot);

    project::SceneDocument document = project::MakeEmptyScene("Main");
    document.camera = {{0.f, 1.f, -6.f}, 0.1f, -0.15f};
    document.entities = {
        {.id = "group", .name = "Group", .position = {0.f, -0.5f, 0.f}, .rotationEuler = {0.f, 0.7f, 0.f}},
        {.id = "a",
         .name = "A",
         .parent = "group",
         .mesh = teapot->id,
         .position = {-1.2f, 0.f, 0.f},
         .scale = {0.03f, 0.03f, 0.03f}},
        {.id = "b",
         .name = "B",
         .parent = "group",
         .mesh = teapot->id,
         .position = {1.2f, 0.f, 0.f},
         .rotationEuler = {0.3f, 0.f, 0.2f},
         .scale = {0.025f, 0.025f, 0.025f}}};
    auto scene = project::BuildScene(document, projectDocument, root);
    check(scene.has_value(), "build");

    // 프로젝트 밖에서 온 물체(EntityInfo 없음)는 빠지고 개수로 알린다.
    auto stray = loadObject(context.root / "tests/fixtures/triangle.obj", {0.f, 0.f, 0.f});
    check(stray->AddChild(loadObject(context.root / "tests/fixtures/triangle.obj", {1.f, 0.f, 0.f})),
          "stray child");
    check(scene->AddObject(stray), "add stray");
    const auto captured = project::CaptureScene(*scene, "Main");
    check(captured.skippedObjects == 2, "stray subtree skipped");
    checkSameScene(document, captured.document);

    const auto sceneFile = root / "Scenes/Main.srscene";
    check(project::SaveSceneFile(sceneFile, captured.document).has_value(), "save");
    const auto loaded = project::LoadSceneFile(sceneFile);
    check(loaded.has_value(), "load");
    checkSameScene(document, *loaded);

    auto original = project::BuildScene(document, projectDocument, root);
    auto reloaded = project::BuildScene(*loaded, projectDocument, root);
    check(original && reloaded, "rebuild");
    const auto expected = renderScene(std::move(*original));
    const auto actual = renderScene(std::move(*reloaded));
    check(expected == actual, "reloaded scene renders identical pixels");
    check(differingPixels(expected, renderScene(Scene{})) > 200, "teapots are visible");
}

// Win32 메뉴가 쓰는 흐름 그대로: 만들기 → 모델 가져오기 → 저장 → 다른 세션에서 열기.
void sessionTests(const TestContext &context)
{
    using namespace std::chrono_literals;
    const auto directory = freshDirectory(context, "session");
    const auto projectFile = directory / "Live/Live.srproj";

    std::vector<std::string> errors;
    const auto onError = [&errors](AssetLoadError error) { errors.push_back(error.message); };
    // 매 프레임 훅에서 결과를 반영하는 Win32App과 같은 구성.
    const auto drive = [](FrameRunner &runner, const project::ProjectSession &session)
    {
        const auto deadline = std::chrono::steady_clock::now() + 20s;
        while (session.IsBusy() && std::chrono::steady_clock::now() < deadline)
        {
            runner.Update(1.f / 60.f, 160, 120);
            std::this_thread::sleep_for(1ms);
        }
        check(!session.IsBusy(), "session jobs finished");
    };

    std::vector<unsigned int> savedPixels;
    {
        FrameRunner runner;
        sr::io::BackgroundLoader loader;
        int changes = 0;
        project::ProjectSession session(runner, loader, onError, [&changes] { ++changes; });
        runner.SetFrameStartHook([&loader](FrameRunner &) { loader.DrainCompleted(); });
        // 데모 장면 대신 열린 장면이 들어오는지 보려고 미리 물체 하나를 넣어 둔다.
        check(runner.GetScene().AddObject(loadObject(context.root / "tests/fixtures/triangle.obj", {})),
              "seed");

        session.CreateProject(directory / "Live/Live"); // 확장자는 붙여 준다
        check(session.IsBusy() && !session.HasProject(), "create is asynchronous");
        drive(runner, session);
        check(errors.empty(), "create errors: " + (errors.empty() ? std::string{} : errors.front()));
        check(session.HasProject() && session.GetProjectFile() == projectFile, "project open");
        check(session.GetTitle() == "Live - Main" && changes > 0, "title and change notice");
        check(runner.GetScene().GetRootObjects().empty(), "scene replaced by empty startup scene");

        runner.GetRenderer().SetOptions({.frontToBack = true});
        session.ImportModel(context.root / "assets/teapot.obj",
                            [&runner](std::unique_ptr<Model> model)
                            {
                                auto object = std::make_shared<GameObject>(
                                    SRMath::vec3{0.f, 0.f, 0.f}, SRMath::vec3{0.f, 0.5f, 0.f},
                                    SRMath::vec3{0.04f, 0.04f, 0.04f}, std::move(model));
                                return runner.GetScene().AddObject(object) ? object : nullptr;
                            });
        drive(runner, session);
        check(errors.empty(), "import errors: " + (errors.empty() ? std::string{} : errors.front()));
        check(session.IsDirty() && session.GetTitle() == "Live - Main*", "dirty after import");
        check(session.GetProject()->assets.size() == 1, "asset registered");
        const auto &info = runner.GetScene().GetRootObjects().front()->GetEntityInfo();
        check(info.name == "teapot" && info.meshAsset == session.GetProject()->assets[0].id &&
                  info.id.size() == 16,
              "entity info on placed object");

        runner.GetScene().GetCamera().SetCameraPos({0.f, 0.5f, -4.f});
        runner.GetScene().SetRotateMode(false);
        session.SaveScene();
        drive(runner, session);
        check(errors.empty() && !session.IsDirty(), "saved and clean");
        savedPixels = renderPixels(runner);

        // 저장 중 편집이 있으면 여전히 dirty다.
        session.SaveScene();
        session.MarkDirty();
        drive(runner, session);
        check(session.IsDirty(), "edit during save keeps dirty");
        check(session.SaveSceneNow().has_value() && !session.IsDirty(), "synchronous save");
        // 비동기 저장 직후의 동기 저장(Ctrl+S 뒤 바로 종료)은 앞 작업을 끝낸 뒤 쓴다.
        session.MarkDirty();
        session.SaveScene();
        check(session.SaveSceneNow().has_value() && !session.IsBusy() && !session.IsDirty(),
              "synchronous save waits for pending save");

        // 다른 이름으로 저장하면 씬 목록에 들어가고 현재 씬이 바뀐다.
        session.SaveSceneAs(directory / "Live/Scenes/Copy");
        drive(runner, session);
        check(errors.empty() && session.GetTitle() == "Live - Copy", "save as");
        check(fs::exists(directory / "Live/Scenes/Copy.srscene"), "copy written");
        check(session.GetProject()->scenes.size() == 2, "scene list grew");

        // 프로젝트 밖의 씬은 거부한다.
        session.OpenScene(directory / "elsewhere.srscene");
        check(errors.size() == 1, "scene outside project rejected");
        errors.clear();

        session.NewScene(directory / "Live/Scenes/Empty");
        drive(runner, session);
        check(errors.empty() && session.GetTitle() == "Live - Empty" &&
                  runner.GetScene().GetRootObjects().empty(),
              "new scene");
        session.NewScene(directory / "Live/Scenes/Empty");
        drive(runner, session);
        check(errors.size() == 1, "existing scene not overwritten");
        errors.clear();

        // 연속으로 열면 나중 것만 반영된다.
        session.OpenScene(directory / "Live/Scenes/Empty.srscene");
        session.OpenScene(directory / "Live/Scenes/Main.srscene");
        drive(runner, session);
        check(errors.empty() && session.GetTitle() == "Live - Main" &&
                  runner.GetScene().GetRootObjects().size() == 1,
              "latest open wins");
    }

    // 새 세션(앱을 다시 켠 것)에서 열면 같은 장면·설정이 같은 픽셀을 그린다.
    FrameRunner runner;
    sr::io::BackgroundLoader loader;
    project::ProjectSession session(runner, loader, onError);
    runner.SetFrameStartHook([&loader](FrameRunner &) { loader.DrainCompleted(); });
    session.OpenProject(projectFile);
    drive(runner, session);
    check(errors.empty(), "open errors: " + (errors.empty() ? std::string{} : errors.front()));
    check(session.GetProject()->scenes.size() == 3 && session.GetProject()->assets.size() == 1,
          "project contents");
    check(runner.GetRenderer().GetOptions().frontToBack, "render options restored");
    check(runner.GetScene().GetRootObjects().size() == 1, "one object");
    check(renderPixels(runner) == savedPixels, "reopened project renders identical pixels");

    session.OpenProject(directory / "Missing/Missing.srproj");
    drive(runner, session);
    check(errors.size() == 1 && session.GetTitle() == "Live - Main", "failed open keeps current project");
}
} // namespace

namespace sr::test
{
void RegisterProjectTests(TestRegistry &registry)
{
    registry.Add("project.json_round_trip", jsonRoundTripTests);
    registry.Add("project.format_errors", formatErrorTests);
    registry.Add("project.create", createProjectTests);
    registry.Add("project.import_and_build", importAndBuildTests);
    registry.Add("project.capture_round_trip", captureRoundTripTests);
    registry.Add("project.session", sessionTests);
}
} // namespace sr::test

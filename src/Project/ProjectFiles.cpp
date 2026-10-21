#include "Project/ProjectFiles.h"
#include "Graphics/Model.h"
#include "Graphics/ModelLoader.h"
#include "IO/FileIO.h"
#include "Scene/GameObject.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cwctype>
#include <limits>
#include <memory>
#include <mutex>
#include <random>
#include <system_error>
#include <unordered_map>
#include <utility>

namespace fs = std::filesystem;

namespace sr::project
{
namespace
{
using json::Value;
using Error = std::string;

// 프로젝트·씬 파일은 사람이 읽는 작은 JSON이다. 이보다 크면 잘못 고른 파일로 본다.
constexpr std::uintmax_t maxDocumentBytes = 64ull << 20;
constexpr std::string_view projectFormat = "srproj";
constexpr std::string_view sceneFormat = "srscene";
constexpr std::string_view startupSceneName = "Main";

[[nodiscard]] std::string toUtf8(const fs::path &path)
{
    const auto text = path.generic_u8string();
    return {text.begin(), text.end()};
}

[[nodiscard]] fs::path fromUtf8(std::string_view text)
{
    return fs::path(std::u8string(text.begin(), text.end()));
}

[[nodiscard]] std::wstring lowerExtension(const fs::path &path)
{
    std::wstring extension = path.extension().wstring();
    std::ranges::transform(extension, extension.begin(),
                           [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return extension;
}

// float를 최단 float 표현의 double로 쓴다. 0.04f가 0.03999999910593033이 아니라 0.04로 저장되고,
// 읽어서 float로 바꾸면 같은 비트가 돌아온다.
[[nodiscard]] Value floatValue(float number)
{
    std::array<char, 32> buffer{};
    const auto written = std::to_chars(buffer.data(), buffer.data() + buffer.size(), number);
    double shortest = number;
    std::from_chars(buffer.data(), written.ptr, shortest);
    return Value(shortest);
}

[[nodiscard]] Value vec3Value(const SRMath::vec3 &vector)
{
    return Value(Value::Array{floatValue(vector.x), floatValue(vector.y), floatValue(vector.z)});
}

// --- 읽기 도우미: 형식 오류는 "어디의 무엇" 문장으로 돌려준다. ---
[[nodiscard]] std::expected<float, Error> toFloat(const Value &value, const std::string &what)
{
    const double *number = value.AsNumber();
    if (!number)
        return std::unexpected(what + " must be a number");
    if (!std::isfinite(*number) || std::abs(*number) > std::numeric_limits<float>::max())
        return std::unexpected(what + " is out of range");
    return static_cast<float>(*number);
}

[[nodiscard]] std::expected<float, Error> getFloat(const Value &object, std::string_view key,
                                                   const std::string &where, float fallback)
{
    const Value *value = object.Find(key);
    if (!value)
        return fallback;
    return toFloat(*value, where + "." + std::string(key));
}

[[nodiscard]] std::expected<bool, Error> getBool(const Value &object, std::string_view key,
                                                 const std::string &where, bool fallback)
{
    const Value *value = object.Find(key);
    if (!value)
        return fallback;
    const bool *flag = value->AsBool();
    if (!flag)
        return std::unexpected(where + "." + std::string(key) + " must be true or false");
    return *flag;
}

// null과 없는 키는 빈 문자열이다(부모 없음, 메시 없음).
[[nodiscard]] std::expected<std::string, Error> getString(const Value &object, std::string_view key,
                                                          const std::string &where)
{
    const Value *value = object.Find(key);
    if (!value || value->IsNull())
        return std::string{};
    const std::string *text = value->AsString();
    if (!text)
        return std::unexpected(where + "." + std::string(key) + " must be a string");
    return *text;
}

[[nodiscard]] std::expected<SRMath::vec3, Error> getVec3(const Value &object, std::string_view key,
                                                         const std::string &where, SRMath::vec3 fallback)
{
    const Value *value = object.Find(key);
    if (!value)
        return fallback;
    const std::string what = where + "." + std::string(key);
    const Value::Array *array = value->AsArray();
    if (!array || array->size() != 3)
        return std::unexpected(what + " must be an array of 3 numbers");
    SRMath::vec3 result;
    for (std::size_t index = 0; index < 3; ++index)
    {
        const auto component = toFloat((*array)[index], what);
        if (!component)
            return std::unexpected(component.error());
        result[index] = *component;
    }
    return result;
}

[[nodiscard]] std::expected<void, Error> checkHeader(const Value &root, std::string_view format)
{
    if (!root.AsObject())
        return std::unexpected("top level must be an object");
    const Value *name = root.Find("format");
    if (!name || !name->AsString() || *name->AsString() != format)
        return std::unexpected("\"format\" must be \"" + std::string(format) + "\"");
    const Value *version = root.Find("version");
    if (!version || !version->AsNumber() || *version->AsNumber() != std::floor(*version->AsNumber()) ||
        *version->AsNumber() < 1)
        return std::unexpected("\"version\" must be a positive integer");
    if (*version->AsNumber() > formatVersion)
        return std::unexpected("file was saved by a newer renderer (version " +
                               std::to_string(static_cast<long long>(*version->AsNumber())) + ", supported " +
                               std::to_string(formatVersion) + ")");
    return {};
}

// 상대 경로 문자열 검사. ResolveProjectPath와 문서 검증이 같이 쓴다.
[[nodiscard]] std::expected<fs::path, Error> checkRelative(std::string_view relative)
{
    if (relative.empty())
        return std::unexpected("path is empty");
    // '\'는 Windows에서만 구분자라 저장 형식에서는 '/'만 쓴다.
    if (relative.find('\\') != std::string_view::npos)
        return std::unexpected("path must use '/' separators: " + std::string(relative));
    const fs::path path = fromUtf8(relative);
    if (path.is_absolute() || path.has_root_name() || path.has_root_directory())
        return std::unexpected("path must be relative to the project folder: " + std::string(relative));
    for (const auto &part : path)
        if (part == "..")
            return std::unexpected("path must stay inside the project folder: " + std::string(relative));
    return path;
}

[[nodiscard]] Value lightValue(const DirectionalLight &light)
{
    Value result;
    result.Set("type", "directional");
    result.Set("rayDirection", vec3Value(light.rayDirection));
    result.Set("color", vec3Value(light.color));
    return result;
}

[[nodiscard]] std::expected<DirectionalLight, Error> lightFromJson(const Value &value,
                                                                   const std::string &where)
{
    if (!value.AsObject())
        return std::unexpected(where + " must be an object");
    const auto type = getString(value, "type", where);
    if (!type)
        return std::unexpected(type.error());
    if (!type->empty() && *type != "directional")
        return std::unexpected(where + ".type \"" + *type + "\" is not supported");
    DirectionalLight light;
    const auto direction = getVec3(value, "rayDirection", where, light.rayDirection);
    if (!direction)
        return std::unexpected(direction.error());
    const auto color = getVec3(value, "color", where, light.color);
    if (!color)
        return std::unexpected(color.error());
    light.rayDirection = *direction;
    light.color = *color;
    return light;
}

[[nodiscard]] std::expected<EntityDesc, Error> entityFromJson(const Value &value, const std::string &where)
{
    if (!value.AsObject())
        return std::unexpected(where + " must be an object");
    EntityDesc entity;
    for (auto [key, target] : {std::pair{"id", &entity.id}, std::pair{"name", &entity.name},
                               std::pair{"parent", &entity.parent}, std::pair{"mesh", &entity.mesh}})
    {
        auto text = getString(value, key, where);
        if (!text)
            return std::unexpected(text.error());
        *target = std::move(*text);
    }
    if (entity.id.empty())
        return std::unexpected(where + ".id is required");

    if (const Value *transform = value.Find("transform"))
    {
        const std::string at = where + ".transform";
        if (!transform->AsObject())
            return std::unexpected(at + " must be an object");
        for (auto [key, target] :
             {std::pair{"position", &entity.position}, std::pair{"rotationEuler", &entity.rotationEuler},
              std::pair{"scale", &entity.scale}})
        {
            const auto vector = getVec3(*transform, key, at, *target);
            if (!vector)
                return std::unexpected(vector.error());
            *target = *vector;
        }
    }
    return entity;
}

[[nodiscard]] std::expected<Value, AssetLoadError> readJsonFile(const fs::path &file)
{
    auto text = io::ReadWholeFile(file, maxDocumentBytes);
    if (!text)
        return std::unexpected(std::move(text.error()));
    auto parsed = json::Parse(*text);
    if (!parsed)
        return std::unexpected(AssetLoadError{"JSON syntax error: " + parsed.error().message + " (column " +
                                                  std::to_string(parsed.error().column) + ")",
                                              file, parsed.error().line});
    return std::move(*parsed);
}

[[nodiscard]] std::expected<void, AssetLoadError> writeJsonFile(const fs::path &file, const Value &value)
{
    auto text = json::Write(value);
    if (!text)
        return std::unexpected(AssetLoadError{"Cannot serialize: " + text.error(), file});
    text->push_back('\n');
    return io::WriteFileAtomically(file, *text);
}

// OBJ의 mtllib 줄이 가리키는 파일 이름들. 가져올 때 .obj와 같이 복사한다.
[[nodiscard]] std::vector<std::string> materialLibraries(std::string_view objText)
{
    std::vector<std::string> names;
    while (!objText.empty())
    {
        const auto end = objText.find('\n');
        std::string_view line = objText.substr(0, end);
        objText = end == std::string_view::npos ? std::string_view{} : objText.substr(end + 1);
        const auto first = line.find_first_not_of(" \t");
        if (first == std::string_view::npos)
            continue;
        line.remove_prefix(first);
        if (!line.starts_with("mtllib") || line.size() < 7 || (line[6] != ' ' && line[6] != '\t'))
            continue;
        line.remove_prefix(7);
        while (!line.empty())
        {
            const auto start = line.find_first_not_of(" \t\r");
            if (start == std::string_view::npos)
                break;
            line.remove_prefix(start);
            const auto stop = line.find_first_of(" \t\r");
            names.emplace_back(line.substr(0, stop));
            line = stop == std::string_view::npos ? std::string_view{} : line.substr(stop);
        }
    }
    return names;
}

void captureObject(const GameObject &object, const std::string &parentId, CapturedScene &captured)
{
    const EntityInfo &info = object.GetEntityInfo();
    if (info.id.empty())
    {
        // 저장할 수 없는 물체는 하위 트리까지 통째로 센다.
        std::size_t count = 0;
        const auto countSubtree = [&count](const auto &self, const GameObject &node) -> void
        {
            ++count;
            for (const auto &child : node.GetChildren())
                if (child)
                    self(self, *child);
        };
        countSubtree(countSubtree, object);
        captured.skippedObjects += count;
        return;
    }
    captured.document.entities.push_back(EntityDesc{.id = info.id,
                                                    .name = info.name,
                                                    .parent = parentId,
                                                    .mesh = info.meshAsset,
                                                    .position = object.GetPosition(),
                                                    .rotationEuler = object.GetRotation(),
                                                    .scale = object.GetScale()});
    for (const auto &child : object.GetChildren())
        if (child)
            captureObject(*child, info.id, captured);
}
} // namespace

// --- ProjectDocument ---
const AssetEntry *ProjectDocument::FindAsset(std::string_view id) const noexcept
{
    const auto found = std::ranges::find(assets, id, &AssetEntry::id);
    return found == assets.end() ? nullptr : &*found;
}

RenderOptions ProjectDocument::GetRenderOptions(RenderOptions base) const noexcept
{
    base.frontToBack = frontToBack;
    base.hiZ = hiZ;
    base.hiddenWireframe = hiddenWireframe;
    return base;
}

void ProjectDocument::SetRenderOptions(const RenderOptions &options) noexcept
{
    frontToBack = options.frontToBack;
    hiZ = options.hiZ;
    hiddenWireframe = options.hiddenWireframe;
}

void ProjectDocument::AddScene(std::string relativePath)
{
    if (std::ranges::find(scenes, relativePath) == scenes.end())
        scenes.push_back(std::move(relativePath));
}

// --- JSON ---
Value ToJson(const ProjectDocument &project)
{
    Value root;
    root.Set("format", projectFormat);
    root.Set("version", formatVersion);
    root.Set("name", project.name);
    root.Set("startupScene", project.startupScene);
    Value::Array scenes;
    for (const auto &scene : project.scenes)
        scenes.emplace_back(scene);
    root.Set("scenes", std::move(scenes));
    Value render;
    render.Set("frontToBack", project.frontToBack);
    render.Set("hiZ", project.hiZ);
    render.Set("hiddenWireframe", project.hiddenWireframe);
    root.Set("render", std::move(render));
    Value::Array assets;
    for (const auto &asset : project.assets)
    {
        Value entry;
        entry.Set("id", asset.id);
        entry.Set("kind", asset.kind);
        entry.Set("path", asset.path);
        assets.push_back(std::move(entry));
    }
    root.Set("assets", std::move(assets));
    return root;
}

Value ToJson(const SceneDocument &scene)
{
    Value root;
    root.Set("format", sceneFormat);
    root.Set("version", formatVersion);
    root.Set("name", scene.name);
    root.Set("autoRotate", scene.autoRotate);
    Value camera;
    camera.Set("position", vec3Value(scene.camera.position));
    camera.Set("yaw", floatValue(scene.camera.yaw));
    camera.Set("pitch", floatValue(scene.camera.pitch));
    root.Set("camera", std::move(camera));
    Value::Array lights;
    for (const auto &light : scene.lights)
        lights.push_back(lightValue(light));
    root.Set("lights", std::move(lights));
    Value::Array entities;
    for (const auto &entity : scene.entities)
    {
        Value entry;
        entry.Set("id", entity.id);
        entry.Set("name", entity.name);
        entry.Set("parent", entity.parent.empty() ? Value(nullptr) : Value(entity.parent));
        Value transform;
        transform.Set("position", vec3Value(entity.position));
        transform.Set("rotationEuler", vec3Value(entity.rotationEuler));
        transform.Set("scale", vec3Value(entity.scale));
        entry.Set("transform", std::move(transform));
        entry.Set("mesh", entity.mesh.empty() ? Value(nullptr) : Value(entity.mesh));
        entities.push_back(std::move(entry));
    }
    root.Set("entities", std::move(entities));
    return root;
}

std::expected<ProjectDocument, std::string> ProjectFromJson(const Value &root)
{
    if (auto header = checkHeader(root, projectFormat); !header)
        return std::unexpected(header.error());
    ProjectDocument project;
    auto name = getString(root, "name", "project");
    if (!name)
        return std::unexpected(name.error());
    project.name = std::move(*name);
    auto startup = getString(root, "startupScene", "project");
    if (!startup)
        return std::unexpected(startup.error());
    project.startupScene = std::move(*startup);

    if (const Value *scenes = root.Find("scenes"))
    {
        if (!scenes->AsArray())
            return std::unexpected("project.scenes must be an array");
        for (const auto &scene : *scenes->AsArray())
        {
            const std::string *path = scene.AsString();
            if (!path)
                return std::unexpected("project.scenes entries must be strings");
            if (auto checked = checkRelative(*path); !checked)
                return std::unexpected("project.scenes: " + checked.error());
            project.AddScene(*path);
        }
    }
    if (!project.startupScene.empty())
    {
        if (auto checked = checkRelative(project.startupScene); !checked)
            return std::unexpected("project.startupScene: " + checked.error());
        project.AddScene(project.startupScene);
    }

    if (const Value *render = root.Find("render"))
    {
        if (!render->AsObject())
            return std::unexpected("project.render must be an object");
        for (auto [key, target] :
             {std::pair{"frontToBack", &project.frontToBack}, std::pair{"hiZ", &project.hiZ},
              std::pair{"hiddenWireframe", &project.hiddenWireframe}})
        {
            const auto flag = getBool(*render, key, "project.render", *target);
            if (!flag)
                return std::unexpected(flag.error());
            *target = *flag;
        }
    }

    if (const Value *assets = root.Find("assets"))
    {
        if (!assets->AsArray())
            return std::unexpected("project.assets must be an array");
        for (std::size_t index = 0; index < assets->AsArray()->size(); ++index)
        {
            const Value &entry = (*assets->AsArray())[index];
            const std::string where = "project.assets[" + std::to_string(index) + "]";
            if (!entry.AsObject())
                return std::unexpected(where + " must be an object");
            AssetEntry asset;
            for (auto [key, target] :
                 {std::pair{"id", &asset.id}, std::pair{"kind", &asset.kind}, std::pair{"path", &asset.path}})
            {
                auto text = getString(entry, key, where);
                if (!text)
                    return std::unexpected(text.error());
                *target = std::move(*text);
            }
            if (asset.id.empty())
                return std::unexpected(where + ".id is required");
            if (asset.kind != "model")
                return std::unexpected(where + ".kind \"" + asset.kind + "\" is not supported");
            if (auto checked = checkRelative(asset.path); !checked)
                return std::unexpected(where + ".path: " + checked.error());
            if (project.FindAsset(asset.id))
                return std::unexpected(where + ".id \"" + asset.id + "\" is duplicated");
            project.assets.push_back(std::move(asset));
        }
    }
    return project;
}

std::expected<SceneDocument, std::string> SceneFromJson(const Value &root)
{
    if (auto header = checkHeader(root, sceneFormat); !header)
        return std::unexpected(header.error());
    SceneDocument scene;
    auto name = getString(root, "name", "scene");
    if (!name)
        return std::unexpected(name.error());
    scene.name = std::move(*name);
    const auto rotate = getBool(root, "autoRotate", "scene", false);
    if (!rotate)
        return std::unexpected(rotate.error());
    scene.autoRotate = *rotate;

    if (const Value *camera = root.Find("camera"))
    {
        if (!camera->AsObject())
            return std::unexpected("scene.camera must be an object");
        const auto position = getVec3(*camera, "position", "scene.camera", scene.camera.position);
        if (!position)
            return std::unexpected(position.error());
        const auto yaw = getFloat(*camera, "yaw", "scene.camera", 0.f);
        if (!yaw)
            return std::unexpected(yaw.error());
        const auto pitch = getFloat(*camera, "pitch", "scene.camera", 0.f);
        if (!pitch)
            return std::unexpected(pitch.error());
        scene.camera = {*position, *yaw, *pitch};
    }

    if (const Value *lights = root.Find("lights"))
    {
        if (!lights->AsArray())
            return std::unexpected("scene.lights must be an array");
        for (std::size_t index = 0; index < lights->AsArray()->size(); ++index)
        {
            auto light =
                lightFromJson((*lights->AsArray())[index], "scene.lights[" + std::to_string(index) + "]");
            if (!light)
                return std::unexpected(light.error());
            scene.lights.push_back(*light);
        }
    }

    if (const Value *entities = root.Find("entities"))
    {
        if (!entities->AsArray())
            return std::unexpected("scene.entities must be an array");
        for (std::size_t index = 0; index < entities->AsArray()->size(); ++index)
        {
            auto entity = entityFromJson((*entities->AsArray())[index],
                                         "scene.entities[" + std::to_string(index) + "]");
            if (!entity)
                return std::unexpected(entity.error());
            scene.entities.push_back(std::move(*entity));
        }
    }
    return scene;
}

// --- 파일 ---
std::expected<ProjectDocument, AssetLoadError> LoadProjectFile(const fs::path &file)
{
    auto root = readJsonFile(file);
    if (!root)
        return std::unexpected(std::move(root.error()));
    auto project = ProjectFromJson(*root);
    if (!project)
        return std::unexpected(AssetLoadError{"Invalid project file: " + project.error(), file});
    return std::move(*project);
}

std::expected<void, AssetLoadError> SaveProjectFile(const fs::path &file, const ProjectDocument &project)
{
    return writeJsonFile(file, ToJson(project));
}

std::expected<SceneDocument, AssetLoadError> LoadSceneFile(const fs::path &file)
{
    auto root = readJsonFile(file);
    if (!root)
        return std::unexpected(std::move(root.error()));
    auto scene = SceneFromJson(*root);
    if (!scene)
        return std::unexpected(AssetLoadError{"Invalid scene file: " + scene.error(), file});
    return std::move(*scene);
}

std::expected<void, AssetLoadError> SaveSceneFile(const fs::path &file, const SceneDocument &scene)
{
    return writeJsonFile(file, ToJson(scene));
}

// --- 경로 ---
std::expected<std::string, std::string> ToProjectPath(const fs::path &root, const fs::path &path)
{
    std::error_code error;
    const fs::path base = fs::weakly_canonical(root, error);
    if (error)
        return std::unexpected("cannot resolve project folder: " + error.message());
    const fs::path target = fs::weakly_canonical(path, error);
    if (error)
        return std::unexpected("cannot resolve path: " + error.message());
    const fs::path relative = target.lexically_relative(base);
    if (relative.empty() || relative == ".")
        return std::unexpected("path is the project folder itself");
    if (*relative.begin() == "..")
        return std::unexpected("path is outside the project folder");
    return toUtf8(relative);
}

std::expected<fs::path, std::string> ResolveProjectPath(const fs::path &root, std::string_view relative)
{
    auto checked = checkRelative(relative);
    if (!checked)
        return std::unexpected(checked.error());
    return (root / *checked).lexically_normal();
}

std::string NewId()
{
    // 여러 스레드(프레임 루프, 로더 작업)가 부를 수 있어 생성기를 잠근다. 호출은 드물다.
    static std::mutex mutex;
    static std::mt19937_64 generator = []
    {
        std::random_device device;
        std::seed_seq seed{
            device(), device(), device(), device(),
            static_cast<unsigned>(std::chrono::steady_clock::now().time_since_epoch().count())};
        return std::mt19937_64(seed);
    }();
    std::uint64_t bits = 0;
    {
        const std::scoped_lock lock(mutex);
        bits = generator();
    }
    std::string id(16, '0');
    constexpr std::string_view digits = "0123456789abcdef";
    for (std::size_t index = 0; index < id.size(); ++index)
        id[id.size() - 1 - index] = digits[(bits >> (index * 4)) & 0xF];
    return id;
}

// --- 만들기 ---
SceneDocument MakeEmptyScene(std::string name)
{
    SceneDocument scene;
    scene.name = std::move(name);
    scene.lights.push_back(DirectionalLight{});
    return scene;
}

std::expected<CreatedProject, AssetLoadError> CreateProject(const fs::path &requestedFile)
{
    fs::path projectFile = requestedFile;
    if (lowerExtension(projectFile) != L".srproj")
        projectFile += projectExtension;
    const fs::path root = projectFile.parent_path();
    if (root.empty())
        return std::unexpected(AssetLoadError{"Project file needs a folder", projectFile});
    const std::string name = toUtf8(projectFile.stem());
    if (name.empty())
        return std::unexpected(AssetLoadError{"Project name is empty", projectFile});

    std::error_code error;
    if (fs::is_directory(root, error))
        for (const auto &entry : fs::directory_iterator(root, error))
            if (entry.is_regular_file(error) && lowerExtension(entry.path()) == L".srproj")
                return std::unexpected(AssetLoadError{"The folder already has a project", entry.path()});
    for (const std::string_view folder : {assetsFolder, cacheFolder, scenesFolder})
    {
        fs::create_directories(root / folder, error);
        if (error)
            return std::unexpected(AssetLoadError{"Cannot create folder: " + error.message(), root / folder});
    }

    const std::string scenePath =
        std::string(scenesFolder) + "/" + std::string(startupSceneName) + std::string(sceneExtension);
    const fs::path sceneFile = root / fromUtf8(scenePath);
    if (fs::exists(sceneFile, error))
        return std::unexpected(AssetLoadError{"Scene file already exists", sceneFile});

    CreatedProject created{.project = {}, .startupScene = MakeEmptyScene(std::string(startupSceneName))};
    if (auto saved = SaveSceneFile(sceneFile, created.startupScene); !saved)
        return std::unexpected(std::move(saved.error()));

    // 캐시는 원본에서 다시 만들 수 있으므로 프로젝트를 git에 넣을 때 뺀다. 이미 있으면 그대로 둔다.
    const fs::path gitignore = root / ".gitignore";
    if (!fs::exists(gitignore, error))
    {
        const std::string text = std::string(cacheFolder) + "/\n";
        if (auto written = io::WriteFileAtomically(gitignore, text); !written)
            return std::unexpected(std::move(written.error()));
    }

    created.project.name = name;
    created.project.startupScene = scenePath;
    created.project.AddScene(scenePath);
    if (auto saved = SaveProjectFile(projectFile, created.project); !saved)
        return std::unexpected(std::move(saved.error()));
    return created;
}

// --- 에셋 ---
std::expected<AssetEntry, AssetLoadError> ImportModelAsset(const fs::path &root,
                                                           const ProjectDocument &project,
                                                           const fs::path &source)
{
    if (lowerExtension(source) != L".obj")
        return std::unexpected(AssetLoadError{"Unsupported file type (only .obj for now)", source});
    std::error_code error;
    if (!fs::is_regular_file(source, error))
        return std::unexpected(AssetLoadError{"File not found", source});

    // 이미 프로젝트 안에 있는 파일은 복사하지 않는다(Assets에 직접 넣어 둔 파일 등).
    if (auto inside = ToProjectPath(root, source))
    {
        const auto existing = std::ranges::find(project.assets, *inside, &AssetEntry::path);
        if (existing != project.assets.end())
            return *existing;
        return AssetEntry{.id = NewId(), .path = std::move(*inside)};
    }

    const std::string stem = toUtf8(source.stem());
    fs::path folder;
    std::string folderName;
    for (int suffix = 1; suffix <= 1000; ++suffix)
    {
        folderName = suffix == 1 ? stem : stem + "-" + std::to_string(suffix);
        folder = root / assetsFolder / fromUtf8(folderName);
        if (!fs::exists(folder, error))
            break;
        folder.clear();
    }
    if (folder.empty())
        return std::unexpected(AssetLoadError{"Too many assets with the same name", source});
    fs::create_directories(folder, error);
    if (error)
        return std::unexpected(AssetLoadError{"Cannot create folder: " + error.message(), folder});

    const fs::path fileName = source.filename();
    fs::copy_file(source, folder / fileName, error);
    if (error)
        return std::unexpected(AssetLoadError{"Cannot copy file: " + error.message(), source});

    // .mtl은 obj 기준 상대 경로라 같은 상대 위치로 복사한다. 없거나 밖을 가리키면 건너뛴다
    // (ModelLoader가 누락 MTL을 기본 재질로 처리한다).
    if (const auto text = io::ReadWholeFile(source))
        for (const auto &library : materialLibraries(*text))
        {
            const auto relative = checkRelative(library);
            if (!relative)
                continue;
            const fs::path from = source.parent_path() / *relative;
            if (!fs::is_regular_file(from, error))
                continue;
            const fs::path to = folder / *relative;
            fs::create_directories(to.parent_path(), error);
            fs::copy_file(from, to, fs::copy_options::skip_existing, error);
            if (error)
                return std::unexpected(AssetLoadError{"Cannot copy material: " + error.message(), from});
        }

    return AssetEntry{.id = NewId(),
                      .path = std::string(assetsFolder) + "/" + folderName + "/" + toUtf8(fileName)};
}

// --- 런타임 장면 ---
std::expected<Scene, AssetLoadError> BuildScene(const SceneDocument &document, const ProjectDocument &project,
                                                const fs::path &root, std::stop_token stop)
{
    const auto &entities = document.entities;
    std::unordered_map<std::string_view, std::size_t> indexById;
    for (std::size_t index = 0; index < entities.size(); ++index)
    {
        if (entities[index].id.empty())
            return std::unexpected(AssetLoadError{"Entity without id"});
        if (!indexById.emplace(entities[index].id, index).second)
            return std::unexpected(AssetLoadError{"Duplicated entity id \"" + entities[index].id + "\""});
    }

    // 1. 물체와 모델: 파일 읽기와 파싱이 대부분의 시간이다.
    std::vector<std::shared_ptr<GameObject>> objects(entities.size());
    for (std::size_t index = 0; index < entities.size(); ++index)
    {
        if (stop.stop_requested())
            return std::unexpected(AssetLoadError{"Cancelled"});
        const EntityDesc &entity = entities[index];
        std::unique_ptr<Model> model;
        if (!entity.mesh.empty())
        {
            const AssetEntry *asset = project.FindAsset(entity.mesh);
            if (!asset)
                return std::unexpected(AssetLoadError{"Entity \"" + entity.name + "\" uses unknown asset \"" +
                                                      entity.mesh + "\""});
            const auto path = ResolveProjectPath(root, asset->path);
            if (!path)
                return std::unexpected(AssetLoadError{"Asset \"" + asset->id + "\": " + path.error()});
            auto loaded = ModelLoader::LoadOBJ(*path);
            if (!loaded)
                return std::unexpected(std::move(loaded.error()));
            model = std::move(*loaded);
        }
        objects[index] = std::make_shared<GameObject>(entity.position, entity.rotationEuler, entity.scale,
                                                      std::move(model));
        objects[index]->SetEntityInfo({.id = entity.id, .name = entity.name, .meshAsset = entity.mesh});
    }

    // 2. 계층: 부모를 먼저 장면에 넣는다. 파일 순서는 상관없고, 같은 부모의 자식은 파일 순서를 지킨다.
    Scene scene;
    enum class Mark : unsigned char
    {
        None,
        Visiting,
        Done
    };
    std::vector<Mark> marks(entities.size(), Mark::None);
    const auto attach = [&](const auto &self, std::size_t index) -> std::expected<void, AssetLoadError>
    {
        if (marks[index] == Mark::Done)
            return {};
        if (marks[index] == Mark::Visiting)
            return std::unexpected(
                AssetLoadError{"Entity \"" + entities[index].id + "\" is its own ancestor"});
        marks[index] = Mark::Visiting;
        std::shared_ptr<GameObject> parent;
        if (const std::string &parentId = entities[index].parent; !parentId.empty())
        {
            const auto found = indexById.find(parentId);
            if (found == indexById.end())
                return std::unexpected(AssetLoadError{"Entity \"" + entities[index].id +
                                                      "\" has unknown parent \"" + parentId + "\""});
            if (auto attached = self(self, found->second); !attached)
                return attached;
            parent = objects[found->second];
        }
        if (!scene.AddObject(objects[index], parent))
            return std::unexpected(AssetLoadError{"Cannot attach entity \"" + entities[index].id + "\""});
        marks[index] = Mark::Done;
        return {};
    };
    for (std::size_t index = 0; index < entities.size(); ++index)
        if (auto attached = attach(attach, index); !attached)
            return std::unexpected(std::move(attached.error()));

    Camera camera(document.camera.position);
    camera.SetCameraYaw(document.camera.yaw);
    camera.SetCameraPitch(document.camera.pitch);
    scene.SetCamera(camera);
    for (const auto &light : document.lights)
        scene.AddLight(light);
    scene.SetRotateMode(document.autoRotate);
    return scene;
}

CapturedScene CaptureScene(const Scene &scene, std::string name)
{
    CapturedScene captured;
    captured.document.name = std::move(name);
    captured.document.autoRotate = scene.IsRotateMode();
    const Camera &camera = scene.GetCamera();
    captured.document.camera = {camera.GetCameraPos(), camera.GetCameraYaw(), camera.GetCameraPitch()};
    captured.document.lights.assign(scene.GetLights().begin(), scene.GetLights().end());
    for (const auto &object : scene.GetRootObjects())
        if (object)
            captureObject(*object, {}, captured);
    return captured;
}
} // namespace sr::project

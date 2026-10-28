#include "Core/DemoScene.h"
#include "Graphics/Model.h"
#include "Graphics/ModelLoader.h"
#include "Math/AABB.h"
#include "Scene/GameObject.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <string_view>
#include <utility>

namespace
{
struct SceneObjectDesc
{
    std::string_view modelName;
    SRMath::vec3 position;
};

// 장면 구성은 이 표 하나로 정한다. 벤치마크·골든 이미지 기준이 바뀌므로 수정 시 골든을 갱신한다.
const std::array sceneObjects{SceneObjectDesc{"IronMan", {0.f, 0.f, 0.f}},
                              SceneObjectDesc{"teapot", {-10.f, 0.f, 0.f}}};
constexpr float sceneObjectScale = 0.04f;

Mesh CreateSphereMesh(float radius, int rings, int sectors, const Material &mat,
                      SRMath::vec3 scale = {1.f, 1.f, 1.f}, SRMath::vec3 offset = {0.f, 0.f, 0.f})
{
    Mesh mesh;
    mesh.material = mat;
    const float R = 1.0f / static_cast<float>(rings - 1);
    const float S = 1.0f / static_cast<float>(sectors - 1);

    mesh.vertices.reserve(rings * sectors);
    mesh.indices.reserve(rings * sectors * 6);

    for (int r = 0; r < rings; ++r)
    {
        for (int s = 0; s < sectors; ++s)
        {
            const float theta = -std::numbers::pi_v<float> * 0.5f + std::numbers::pi_v<float> * r * R;
            const float phi = 2.0f * std::numbers::pi_v<float> * s * S;
            const float y = std::sin(theta);
            const float cosTheta = std::cos(theta);
            const float x = std::cos(phi) * cosTheta;
            const float z = std::sin(phi) * cosTheta;

            Vertex vert{};
            vert.position = {offset.x + x * radius * scale.x, offset.y + y * radius * scale.y,
                             offset.z + z * radius * scale.z};
            vert.normal = SRMath::normalize(SRMath::vec3(x / scale.x, y / scale.y, z / scale.z));
            vert.texcoord = {s * S, r * R};
            mesh.vertices.push_back(vert);
        }
    }

    for (int r = 0; r < rings - 1; ++r)
    {
        for (int s = 0; s < sectors - 1; ++s)
        {
            const unsigned int i0 = r * sectors + s;
            const unsigned int i1 = r * sectors + (s + 1);
            const unsigned int i2 = (r + 1) * sectors + (s + 1);
            const unsigned int i3 = (r + 1) * sectors + s;

            mesh.indices.push_back(i0);
            mesh.indices.push_back(i1);
            mesh.indices.push_back(i2);

            mesh.indices.push_back(i0);
            mesh.indices.push_back(i2);
            mesh.indices.push_back(i3);
        }
    }

    mesh.localAABB = ComputeLocalAABB(mesh);
    return mesh;
}

Mesh CreateCylinderMesh(float radius, float height, int segments, const Material &mat,
                        SRMath::vec3 offset = {0.f, 0.f, 0.f})
{
    Mesh mesh;
    mesh.material = mat;
    const float halfH = height * 0.5f;
    const float step = 2.0f * std::numbers::pi_v<float> / segments;

    for (int i = 0; i <= segments; ++i)
    {
        const float angle = i * step;
        const float x = std::cos(angle) * radius;
        const float z = std::sin(angle) * radius;
        const float u = static_cast<float>(i) / segments;

        Vertex vTop{};
        vTop.position = {offset.x + x, offset.y + halfH, offset.z + z};
        vTop.normal = {x / radius, 0.f, z / radius};
        vTop.texcoord = {u, 1.f};
        mesh.vertices.push_back(vTop);

        Vertex vBottom{};
        vBottom.position = {offset.x + x, offset.y - halfH, offset.z + z};
        vBottom.normal = {x / radius, 0.f, z / radius};
        vBottom.texcoord = {u, 0.f};
        mesh.vertices.push_back(vBottom);
    }

    for (int i = 0; i < segments; ++i)
    {
        const unsigned int i0 = i * 2;
        const unsigned int i1 = i * 2 + 1;
        const unsigned int i2 = (i + 1) * 2 + 1;
        const unsigned int i3 = (i + 1) * 2;

        mesh.indices.push_back(i0);
        mesh.indices.push_back(i1);
        mesh.indices.push_back(i2);

        mesh.indices.push_back(i0);
        mesh.indices.push_back(i2);
        mesh.indices.push_back(i3);
    }

    mesh.localAABB = ComputeLocalAABB(mesh);
    return mesh;
}

Mesh CreateTorusMesh(float mainRadius, float tubeRadius, int mainSegments, int tubeSegments,
                     const Material &mat, SRMath::vec3 offset = {0.f, 0.f, 0.f})
{
    Mesh mesh;
    mesh.material = mat;

    for (int i = 0; i <= mainSegments; ++i)
    {
        const float u = static_cast<float>(i) / mainSegments;
        const float mainAngle = u * 2.0f * std::numbers::pi_v<float>;
        const float cosMain = std::cos(mainAngle);
        const float sinMain = std::sin(mainAngle);

        for (int j = 0; j <= tubeSegments; ++j)
        {
            const float v = static_cast<float>(j) / tubeSegments;
            const float tubeAngle = v * 2.0f * std::numbers::pi_v<float>;
            const float cosTube = std::cos(tubeAngle);
            const float sinTube = std::sin(tubeAngle);

            const float r = mainRadius + tubeRadius * cosTube;
            const float x = r * cosMain;
            const float y = tubeRadius * sinTube;
            const float z = r * sinMain;

            Vertex vert{};
            vert.position = {offset.x + x, offset.y + y, offset.z + z};
            vert.normal = {cosTube * cosMain, sinTube, cosTube * sinMain};
            vert.texcoord = {u, v};
            mesh.vertices.push_back(vert);
        }
    }

    for (int i = 0; i < mainSegments; ++i)
    {
        for (int j = 0; j < tubeSegments; ++j)
        {
            const unsigned int i0 = i * (tubeSegments + 1) + j;
            const unsigned int i1 = (i + 1) * (tubeSegments + 1) + j;
            const unsigned int i2 = (i + 1) * (tubeSegments + 1) + (j + 1);
            const unsigned int i3 = i * (tubeSegments + 1) + (j + 1);

            mesh.indices.push_back(i0);
            mesh.indices.push_back(i1);
            mesh.indices.push_back(i2);

            mesh.indices.push_back(i0);
            mesh.indices.push_back(i2);
            mesh.indices.push_back(i3);
        }
    }

    mesh.localAABB = ComputeLocalAABB(mesh);
    return mesh;
}
Mesh CreateTerrainMesh(float width, float depth, int gridX, int gridZ, float heightScale, const Material &mat)
{
    Mesh mesh;
    mesh.material = mat;
    mesh.vertices.reserve(gridX * gridZ);
    mesh.indices.reserve((gridX - 1) * (gridZ - 1) * 6);

    const float dx = width / static_cast<float>(gridX - 1);
    const float dz = depth / static_cast<float>(gridZ - 1);
    const float halfW = width * 0.5f;
    const float halfD = depth * 0.5f;

    auto evalHeight = [&](float x, float z) -> float {
        return (std::sin(0.18f * x) * std::cos(0.18f * z) * 3.5f +
                std::sin(0.45f * x + 1.2f) * std::sin(0.45f * z) * 1.2f +
                std::cos(0.95f * x) * std::cos(0.95f * z) * 0.4f +
                std::sin(2.1f * x) * std::sin(2.1f * z) * 0.15f) * heightScale;
    };

    constexpr float eps = 0.05f;
    for (int z = 0; z < gridZ; ++z)
    {
        const float posZ = -halfD + z * dz;
        const float v = static_cast<float>(z) / (gridZ - 1);
        for (int x = 0; x < gridX; ++x)
        {
            const float posX = -halfW + x * dx;
            const float u = static_cast<float>(x) / (gridX - 1);
            const float y = evalHeight(posX, posZ);

            const float hL = evalHeight(posX - eps, posZ);
            const float hR = evalHeight(posX + eps, posZ);
            const float hD = evalHeight(posX, posZ - eps);
            const float hU = evalHeight(posX, posZ + eps);

            const float dydx = (hR - hL) / (2.0f * eps);
            const float dydz = (hU - hD) / (2.0f * eps);
            const SRMath::vec3 norm = SRMath::normalize(SRMath::vec3(-dydx, 1.0f, -dydz));

            Vertex vert{};
            vert.position = {posX, y, posZ};
            vert.normal = norm;
            vert.texcoord = {u, v};
            mesh.vertices.push_back(vert);
        }
    }

    for (int z = 0; z < gridZ - 1; ++z)
    {
        for (int x = 0; x < gridX - 1; ++x)
        {
            const unsigned int i00 = z * gridX + x;
            const unsigned int i10 = z * gridX + (x + 1);
            const unsigned int i01 = (z + 1) * gridX + x;
            const unsigned int i11 = (z + 1) * gridX + (x + 1);

            mesh.indices.push_back(i00);
            mesh.indices.push_back(i10);
            mesh.indices.push_back(i11);

            mesh.indices.push_back(i00);
            mesh.indices.push_back(i11);
            mesh.indices.push_back(i01);
        }
    }

    mesh.localAABB = ComputeLocalAABB(mesh);
    return mesh;
}

Mesh CreateIcosahedronMesh(float radius, const Material &mat, SRMath::vec3 offset = {0.f, 0.f, 0.f})
{
    Mesh mesh;
    mesh.material = mat;
    const float phi = (1.0f + std::sqrt(5.0f)) * 0.5f;
    const float invLen = 1.0f / std::sqrt(1.0f + phi * phi);
    const float a = 1.0f * invLen * radius;
    const float b = phi * invLen * radius;

    const SRMath::vec3 rawVertices[12] = {
        {-a, b, 0.f}, {a, b, 0.f}, {-a, -b, 0.f}, {a, -b, 0.f},
        {0.f, -a, b}, {0.f, a, b}, {0.f, -a, -b}, {0.f, a, -b},
        {b, 0.f, -a}, {b, 0.f, a}, {-b, 0.f, -a}, {-b, 0.f, a}
    };

    constexpr unsigned int rawIndices[20][3] = {
        {0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11},
        {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
        {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9},
        {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}
    };

    mesh.vertices.reserve(12);
    for (int i = 0; i < 12; ++i)
    {
        Vertex v{};
        v.position = rawVertices[i] + offset;
        v.normal = SRMath::normalize(rawVertices[i]);
        v.texcoord = {0.5f, 0.5f};
        mesh.vertices.push_back(v);
    }

    mesh.indices.reserve(60);
    for (int i = 0; i < 20; ++i)
    {
        mesh.indices.push_back(rawIndices[i][0]);
        mesh.indices.push_back(rawIndices[i][1]);
        mesh.indices.push_back(rawIndices[i][2]);
    }

    mesh.localAABB = ComputeLocalAABB(mesh);
    return mesh;
}

} // namespace

namespace sr::demo
{
std::expected<Scene, AssetLoadError> LoadScene(const std::filesystem::path &assetDirectory)
{
    Scene scene;
    for (const auto &object : sceneObjects)
    {
        auto model = ModelLoader::LoadOBJ(assetDirectory / std::filesystem::path(object.modelName));
        if (!model)
            return std::unexpected(std::move(model.error()));
        // 새 루트 물체라 실패할 수 없다.
        [[maybe_unused]] const bool added = scene.AddObject(std::make_shared<GameObject>(
            object.position, SRMath::vec3(0.f, 0.f, 0.f),
            SRMath::vec3(sceneObjectScale, sceneObjectScale, sceneObjectScale), std::move(*model)));
    }
    scene.SetCamera(Camera(SRMath::vec3(0.f, 0.f, -5.f)));
    scene.AddLight(DirectionalLight());
    return scene;
}

void FitCameraToScene(Scene &scene, float aspectRatio)
{
    AABB sceneBounds;
    for (const auto &object : scene.GetRootObjects())
        sceneBounds.Encapsulate(object->GetWorldAABB());
    FitCameraToBounds(scene, sceneBounds, aspectRatio);
}

void FitCameraToBounds(Scene &scene, const AABB &sceneBounds, float aspectRatio)
{
    const auto center = sceneBounds.Center();
    const auto halfSize = (sceneBounds.max - sceneBounds.min) * 0.5f;
    const float distance =
        std::max(halfSize.y, halfSize.x / aspectRatio) / std::tan(std::numbers::pi_v<float> / 6.f) * 1.15f +
        halfSize.z;
    Camera &camera = scene.GetCamera();
    camera.SetCameraPos({center.x, center.y, center.z - distance});
    camera.SetCameraYaw(0.f);
    camera.SetCameraPitch(0.f);
}

static void SetupPhongFallback(Material &mat)
{
    mat.diffuse = {mat.baseColor.x, mat.baseColor.y, mat.baseColor.z};
    const float r2 = mat.roughness * mat.roughness;
    mat.shininess = std::clamp(2.0f / std::max(r2 * r2, 1e-4f) - 2.0f, 1.0f, 256.0f);
    mat.specular = SRMath::vec3(0.04f, 0.04f, 0.04f) * (1.0f - mat.metallic) + mat.diffuse * mat.metallic;
}

Scene CreatePbrSphereGridScene()
{
    Scene scene;
    constexpr int gridRows = 5;
    constexpr int gridCols = 5;
    constexpr float spacing = 2.4f;

    for (int row = 0; row < gridRows; ++row)
    {
        const float roughness = std::clamp(static_cast<float>(row) / (gridRows - 1), 0.05f, 1.0f);
        for (int col = 0; col < gridCols; ++col)
        {
            const float metallic = static_cast<float>(col) / (gridCols - 1);

            Material mat;
            mat.name = "PBR_Sphere_" + std::to_string(row) + "_" + std::to_string(col);
            mat.isPbr = true;
            mat.baseColor = {0.95f, 0.75f, 0.35f, 1.0f}; // Warm brass/gold
            mat.diffuse = {0.95f, 0.75f, 0.35f};
            mat.roughness = roughness;
            mat.metallic = metallic;
            mat.ambient = {0.05f, 0.05f, 0.05f};
            SetupPhongFallback(mat);

            std::vector<Mesh> meshes;
            meshes.push_back(CreateSphereMesh(0.9f, 24, 32, mat));
            auto model = ModelLoader::FromMeshes(std::move(meshes));

            const SRMath::vec3 pos{(col - (gridCols - 1) * 0.5f) * spacing,
                                   (row - (gridRows - 1) * 0.5f) * spacing,
                                   0.0f};

            [[maybe_unused]] const bool added = scene.AddObject(std::make_shared<GameObject>(
                pos, SRMath::vec3(0.f, 0.f, 0.f), SRMath::vec3(1.f, 1.f, 1.f), std::move(model)));
        }
    }

    scene.SetCamera(Camera(SRMath::vec3(0.f, 0.f, -14.f)));
    scene.AddLight(DirectionalLight(SRMath::vec3(0.5f, -0.8f, 1.0f), SRMath::vec3(1.0f, 0.98f, 0.95f)));
    return scene;
}

Scene CreatePbrShowcaseScene()
{
    Scene scene;
    std::vector<Mesh> helmetMeshes;

    // 1. Visor (초광택 골드 크롬 미러 코어: Metal=1.0, Roughness=0.04)
    Material visorMat;
    visorMat.name = "PBR_Visor_Gold";
    visorMat.isPbr = true;
    visorMat.baseColor = {1.0f, 0.76f, 0.33f, 1.0f};
    visorMat.diffuse = {1.0f, 0.76f, 0.33f};
    visorMat.roughness = 0.04f;
    visorMat.metallic = 1.0f;
    visorMat.ambient = {0.02f, 0.02f, 0.02f};
    SetupPhongFallback(visorMat);
    helmetMeshes.push_back(CreateSphereMesh(1.1f, 32, 48, visorMat, {1.0f, 0.85f, 1.25f}, {0.f, 0.f, 0.15f}));

    // 2. Armor Shell (무광 카본/세라믹 복합 외골격: Dielectric Metal=0.0, Roughness=0.50)
    Material armorMat;
    armorMat.name = "PBR_Armor_Carbon";
    armorMat.isPbr = true;
    armorMat.baseColor = {0.14f, 0.17f, 0.22f, 1.0f};
    armorMat.diffuse = {0.14f, 0.17f, 0.22f};
    armorMat.roughness = 0.50f;
    armorMat.metallic = 0.0f;
    armorMat.ambient = {0.04f, 0.04f, 0.04f};
    SetupPhongFallback(armorMat);
    helmetMeshes.push_back(CreateSphereMesh(1.25f, 32, 48, armorMat, {1.05f, 1.15f, 1.05f}, {0.f, 0.2f, -0.1f}));

    // 3. Neck / Collar Torus (티타늄 넥 칼라: Metal=0.9, Roughness=0.25)
    Material titaniumMat;
    titaniumMat.name = "PBR_Neck_Titanium";
    titaniumMat.isPbr = true;
    titaniumMat.baseColor = {0.72f, 0.75f, 0.78f, 1.0f};
    titaniumMat.diffuse = {0.72f, 0.75f, 0.78f};
    titaniumMat.roughness = 0.25f;
    titaniumMat.metallic = 0.90f;
    titaniumMat.ambient = {0.05f, 0.05f, 0.05f};
    SetupPhongFallback(titaniumMat);
    helmetMeshes.push_back(CreateTorusMesh(1.2f, 0.2f, 36, 20, titaniumMat, {0.f, -1.1f, 0.f}));

    // 4. Ear Pods (좌우 통신/센서 포드 실린더: Metal=0.85, Roughness=0.30)
    helmetMeshes.push_back(CreateCylinderMesh(0.45f, 0.5f, 24, titaniumMat, {-1.3f, 0.0f, 0.0f}));
    helmetMeshes.push_back(CreateCylinderMesh(0.45f, 0.5f, 24, titaniumMat, {1.3f, 0.0f, 0.0f}));

    // 5. Emissive Power Halo Ring (ACES 필름 톤매핑 검증용 HDR 앰버 발광 링: Emissive={3.2, 1.4, 0.35})
    Material glowMat;
    glowMat.name = "PBR_Glow_Amber";
    glowMat.isPbr = true;
    glowMat.baseColor = {0.1f, 0.05f, 0.02f, 1.0f};
    glowMat.diffuse = {0.1f, 0.05f, 0.02f};
    glowMat.emissive = {3.2f, 1.4f, 0.35f}; // HDR 광원
    glowMat.roughness = 0.10f;
    glowMat.metallic = 0.5f;
    glowMat.ambient = {0.1f, 0.1f, 0.1f};
    SetupPhongFallback(glowMat);
    helmetMeshes.push_back(CreateTorusMesh(0.95f, 0.08f, 36, 16, glowMat, {0.f, 0.7f, 0.35f}));

    auto model = ModelLoader::FromMeshes(std::move(helmetMeshes));
    [[maybe_unused]] const bool added = scene.AddObject(std::make_shared<GameObject>(
        SRMath::vec3(0.f, 0.f, 0.f), SRMath::vec3(0.f, 0.f, 0.f), SRMath::vec3(1.f, 1.f, 1.f), std::move(model)));

    scene.SetCamera(Camera(SRMath::vec3(0.f, 0.f, -5.5f)));
    scene.AddLight(DirectionalLight(SRMath::vec3(0.6f, -0.7f, 0.8f), SRMath::vec3(1.0f, 0.95f, 0.90f)));
    return scene;
}

Scene CreatePolygonalTerrainScene()
{
    Scene scene;
    Material terrainMat;
    terrainMat.name = "PBR_Terrain_Alpine";
    terrainMat.isPbr = true;
    terrainMat.baseColor = {0.52f, 0.50f, 0.46f, 1.0f};
    terrainMat.roughness = 0.78f;
    terrainMat.metallic = 0.05f;
    terrainMat.ambient = {0.06f, 0.06f, 0.06f};
    SetupPhongFallback(terrainMat);

    std::vector<Mesh> meshes;
    meshes.push_back(CreateTerrainMesh(40.0f, 40.0f, 160, 160, 1.0f, terrainMat));
    auto model = ModelLoader::FromMeshes(std::move(meshes));

    [[maybe_unused]] const bool added = scene.AddObject(std::make_shared<GameObject>(
        SRMath::vec3(0.f, -2.0f, 0.f), SRMath::vec3(0.f, 0.f, 0.f), SRMath::vec3(1.f, 1.f, 1.f), std::move(model)));

    Camera cam(SRMath::vec3(0.f, 10.0f, -22.0f));
    cam.SetCameraPitch(-0.35f);
    scene.SetCamera(cam);
    scene.AddLight(DirectionalLight(SRMath::vec3(0.65f, -0.6f, 0.45f), SRMath::vec3(1.0f, 0.94f, 0.85f)));
    return scene;
}

Scene CreateInstancedFieldScene()
{
    Scene scene;
    Material asteroidMat;
    asteroidMat.name = "PBR_Asteroid_Basalt";
    asteroidMat.isPbr = true;
    asteroidMat.baseColor = {0.28f, 0.30f, 0.35f, 1.0f};
    asteroidMat.roughness = 0.35f;
    asteroidMat.metallic = 0.70f;
    asteroidMat.ambient = {0.04f, 0.04f, 0.04f};
    SetupPhongFallback(asteroidMat);

    constexpr int layerCount = 12;
    constexpr int perLayer = 10;

    for (int layer = 0; layer < layerCount; ++layer)
    {
        const float depthZ = 3.0f + layer * 3.5f;
        const float spread = 1.0f + layer * 0.45f;
        for (int i = 0; i < perLayer; ++i)
        {
            const float angle = (i * 2.0f * std::numbers::pi_v<float> / perLayer) + layer * 0.4f;
            const float r = spread * (0.3f + 0.7f * std::sin(angle * 2.0f + layer));
            const float posX = r * std::cos(angle);
            const float posY = r * std::sin(angle);
            const float scale = 0.5f + 0.25f * std::cos(layer * 1.5f + i);

            std::vector<Mesh> meshes;
            meshes.push_back(CreateIcosahedronMesh(scale, asteroidMat));
            auto model = ModelLoader::FromMeshes(std::move(meshes));

            const SRMath::vec3 pos{posX, posY, depthZ};
            const SRMath::vec3 rot{i * 0.5f, layer * 0.3f, (i + layer) * 0.2f};
            [[maybe_unused]] const bool added = scene.AddObject(std::make_shared<GameObject>(
                pos, rot, SRMath::vec3(1.f, 1.f, 1.f), std::move(model)));
        }
    }

    scene.SetCamera(Camera(SRMath::vec3(0.f, 0.f, -1.0f)));
    scene.AddLight(DirectionalLight(SRMath::vec3(0.4f, -0.5f, 0.8f), SRMath::vec3(1.0f, 0.96f, 0.92f)));
    return scene;
}

Scene CreateMultiLightArenaScene()
{
    Scene scene;

    Material floorMat;
    floorMat.name = "PBR_Arena_Floor";
    floorMat.isPbr = true;
    floorMat.baseColor = {0.35f, 0.37f, 0.40f, 1.0f};
    floorMat.roughness = 0.45f;
    floorMat.metallic = 0.10f;
    floorMat.ambient = {0.02f, 0.02f, 0.02f};
    SetupPhongFallback(floorMat);

    std::vector<Mesh> floorMeshes;
    floorMeshes.push_back(CreateCylinderMesh(14.0f, 0.5f, 36, floorMat, {0.f, -0.25f, 0.f}));
    auto floorModel = ModelLoader::FromMeshes(std::move(floorMeshes));
    [[maybe_unused]] const bool addedFloor = scene.AddObject(std::make_shared<GameObject>(
        SRMath::vec3(0.f, 0.f, 0.f), SRMath::vec3(0.f, 0.f, 0.f), SRMath::vec3(1.f, 1.f, 1.f), std::move(floorModel)));

    Material altarMat;
    altarMat.name = "PBR_Arena_AltarGold";
    altarMat.isPbr = true;
    altarMat.baseColor = {0.98f, 0.82f, 0.35f, 1.0f};
    altarMat.roughness = 0.15f;
    altarMat.metallic = 0.95f;
    altarMat.ambient = {0.04f, 0.04f, 0.04f};
    SetupPhongFallback(altarMat);

    std::vector<Mesh> altarMeshes;
    altarMeshes.push_back(CreateCylinderMesh(2.5f, 1.2f, 32, altarMat, {0.f, 0.6f, 0.f}));
    altarMeshes.push_back(CreateTorusMesh(1.8f, 0.35f, 32, 16, altarMat, {0.f, 2.2f, 0.f}));
    auto altarModel = ModelLoader::FromMeshes(std::move(altarMeshes));
    [[maybe_unused]] const bool addedAltar = scene.AddObject(std::make_shared<GameObject>(
        SRMath::vec3(0.f, 0.f, 0.f), SRMath::vec3(0.f, 0.f, 0.f), SRMath::vec3(1.f, 1.f, 1.f), std::move(altarModel)));

    Material pillarMat;
    pillarMat.name = "PBR_Arena_PillarBronze";
    pillarMat.isPbr = true;
    pillarMat.baseColor = {0.65f, 0.45f, 0.28f, 1.0f};
    pillarMat.roughness = 0.35f;
    pillarMat.metallic = 0.85f;
    pillarMat.ambient = {0.03f, 0.03f, 0.03f};
    SetupPhongFallback(pillarMat);

    constexpr int pillarCount = 8;
    constexpr float pillarRadius = 9.0f;
    for (int i = 0; i < pillarCount; ++i)
    {
        const float angle = i * 2.0f * std::numbers::pi_v<float> / pillarCount;
        const float px = pillarRadius * std::cos(angle);
        const float pz = pillarRadius * std::sin(angle);

        std::vector<Mesh> pillarMeshes;
        pillarMeshes.push_back(CreateCylinderMesh(0.7f, 5.0f, 24, pillarMat, {0.f, 2.5f, 0.f}));
        pillarMeshes.push_back(CreateTorusMesh(0.9f, 0.2f, 24, 12, altarMat, {0.f, 5.0f, 0.f}));
        auto pillarModel = ModelLoader::FromMeshes(std::move(pillarMeshes));

        [[maybe_unused]] const bool addedPillar = scene.AddObject(std::make_shared<GameObject>(
            SRMath::vec3(px, 0.f, pz), SRMath::vec3(0.f, 0.f, 0.f), SRMath::vec3(1.f, 1.f, 1.f), std::move(pillarModel)));
    }

    scene.AddLight(DirectionalLight(SRMath::vec3(0.5f, -0.8f, 0.4f), SRMath::vec3(0.95f, 0.90f, 0.85f)));

    PointLight redLight{{-6.0f, 2.5f, -6.0f}, {1.8f, 0.25f, 0.25f}, 16.0f, 1.0f, 0.09f, 0.032f};
    PointLight greenLight{{6.0f, 2.5f, -6.0f}, {0.25f, 1.8f, 0.35f}, 16.0f, 1.0f, 0.09f, 0.032f};
    PointLight blueLight{{6.0f, 2.5f, 6.0f}, {0.35f, 0.5f, 2.0f}, 16.0f, 1.0f, 0.09f, 0.032f};
    PointLight purpleLight{{-6.0f, 2.5f, 6.0f}, {1.6f, 0.3f, 1.8f}, 16.0f, 1.0f, 0.09f, 0.032f};
    scene.AddPointLight(redLight);
    scene.AddPointLight(greenLight);
    scene.AddPointLight(blueLight);
    scene.AddPointLight(purpleLight);

    SpotLight spotLight{{0.0f, 8.5f, 0.0f}, {0.0f, -1.0f, 0.0f}, {2.2f, 2.0f, 1.5f}, 18.0f,
                        0.92f, 0.82f, 1.0f, 0.07f, 0.017f};
    scene.AddSpotLight(spotLight);

    Camera cam(SRMath::vec3(0.f, 9.0f, -18.0f));
    cam.SetCameraPitch(-0.35f);
    scene.SetCamera(cam);
    return scene;
}

} // namespace sr::demo

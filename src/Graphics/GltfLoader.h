#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "IO/Gltf.h"
#include "Math/AABB.h"
#include "Math/SRMath.h"
#include "Utils/AssetLoadError.h"

class GameObject;
class Texture;

// glTF 문서(IO/Gltf)를 렌더러 장면 조각으로 바꾼다. 작업 스레드에서 불러도 된다:
// 결과의 GameObject 트리는 아직 어떤 Scene에도 속하지 않으므로 프레임 스레드에서 Scene::AddObject(root)로
// 넣는다.
//   - glTF node 하나 = GameObject 하나(mesh 없는 node는 model 없는 그룹 노드). 루트는 항상 그룹 노드다.
//   - mesh의 primitive 하나 = Mesh 하나. 같은 mesh를 쓰는 node마다 Model을 따로 만든다(Model은 Octree를
//   독점한다).
//   - 삼각형만 그린다. points/lines primitive는 경고와 함께 건너뛴다.
//   - 머티리얼은 baseColor(인자·텍스처)를 Phong 재질로 근사한다.
//   - skin/animation은 데이터만 꺼내 둔다. 재생·스키닝은 sr::anim::Animator::Create(LoadedScene)가 붙인다.
namespace sr::gltf
{
// 이미지 바이트를 RGBA Texture로 디코딩한다. 비어 있으면 기본 디코더(현재 stb_image, 3단계에서 자체 디코더로
// 교체)를 쓴다.
using ImageDecoder = std::function<std::expected<std::shared_ptr<::Texture>, AssetLoadError>(
    std::span<const std::byte> bytes, std::string_view mimeType)>;

struct SceneLoadOptions
{
    LoadOptions file{};
    int scene = none; // none이면 문서의 기본 scene, 그것도 없으면 0번, scene이 없으면 모든 루트 node
    ImageDecoder decodeImage{};
};

struct SkinData
{
    std::string name;
    std::vector<int> joints; // glTF node 인덱스(LoadedScene::nodes 인덱스와 같다)
    std::vector<SRMath::mat4> inverseBindMatrices; // joints와 같은 길이
    int skeleton = none;
};

// 한 node의 한 속성(translation/rotation/scale/weights) 키프레임. 값은 문서 그대로다(rotation은 쿼터니언
// xyzw).
struct AnimationTrack
{
    int node = none;
    AnimationPath path = AnimationPath::Translation;
    Interpolation interpolation = Interpolation::Linear;
    int componentCount = 3;    // translation/scale 3, rotation 4, weights는 모프 타깃 수
    std::vector<float> times;  // 초, 오름차순
    std::vector<float> values; // times.size() * componentCount (CUBICSPLINE이면 그 3배: in, value, out)
};

struct AnimationClip
{
    std::string name;
    std::vector<AnimationTrack> tracks;
    float duration = 0.f; // 가장 늦은 키 시간
};

// JOINTS_0/WEIGHTS_0. Model의 Mesh 하나에 대응하며 정점 순서가 같다.
struct VertexSkinWeights
{
    std::vector<std::array<std::uint16_t, 4>> joints; // skin.joints 안의 인덱스
    std::vector<std::array<float, 4>> weights;
};

struct NodeBinding
{
    std::shared_ptr<GameObject> object; // 선택한 scene에 없는 node면 null
    int mesh = none;
    int skin = none;
};

struct LoadedScene
{
    std::shared_ptr<GameObject> root; // 변환 없는 그룹 노드. 배치(위치·크기)는 호출자가 정한다
    AABB bounds;                      // root 기준 전체 경계. 그릴 것이 없으면 IsValid()가 false
    std::vector<NodeBinding> nodes; // glTF node 순서
    // [glTF mesh][그 mesh로 만든 Model의 Mesh 순서]. skin 정보가 없는 primitive는 빈 벡터다.
    std::vector<std::vector<VertexSkinWeights>> meshSkinWeights;
    std::vector<SkinData> skins;
    std::vector<AnimationClip> animations;
    std::vector<std::string> warnings; // 건너뛴 기능, 디코딩 못 한 텍스처 등. 장면은 그대로 쓸 수 있다
};

[[nodiscard]] std::expected<LoadedScene, AssetLoadError> LoadScene(const std::filesystem::path &path,
                                                                   const SceneLoadOptions &options = {});
[[nodiscard]] std::expected<LoadedScene, AssetLoadError> BuildScene(const Document &document,
                                                                    const SceneLoadOptions &options = {});

// glTF 단위 쿼터니언(x, y, z, w)을 GameObject 회전 표현(축 * 각도, 라디안)으로 바꾼다.
[[nodiscard]] SRMath::vec3 QuaternionToAxisAngle(const std::array<float, 4> &quaternion) noexcept;
} // namespace sr::gltf

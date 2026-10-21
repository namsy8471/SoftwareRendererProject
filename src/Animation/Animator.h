#pragma once

#include <cstddef>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "Graphics/GltfLoader.h"
#include "Math/AABB.h"
#include "Math/SRMath.h"

class GameObject;

// glTF 한 장면(LoadedScene)의 재생기. 클립의 키프레임을 node GameObject의 로컬 TRS로 쓰고, skin이 있는
// mesh node에 SkinnedMesh를 붙여 매 프레임 변형한다. Scene이 소유하며 Scene::Update가 아래 순서로 부른다:
//   ApplyPose(dt) → 계층 transform 갱신 → UpdateSkins().
// 고정 dt(벤치·골든)면 재생 시간도 고정되어 픽셀이 실행마다 같다. 모든 호출은 프레임 스레드에서 한다
// (Create는 장면에 넣기 전이면 작업 스레드에서 불러도 된다).
namespace sr::anim
{
class Animator
{
  public:
    // scene.root 아래 node들에 SkinnedMesh를 붙이고, 첫 클립(있으면)의 0초 포즈를 적용해 스킨까지 한 번
    // 갱신한다. 그래서 GetBounds()가 바로 실제 포즈의 경계를 준다. 모프 weights 트랙은 경고와 함께 무시한다.
    [[nodiscard]] static std::expected<std::shared_ptr<Animator>, std::string> Create(
        const sr::gltf::LoadedScene &scene);

    [[nodiscard]] std::size_t GetClipCount() const noexcept { return m_clips.size(); }
    [[nodiscard]] std::string_view GetClipName(std::size_t clip) const noexcept;
    // 클립이 없으면 -1.
    [[nodiscard]] int GetClip() const noexcept { return m_clip; }
    // 다른 클립으로 바꾸면 모든 node를 원래(로드 시) 포즈로 되돌린 뒤 0초부터 시작한다. 범위 밖은 무시한다.
    void SetClip(std::size_t clip);
    [[nodiscard]] float GetDuration() const noexcept;

    [[nodiscard]] bool IsPlaying() const noexcept { return m_playing; }
    void SetPlaying(bool playing) noexcept { m_playing = playing; }
    // 반복이 꺼져 있으면 끝에서 멈추고 IsPlaying()이 false가 된다. 다시 재생하면 처음부터 시작한다.
    [[nodiscard]] bool IsLooping() const noexcept { return m_looping; }
    void SetLooping(bool looping) noexcept { m_looping = looping; }
    [[nodiscard]] float GetSpeed() const noexcept { return m_speed; }
    void SetSpeed(float speed) noexcept { m_speed = speed; }
    [[nodiscard]] float GetTime() const noexcept { return m_time; }
    // 반복이면 [0, duration)으로 감고, 아니면 [0, duration]으로 자른다.
    void SetTime(float seconds) noexcept;

    // 재생 중이면 시간을 deltaTime * speed만큼 진행하고, 시간이 바뀌었거나 클립이 바뀌었으면 포즈를 쓴다.
    void ApplyPose(float deltaTime);
    // 계층 transform이 갱신된 뒤 부른다.
    void UpdateSkins();

    [[nodiscard]] std::span<const std::shared_ptr<GameObject>> GetSkinnedObjects() const noexcept
    {
        return m_skinnedObjects;
    }
    // 스킨 메시는 이번 변형 결과, 나머지는 world AABB의 합(마지막 UpdateSkins 기준).
    [[nodiscard]] AABB GetBounds() const;
    [[nodiscard]] std::span<const std::string> GetWarnings() const noexcept { return m_warnings; }

  private:
    // 로드 시 로컬 TRS. GltfLoader는 회전을 축 * 각도로 넣으므로 그 표현 그대로 되돌린다.
    struct RestPose
    {
        SRMath::vec3 position;
        SRMath::vec3 rotation;
        SRMath::vec3 scale;
    };

    std::vector<std::shared_ptr<GameObject>> m_nodes; // glTF node 순서. 선택한 scene 밖 node는 null
    std::vector<RestPose> m_restPose;
    std::vector<sr::gltf::AnimationClip> m_clips;
    std::vector<std::shared_ptr<GameObject>> m_skinnedObjects;
    std::vector<std::string> m_warnings;
    int m_clip = -1;
    float m_time = 0.f;
    float m_speed = 1.f;
    bool m_playing = true;
    bool m_looping = true;
    bool m_poseDirty = true;

    void restoreRestPose();
    void writePose();
};
} // namespace sr::anim

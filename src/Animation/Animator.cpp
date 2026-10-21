#include "Animation/Animator.h"
#include "Animation/AnimationSampling.h"
#include "Animation/Skinning.h"
#include "Graphics/Model.h"
#include "Scene/GameObject.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace sr::anim
{
namespace
{
using sr::gltf::AnimationPath;
} // namespace

std::expected<std::shared_ptr<Animator>, std::string> Animator::Create(const sr::gltf::LoadedScene &scene)
{
    auto animator = std::make_shared<Animator>();
    animator->m_nodes.reserve(scene.nodes.size());
    animator->m_restPose.reserve(scene.nodes.size());
    for (const auto &binding : scene.nodes)
    {
        animator->m_nodes.push_back(binding.object);
        RestPose pose{};
        if (binding.object)
            pose = {binding.object->GetPosition(), binding.object->GetRotation(), binding.object->GetScale()};
        animator->m_restPose.push_back(pose);
    }

    for (std::size_t n = 0; n < scene.nodes.size(); ++n)
    {
        const auto &binding = scene.nodes[n];
        if (binding.skin == sr::gltf::none || binding.mesh == sr::gltf::none || !binding.object ||
            !binding.object->GetModel())
            continue;
        const auto skinIndex = static_cast<std::size_t>(binding.skin);
        const auto meshIndex = static_cast<std::size_t>(binding.mesh);
        if (skinIndex >= scene.skins.size() || meshIndex >= scene.meshSkinWeights.size())
            return std::unexpected("node " + std::to_string(n) + " refers to a missing skin or mesh");
        const auto &skin = scene.skins[skinIndex];
        std::vector<std::weak_ptr<GameObject>> joints;
        joints.reserve(skin.joints.size());
        for (const int joint : skin.joints)
        {
            if (joint < 0 || static_cast<std::size_t>(joint) >= animator->m_nodes.size())
                return std::unexpected("skin " + std::to_string(skinIndex) + " joint is not a node");
            joints.push_back(animator->m_nodes[static_cast<std::size_t>(joint)]);
        }
        auto skinned = SkinnedMesh::Create(*binding.object->GetModel(), scene.meshSkinWeights[meshIndex],
                                           std::move(joints), skin.inverseBindMatrices);
        if (!skinned)
            return std::unexpected("node " + std::to_string(n) + ": " + skinned.error());
        binding.object->SetSkinnedMesh(std::move(*skinned));
        animator->m_skinnedObjects.push_back(binding.object);
    }

    animator->m_clips = scene.animations;
    for (const auto &clip : animator->m_clips)
        for (const auto &track : clip.tracks)
            if (track.path == AnimationPath::Weights)
            {
                animator->m_warnings.push_back("animation '" + clip.name +
                                               "': morph target weights are not supported, track ignored");
                break;
            }
    if (!animator->m_clips.empty())
        animator->m_clip = 0;

    // 로드 직후의 실제 포즈(0초)로 행렬과 스킨을 맞춘다. 장면에 넣기 전이라 root가 계층의 맨 위다.
    animator->ApplyPose(0.f);
    if (scene.root)
        scene.root->Update(0.f, false);
    animator->UpdateSkins();
    return animator;
}

std::string_view Animator::GetClipName(std::size_t clip) const noexcept
{
    return clip < m_clips.size() ? std::string_view{m_clips[clip].name} : std::string_view{};
}

void Animator::SetClip(std::size_t clip)
{
    if (clip >= m_clips.size() || static_cast<int>(clip) == m_clip)
        return;
    restoreRestPose();
    m_clip = static_cast<int>(clip);
    m_time = 0.f;
    m_poseDirty = true;
}

float Animator::GetDuration() const noexcept
{
    return m_clip >= 0 ? m_clips[static_cast<std::size_t>(m_clip)].duration : 0.f;
}

void Animator::SetTime(float seconds) noexcept
{
    const float duration = GetDuration();
    if (!std::isfinite(seconds) || duration <= 0.f)
        seconds = 0.f;
    else if (m_looping)
    {
        seconds = std::fmod(seconds, duration);
        if (seconds < 0.f)
            seconds += duration;
    }
    else
        seconds = std::clamp(seconds, 0.f, duration);
    if (seconds != m_time)
        m_poseDirty = true;
    m_time = seconds;
}

void Animator::ApplyPose(float deltaTime)
{
    if (m_clip < 0)
        return;
    if (m_playing && deltaTime != 0.f)
    {
        const float duration = GetDuration();
        if (!m_looping && m_time >= duration)
            SetTime(0.f); // 끝에서 멈춘 클립을 다시 재생하면 처음부터
        const float next = m_time + deltaTime * m_speed;
        SetTime(next);
        if (!m_looping && next >= duration)
            m_playing = false;
    }
    if (m_poseDirty)
        writePose();
}

void Animator::writePose()
{
    m_poseDirty = false;
    const auto &clip = m_clips[static_cast<std::size_t>(m_clip)];
    for (const auto &track : clip.tracks)
    {
        if (track.node < 0 || static_cast<std::size_t>(track.node) >= m_nodes.size())
            continue;
        GameObject *node = m_nodes[static_cast<std::size_t>(track.node)].get();
        if (!node)
            continue;
        const auto value = SampleTrack(track, m_time);
        switch (track.path)
        {
        case AnimationPath::Translation:
            node->SetPosition({value[0], value[1], value[2]});
            break;
        case AnimationPath::Rotation:
            node->SetOrientation({value[0], value[1], value[2], value[3]});
            break;
        case AnimationPath::Scale:
            node->SetScale({value[0], value[1], value[2]});
            break;
        case AnimationPath::Weights:
            break;
        }
    }
}

void Animator::restoreRestPose()
{
    for (std::size_t n = 0; n < m_nodes.size(); ++n)
    {
        GameObject *node = m_nodes[n].get();
        if (!node)
            continue;
        const RestPose &pose = m_restPose[n];
        node->SetPosition(pose.position);
        node->SetRotation(pose.rotation);
        node->SetScale(pose.scale);
    }
}

void Animator::UpdateSkins()
{
    for (const auto &object : m_skinnedObjects)
        object->GetSkinnedMesh()->Update(object->GetWorldMatrix());
}

AABB Animator::GetBounds() const
{
    AABB bounds;
    for (const auto &node : m_nodes)
    {
        if (!node)
            continue;
        if (const auto *skinned = node->GetSkinnedMesh())
        {
            if (skinned->GetWorldAABB().IsValid())
                bounds.Encapsulate(skinned->GetWorldAABB());
        }
        else if (node->GetModel())
            bounds.Encapsulate(node->GetWorldAABB());
    }
    return bounds;
}
} // namespace sr::anim

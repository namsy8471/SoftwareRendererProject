#include "Scene/EditorState.h"
#include "Animation/Skinning.h"
#include "Renderer/RenderCommand.h"
#include "Renderer/RenderQueue.h"
#include "Scene/GameObject.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace sr::editor
{
namespace
{
void accumulateBounds(const GameObject &object, AABB &bounds) noexcept
{
    if (const auto *skinned = object.GetSkinnedMesh())
    {
        if (skinned->GetWorldAABB().IsValid())
            bounds.Encapsulate(skinned->GetWorldAABB());
    }
    else if (object.GetWorldAABB().IsValid())
    {
        bounds.Encapsulate(object.GetWorldAABB());
    }
    for (const auto &child : object.GetChildren())
        if (child)
            accumulateBounds(*child, bounds);
}

// 반직선 R(s) = rayOrigin + s * rayDir (s >= 0)와 선분 S(u) = segStart + u * (segEnd - segStart) (0 <= u <= 1) 사이의 최단 거리.
[[nodiscard]] float distanceRayToSegment(const Ray &ray, const SRMath::vec3 &segStart,
                                         const SRMath::vec3 &segEnd) noexcept
{
    const SRMath::vec3 d1 = ray.direction;
    const SRMath::vec3 d2 = segEnd - segStart;
    const SRMath::vec3 r = ray.origin - segStart;

    const float a = SRMath::dot(d1, d1);
    const float e = SRMath::dot(d2, d2);
    if (a <= 1e-12f || e <= 1e-12f)
        return std::numeric_limits<float>::max();

    const float f = SRMath::dot(d2, r);
    const float c = SRMath::dot(d1, r);
    const float b = SRMath::dot(d1, d2);
    const float denom = a * e - b * b;

    float s = denom > 1e-7f ? std::max(0.0f, (b * f - c * e) / denom) : 0.0f;
    float u = (b * s + f) / e;

    if (u < 0.0f)
    {
        u = 0.0f;
        s = std::max(0.0f, -c / a);
    }
    else if (u > 1.0f)
    {
        u = 1.0f;
        s = std::max(0.0f, (b - c) / a);
    }

    const SRMath::vec3 closestOnRay = ray.origin + d1 * s;
    const SRMath::vec3 closestOnSeg = segStart + d2 * u;
    return SRMath::length(closestOnRay - closestOnSeg);
}

void addLine(DebugPrimitiveCommand &command, const SRMath::vec3 &from, const SRMath::vec3 &to,
             const SRMath::vec4 &color)
{
    command.vertices.push_back({from, color});
    command.vertices.push_back({to, color});
}
} // namespace

AABB ComputeObjectWorldBounds(const GameObject &object) noexcept
{
    AABB bounds;
    accumulateBounds(object, bounds);
    return bounds;
}

void EditorState::Select(std::shared_ptr<GameObject> object) noexcept
{
    m_selectedObject = std::move(object);
    m_activeAxis = GizmoAxis::None;
    m_isEditingTransform = false;
    m_editingObject.reset();
}

void EditorState::ClearSelection() noexcept
{
    m_selectedObject.reset();
    m_activeAxis = GizmoAxis::None;
    m_isEditingTransform = false;
    m_editingObject.reset();
}

SRMath::vec3 EditorState::GetGizmoOrigin() const noexcept
{
    if (!m_selectedObject)
        return {0.f, 0.f, 0.f};
    const AABB bounds = ComputeObjectWorldBounds(*m_selectedObject);
    if (bounds.IsValid())
        return bounds.Center();
    const auto &world = m_selectedObject->GetWorldMatrix();
    return {world.cols[3][0], world.cols[3][1], world.cols[3][2]};
}

float EditorState::GetGizmoAxisLength() const noexcept
{
    if (!m_selectedObject)
        return 1.0f;
    const AABB bounds = ComputeObjectWorldBounds(*m_selectedObject);
    if (!bounds.IsValid())
        return 1.0f;
    const float radius = SRMath::length(bounds.max - bounds.min) * 0.5f;
    return std::max(0.5f, radius * 1.2f);
}

GizmoAxis EditorState::PickGizmoAxis(const Ray &ray, float thresholdRatio) const noexcept
{
    if (!m_selectedObject)
        return GizmoAxis::None;

    const SRMath::vec3 origin = GetGizmoOrigin();
    const float axisLength = GetGizmoAxisLength();
    const float maxDistance = std::max(0.01f, axisLength * thresholdRatio);

    const std::array<std::pair<GizmoAxis, SRMath::vec3>, 3> axes{{
        {GizmoAxis::X, origin + SRMath::vec3{axisLength, 0.f, 0.f}},
        {GizmoAxis::Y, origin + SRMath::vec3{0.f, axisLength, 0.f}},
        {GizmoAxis::Z, origin + SRMath::vec3{0.f, 0.f, axisLength}},
    }};

    GizmoAxis bestAxis = GizmoAxis::None;
    float bestDist = maxDistance;
    for (const auto &[axis, endPoint] : axes)
    {
        const float dist = distanceRayToSegment(ray, origin, endPoint);
        if (dist < bestDist)
        {
            bestDist = dist;
            bestAxis = axis;
        }
    }
    return bestAxis;
}

TransformSnapshot EditorState::captureSnapshot(const GameObject &object) noexcept
{
    return TransformSnapshot{.position = object.GetPosition(),
                             .rotation = object.GetRotation(),
                             .orientation = object.GetOrientation(),
                             .useOrientation = object.HasOrientation(),
                             .scale = object.GetScale()};
}

void EditorState::restoreSnapshot(GameObject &object, const TransformSnapshot &snapshot) noexcept
{
    object.SetPosition(snapshot.position);
    object.SetRotation(snapshot.rotation);
    if (snapshot.useOrientation)
        object.SetOrientation(snapshot.orientation);
    object.SetScale(snapshot.scale);
    object.Update(0.f, false);
}

void EditorState::pushHistory(std::shared_ptr<GameObject> object, const TransformSnapshot &before,
                              const TransformSnapshot &after)
{
    if (!object || before == after)
        return;
    if (m_undoStack.size() >= maxHistoryEntries)
        m_undoStack.pop_front();
    m_undoStack.push_back(HistoryEntry{.object = std::move(object), .before = before, .after = after});
    m_redoStack.clear();
}

void EditorState::BeginTransformEdit() noexcept
{
    if (!m_selectedObject)
        return;
    m_isEditingTransform = true;
    m_editingObject = m_selectedObject;
    m_editingBefore = captureSnapshot(*m_selectedObject);
}

bool EditorState::CommitTransformEdit()
{
    if (!m_isEditingTransform)
        return false;
    m_isEditingTransform = false;
    auto object = std::move(m_editingObject);
    if (!object)
        return false;
    const TransformSnapshot after = captureSnapshot(*object);
    if (m_editingBefore == after)
        return false;
    pushHistory(std::move(object), m_editingBefore, after);
    return true;
}

void EditorState::ApplyTranslation(const SRMath::vec3 &delta)
{
    if (!m_selectedObject)
        return;
    const TransformSnapshot before = captureSnapshot(*m_selectedObject);
    m_selectedObject->SetPosition(m_selectedObject->GetPosition() + delta);
    m_selectedObject->Update(0.f, false);
    if (!m_isEditingTransform)
        pushHistory(m_selectedObject, before, captureSnapshot(*m_selectedObject));
}

void EditorState::ApplyRotationEuler(const SRMath::vec3 &deltaRadians)
{
    if (!m_selectedObject)
        return;
    const TransformSnapshot before = captureSnapshot(*m_selectedObject);
    const SRMath::vec3 nextEuler = m_selectedObject->GetRotation() + deltaRadians;
    if (m_selectedObject->HasOrientation())
    {
        const SRMath::quat deltaQuat = SRMath::from_rotation_vector(deltaRadians);
        const SRMath::quat nextQuat = SRMath::normalize(deltaQuat * m_selectedObject->GetOrientation());
        m_selectedObject->SetRotation(nextEuler);
        m_selectedObject->SetOrientation(nextQuat);
    }
    else
    {
        m_selectedObject->SetRotation(nextEuler);
    }
    m_selectedObject->Update(0.f, false);
    if (!m_isEditingTransform)
        pushHistory(m_selectedObject, before, captureSnapshot(*m_selectedObject));
}

void EditorState::ApplyScaleFactor(const SRMath::vec3 &factor)
{
    if (!m_selectedObject)
        return;
    const TransformSnapshot before = captureSnapshot(*m_selectedObject);
    SRMath::vec3 scale = m_selectedObject->GetScale();
    scale.x = std::max(0.001f, scale.x * factor.x);
    scale.y = std::max(0.001f, scale.y * factor.y);
    scale.z = std::max(0.001f, scale.z * factor.z);
    m_selectedObject->SetScale(scale);
    m_selectedObject->Update(0.f, false);
    if (!m_isEditingTransform)
        pushHistory(m_selectedObject, before, captureSnapshot(*m_selectedObject));
}

bool EditorState::Undo() noexcept
{
    if (m_undoStack.empty())
        return false;
    HistoryEntry entry = std::move(m_undoStack.back());
    m_undoStack.pop_back();
    if (entry.object)
        restoreSnapshot(*entry.object, entry.before);
    if (m_redoStack.size() >= maxHistoryEntries)
        m_redoStack.pop_front();
    m_redoStack.push_back(std::move(entry));
    return true;
}

bool EditorState::Redo() noexcept
{
    if (m_redoStack.empty())
        return false;
    HistoryEntry entry = std::move(m_redoStack.back());
    m_redoStack.pop_back();
    if (entry.object)
        restoreSnapshot(*entry.object, entry.after);
    if (m_undoStack.size() >= maxHistoryEntries)
        m_undoStack.pop_front();
    m_undoStack.push_back(std::move(entry));
    return true;
}

void EditorState::ClearHistory() noexcept
{
    m_undoStack.clear();
    m_redoStack.clear();
    m_isEditingTransform = false;
    m_editingObject.reset();
}

void EditorState::AppendEditorGizmos(RenderQueue &queue) const
{
    if (!m_selectedObject)
        return;

    const SRMath::vec3 origin = GetGizmoOrigin();
    AABB bounds = ComputeObjectWorldBounds(*m_selectedObject);
    if (!bounds.IsValid())
    {
        bounds.min = origin - SRMath::vec3{0.25f, 0.25f, 0.25f};
        bounds.max = origin + SRMath::vec3{0.25f, 0.25f, 0.25f};
    }

    // 1. 선택 물체 월드 AABB 상자(밝은 노란색, 항상 표시).
    {
        const auto corners = bounds.Corners();
        const SRMath::vec4 boxColor{1.0f, 0.9f, 0.2f, 1.0f};
        DebugPrimitiveCommand boxCommand;
        boxCommand.worldTransform = SRMath::mat4::identity();
        boxCommand.type = DebugPrimitiveType::Line;
        boxCommand.depthPolicy = DebugDepthPolicy::AlwaysVisible;
        boxCommand.vertices.reserve(24);
        boxCommand.vertices.insert(
            boxCommand.vertices.end(),
            {{corners[0], boxColor}, {corners[1], boxColor}, {corners[1], boxColor}, {corners[3], boxColor},
             {corners[3], boxColor}, {corners[2], boxColor}, {corners[2], boxColor}, {corners[0], boxColor},
             {corners[4], boxColor}, {corners[5], boxColor}, {corners[5], boxColor}, {corners[7], boxColor},
             {corners[7], boxColor}, {corners[6], boxColor}, {corners[6], boxColor}, {corners[4], boxColor},
             {corners[0], boxColor}, {corners[4], boxColor}, {corners[1], boxColor}, {corners[5], boxColor},
             {corners[2], boxColor}, {corners[6], boxColor}, {corners[3], boxColor}, {corners[7], boxColor}});
        queue.Submit(std::move(boxCommand));
    }

    // 2. 선택 물체 중심 3축 변환 기즈모(X=빨강, Y=초록, Z=파랑, 활성 축=밝은 노란색/흰색 강조).
    {
        const float axisLen = GetGizmoAxisLength();
        const float headLen = axisLen * 0.18f;
        const float headWidth = axisLen * 0.08f;
        const SRMath::vec4 highlightColor{1.0f, 1.0f, 0.4f, 1.0f};

        DebugPrimitiveCommand gizmoCommand;
        gizmoCommand.worldTransform = SRMath::mat4::identity();
        gizmoCommand.type = DebugPrimitiveType::Line;
        gizmoCommand.depthPolicy = DebugDepthPolicy::AlwaysVisible;
        gizmoCommand.vertices.reserve(18);

        const auto addAxisArrow = [&](GizmoAxis axis, const SRMath::vec3 &dir, const SRMath::vec3 &side,
                                      const SRMath::vec4 &baseColor)
        {
            const SRMath::vec4 color = (m_activeAxis == axis) ? highlightColor : baseColor;
            const SRMath::vec3 tip = origin + dir * axisLen;
            addLine(gizmoCommand, origin, tip, color);
            addLine(gizmoCommand, tip, tip - dir * headLen + side * headWidth, color);
            addLine(gizmoCommand, tip, tip - dir * headLen - side * headWidth, color);
        };

        addAxisArrow(GizmoAxis::X, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {1.0f, 0.15f, 0.15f, 1.0f});
        addAxisArrow(GizmoAxis::Y, {0.f, 1.f, 0.f}, {1.f, 0.f, 0.f}, {0.15f, 0.95f, 0.15f, 1.0f});
        addAxisArrow(GizmoAxis::Z, {0.f, 0.f, 1.f}, {0.f, 1.f, 0.f}, {0.2f, 0.45f, 1.0f, 1.0f});

        queue.Submit(std::move(gizmoCommand));
    }
}
} // namespace sr::editor

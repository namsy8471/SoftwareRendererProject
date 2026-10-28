#pragma once

#include <cstddef>
#include <deque>
#include <memory>
#include "Math/AABB.h"
#include "Math/SRMath.h"
#include "Scene/ScenePicking.h"

class GameObject;
class RenderQueue;

namespace sr::editor
{
enum class TransformMode
{
    Translate,
    Rotate,
    Scale
};

enum class GizmoAxis
{
    None,
    X,
    Y,
    Z
};

struct TransformSnapshot
{
    SRMath::vec3 position{0.f, 0.f, 0.f};
    SRMath::vec3 rotation{0.f, 0.f, 0.f};
    SRMath::quat orientation = SRMath::quat::identity();
    bool useOrientation = false;
    SRMath::vec3 scale{1.f, 1.f, 1.f};

    friend bool operator==(const TransformSnapshot &a, const TransformSnapshot &b) noexcept
    {
        return a.position.x == b.position.x && a.position.y == b.position.y && a.position.z == b.position.z &&
               a.rotation.x == b.rotation.x && a.rotation.y == b.rotation.y && a.rotation.z == b.rotation.z &&
               a.orientation.x == b.orientation.x && a.orientation.y == b.orientation.y &&
               a.orientation.z == b.orientation.z && a.orientation.w == b.orientation.w &&
               a.useOrientation == b.useOrientation && a.scale.x == b.scale.x && a.scale.y == b.scale.y &&
               a.scale.z == b.scale.z;
    }
};

// 물체 자신(또는 스킨 메시)과 하위 트리의 월드 AABB 합집합을 구한다.
[[nodiscard]] AABB ComputeObjectWorldBounds(const GameObject &object) noexcept;

class EditorState
{
  public:
    static constexpr std::size_t maxHistoryEntries = 64;
    static constexpr std::size_t kMaxHistory = maxHistoryEntries;

    void Select(std::shared_ptr<GameObject> object) noexcept;
    void ClearSelection() noexcept;
    [[nodiscard]] std::shared_ptr<GameObject> GetSelectedObject() const noexcept { return m_selectedObject; }
    [[nodiscard]] bool HasSelection() const noexcept { return m_selectedObject != nullptr; }

    [[nodiscard]] TransformMode GetTransformMode() const noexcept { return m_transformMode; }
    void SetTransformMode(TransformMode mode) noexcept { m_transformMode = mode; }
    [[nodiscard]] TransformMode GetMode() const noexcept { return m_transformMode; }
    void SetMode(TransformMode mode) noexcept { m_transformMode = mode; }

    [[nodiscard]] GizmoAxis GetActiveAxis() const noexcept { return m_activeAxis; }
    void SetActiveAxis(GizmoAxis axis) noexcept { m_activeAxis = axis; }

    [[nodiscard]] SRMath::vec3 GetGizmoOrigin() const noexcept;
    [[nodiscard]] float GetGizmoAxisLength() const noexcept;
    [[nodiscard]] GizmoAxis PickGizmoAxis(const Ray &ray, float thresholdRatio = 0.08f) const noexcept;

    void BeginTransformEdit() noexcept;
    bool CommitTransformEdit();
    [[nodiscard]] bool IsEditingTransform() const noexcept { return m_isEditingTransform; }

    void ApplyTranslation(const SRMath::vec3 &delta);
    void ApplyRotationEuler(const SRMath::vec3 &deltaRadians);
    void ApplyScaleFactor(const SRMath::vec3 &factor);

    [[nodiscard]] bool CanUndo() const noexcept { return !m_undoStack.empty(); }
    [[nodiscard]] bool CanRedo() const noexcept { return !m_redoStack.empty(); }
    [[nodiscard]] std::size_t GetUndoCount() const noexcept { return m_undoStack.size(); }
    [[nodiscard]] std::size_t GetRedoCount() const noexcept { return m_redoStack.size(); }
    bool Undo() noexcept;
    bool Redo() noexcept;
    void ClearHistory() noexcept;

    // 선택된 물체가 있을 때만 노란색 선택 AABB와 3축 기즈모 선을 제출한다.
    void AppendEditorGizmos(RenderQueue &queue) const;

  private:
    struct HistoryEntry
    {
        std::shared_ptr<GameObject> object;
        TransformSnapshot before;
        TransformSnapshot after;
    };

    [[nodiscard]] static TransformSnapshot captureSnapshot(const GameObject &object) noexcept;
    static void restoreSnapshot(GameObject &object, const TransformSnapshot &snapshot) noexcept;
    void pushHistory(std::shared_ptr<GameObject> object, const TransformSnapshot &before,
                     const TransformSnapshot &after);

    std::shared_ptr<GameObject> m_selectedObject;
    TransformMode m_transformMode = TransformMode::Translate;
    GizmoAxis m_activeAxis = GizmoAxis::None;

    bool m_isEditingTransform = false;
    std::shared_ptr<GameObject> m_editingObject;
    TransformSnapshot m_editingBefore{};

    std::deque<HistoryEntry> m_undoStack;
    std::deque<HistoryEntry> m_redoStack;
};
} // namespace sr::editor

using sr::editor::EditorState;
using sr::editor::GizmoAxis;
using sr::editor::TransformMode;
using sr::editor::TransformSnapshot;

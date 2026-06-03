module;

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <glm/glm.hpp>

export module Extrinsic.Runtime.GizmoInteraction;

import Extrinsic.Core.Geometry2D;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Graphics.RenderWorld;

export namespace Extrinsic::Runtime
{
    // Which transform operation the gizmo currently authors. RUNTIME-084 Slice A
    // implements translate hit-test + drag math; rotate/scale modes are
    // hit-testable and render-packetable but their drag application is owned by a
    // follow-up slice (the drag-tick deltas only mutate translation today).
    enum class GizmoMode : std::uint8_t
    {
        Translate = 0,
        Rotate    = 1,
        Scale     = 2,
    };

    // The handle a pick resolved to. `None` is the no-hit / background result.
    enum class GizmoAxis : std::uint8_t
    {
        None = 0,
        X    = 1,
        Y    = 2,
        Z    = 3,
    };

    // Whether the gizmo axes follow world space (`Global`) or the primary
    // selected entity's local rotation frame (`Local`). Multi-select always uses
    // the `Global` frame about the selection pivot.
    enum class GizmoOrientation : std::uint8_t
    {
        Global = 0,
        Local  = 1,
    };

    // Modifier bit flags the interaction observes. `Snap` rounds the applied
    // translation to `GizmoConfig::TranslateSnapStep`; the remaining bits are
    // captured for the editor's benefit and never leak into the render packet.
    enum class GizmoModifier : std::uint32_t
    {
        None  = 0u,
        Snap  = 1u << 0,
        Clone = 1u << 1,
    };

    [[nodiscard]] constexpr GizmoModifier operator|(GizmoModifier a, GizmoModifier b) noexcept
    {
        return static_cast<GizmoModifier>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
    }
    [[nodiscard]] constexpr bool HasModifier(std::uint32_t mask, GizmoModifier bit) noexcept
    {
        return (mask & static_cast<std::uint32_t>(bit)) != 0u;
    }

    // World-space pick ray, mirroring `CameraViewSnapshot::PickRay*` but passed
    // explicitly so drag math is testable without a full snapshot. `Direction`
    // is expected to be normalized; the interaction renormalizes defensively.
    struct PickRay
    {
        glm::vec3 Origin{0.f};
        glm::vec3 Direction{0.f, 0.f, -1.f};
    };

    // Static tuning knobs. Defaults match the sandbox editor's gizmo.
    struct GizmoConfig
    {
        // Screen-space pick radius (pixels) around an axis handle line. A cursor
        // farther than this from every visible axis is a background no-hit.
        float HandlePickRadiusPixels = 8.f;
        // World length of each axis handle, used for hit projection and the
        // render packet's `AxisLength`.
        float AxisLength = 1.f;
        // Translation snap increment applied when the `Snap` modifier is held.
        float TranslateSnapStep = 0.25f;
    };

    // The resolved gizmo handle for a pointer pick.
    struct GizmoHitResult
    {
        bool                       Hit{false};
        GizmoAxis                  Axis{GizmoAxis::None};
        Extrinsic::ECS::EntityHandle Entity{Extrinsic::ECS::InvalidEntityHandle};
        // Screen-space distance (pixels) from the cursor to the winning handle
        // line; meaningless when `Hit` is false.
        float                      PixelDistance{0.f};
    };

    // One undoable authoring-transform edit emitted on drag-commit. The editor
    // owns redo/undo policy; runtime only records the before/after pair so the
    // editor can replay it.
    struct GizmoTransformEdit
    {
        Extrinsic::ECS::EntityHandle Entity{Extrinsic::ECS::InvalidEntityHandle};
        glm::vec3                    BeforePosition{0.f};
        glm::vec3                    AfterPosition{0.f};
    };

    // Minimal runtime-owned undo stack for gizmo edits. The editor may instead
    // supply its own sink; this keeps the module standalone-testable.
    class GizmoUndoStack
    {
    public:
        void Push(const GizmoTransformEdit& edit);
        [[nodiscard]] std::size_t Size() const noexcept { return m_Records.size(); }
        [[nodiscard]] bool        Empty() const noexcept { return m_Records.empty(); }
        [[nodiscard]] const GizmoTransformEdit& Back() const { return m_Records.back(); }
        [[nodiscard]] std::span<const GizmoTransformEdit> Records() const noexcept { return m_Records; }
        void Clear() noexcept { m_Records.clear(); }

    private:
        std::vector<GizmoTransformEdit> m_Records{};
    };

    // Diagnostics counters surfaced for editor overlays / tests.
    struct GizmoInteractionDiagnostics
    {
        std::uint32_t HitTests           = 0u;
        std::uint32_t HitsResolved       = 0u;
        std::uint32_t DragsStarted       = 0u;
        std::uint32_t DragTicks          = 0u;
        std::uint32_t DragsCommitted     = 0u;
        std::uint32_t DragsCancelled     = 0u;
        std::uint32_t SnappedTicks       = 0u;
        std::uint32_t EditsEmitted       = 0u;
    };

    // Runtime / editor-owned transform-gizmo interaction (RUNTIME-084, Slice A).
    //
    // Owns per-frame interaction state (mode, axis lock, drag origin, snap step,
    // modifier mask, multi-select pivot, orientation frame), screen-space handle
    // hit testing, axis-constrained drag application against ECS authoring
    // transforms, and undo emission on drag-commit. Hit testing reads
    // `CameraViewSnapshot::ViewProjection` + a cursor pixel + viewport; drag math
    // reads the world `PickRay`. Graphics never sees this state: only the frozen
    // `Graphics::TransformGizmoRenderPacket` field set is produced by
    // `TransformGizmoRenderPacketBuilder`.
    //
    // Layering: imports promoted ECS registry/handle/transform plus the existing
    // graphics camera-snapshot and render-world (gizmo packet) edges. It never
    // imports platform input or the renderer; `Engine` wiring is the deferred
    // Slice B.
    class GizmoInteraction
    {
    public:
        using Registry     = Extrinsic::ECS::Scene::Registry;
        using EntityHandle = Extrinsic::ECS::EntityHandle;

        GizmoInteraction() = default;
        explicit GizmoInteraction(const GizmoConfig& config) noexcept;

        // --- interaction-state accessors ---
        void SetMode(GizmoMode mode) noexcept { m_Mode = mode; }
        [[nodiscard]] GizmoMode Mode() const noexcept { return m_Mode; }
        void SetOrientation(GizmoOrientation frame) noexcept { m_Orientation = frame; }
        [[nodiscard]] GizmoOrientation Orientation() const noexcept { return m_Orientation; }
        void SetAxisLock(GizmoAxis axis) noexcept { m_AxisLock = axis; }
        [[nodiscard]] GizmoAxis AxisLock() const noexcept { return m_AxisLock; }
        void SetModifierMask(std::uint32_t mask) noexcept { m_ModifierMask = mask; }
        [[nodiscard]] std::uint32_t ModifierMask() const noexcept { return m_ModifierMask; }

        [[nodiscard]] bool      IsDragging() const noexcept { return m_Dragging; }
        [[nodiscard]] GizmoAxis DragAxis() const noexcept { return m_DragAxis; }
        [[nodiscard]] glm::vec3 DragOrigin() const noexcept { return m_DragOrigin; }
        [[nodiscard]] glm::vec3 MultiSelectPivot() const noexcept { return m_Pivot; }

        // --- gizmo pivot / frame helpers ---
        // World-space pivot for the current selection (average of selected entity
        // positions). Returns false when the selection is empty or has no live
        // transforms.
        [[nodiscard]] bool ComputePivot(const Registry& registry,
                                        std::span<const EntityHandle> selected,
                                        glm::vec3& outPivot) const;

        // --- hit testing (screen-space handle pick) ---
        [[nodiscard]] GizmoHitResult HitTest(const Registry& registry,
                                             const Extrinsic::Graphics::CameraViewSnapshot& camera,
                                             glm::vec2 cursorPixel,
                                             Core::Extent2D viewport,
                                             std::span<const EntityHandle> selected);

        // --- drag lifecycle ---
        // Begin an axis drag from a resolved hit. Records the per-entity before
        // transforms and the drag anchor parameter along the world axis. Returns
        // false when the hit is a no-hit, the selection is empty, or the ray is
        // degenerate.
        bool BeginDrag(const Registry& registry,
                       const GizmoHitResult& hit,
                       const PickRay& ray,
                       std::span<const EntityHandle> selected);

        // Apply an in-progress drag for the current ray. Mutates ECS authoring
        // transforms (translation along the locked axis) and stamps the transform
        // dirty marker. No-op (returns false) when not dragging or the ray is
        // degenerate. The applied scalar is rounded to the snap step when the
        // `Snap` modifier is set.
        bool DragTick(Registry& registry, const PickRay& ray);

        // Commit the drag: emit one `GizmoTransformEdit` per moved entity to
        // `undo`, then clear drag state. Returns the number of edits emitted.
        std::size_t DragCommit(const Registry& registry, GizmoUndoStack& undo);

        // Abort the drag, restoring each entity to its recorded before transform.
        void DragCancel(Registry& registry);

        [[nodiscard]] const GizmoInteractionDiagnostics& Diagnostics() const noexcept { return m_Diagnostics; }
        [[nodiscard]] GizmoConfig&       Config() noexcept { return m_Config; }
        [[nodiscard]] const GizmoConfig& Config() const noexcept { return m_Config; }

    private:
        struct DragEntry
        {
            EntityHandle Entity{Extrinsic::ECS::InvalidEntityHandle};
            glm::vec3    BeforePosition{0.f};
        };

        // World-space unit direction for `axis` under the current orientation
        // frame and (for Local) the primary entity's rotation.
        [[nodiscard]] glm::vec3 AxisDirection(const Registry& registry,
                                              EntityHandle primary,
                                              GizmoAxis axis) const;

        GizmoConfig                 m_Config{};
        GizmoInteractionDiagnostics m_Diagnostics{};

        GizmoMode        m_Mode{GizmoMode::Translate};
        GizmoOrientation m_Orientation{GizmoOrientation::Global};
        GizmoAxis        m_AxisLock{GizmoAxis::None};
        std::uint32_t    m_ModifierMask{0u};

        bool        m_Dragging{false};
        GizmoAxis   m_DragAxis{GizmoAxis::None};
        glm::vec3   m_DragOrigin{0.f};   // gizmo pivot at drag start
        glm::vec3   m_DragAxisDir{1.f, 0.f, 0.f};
        float       m_DragStartParam{0.f};
        glm::vec3   m_Pivot{0.f};
        std::vector<DragEntry> m_DragEntries{};
    };

    // Produces `Graphics::TransformGizmoRenderPacket` records for the active
    // selection + interaction state. The packet field set is frozen by
    // GRAPHICS-017Q; this builder maps only those fields (stable id, gizmo
    // transform, axis length, mode visibility flags) and never the drag state,
    // axis lock, snap thresholds, or modifier keys.
    class TransformGizmoRenderPacketBuilder
    {
    public:
        using Registry     = Extrinsic::ECS::Scene::Registry;
        using EntityHandle = Extrinsic::ECS::EntityHandle;

        // Rebuilds the packet vector for `selected`. One packet per live,
        // transform-bearing entity. Returns the produced span (valid until the
        // next `Build` / builder destruction).
        std::span<const Extrinsic::Graphics::TransformGizmoRenderPacket> Build(
            const Registry& registry,
            std::span<const EntityHandle> selected,
            GizmoMode mode,
            GizmoOrientation orientation,
            float axisLength);

        [[nodiscard]] std::span<const Extrinsic::Graphics::TransformGizmoRenderPacket> Packets() const noexcept
        {
            return m_Packets;
        }

    private:
        std::vector<Extrinsic::Graphics::TransformGizmoRenderPacket> m_Packets{};
    };
}

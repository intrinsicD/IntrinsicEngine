// Transform-gizmo drag sessions (pivot, basis, atomic TRS preview, one undo batch), ray adapter and packet builder.
module;

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

export module Extrinsic.Runtime.GizmoInteraction;

import Extrinsic.Core.Geometry2D;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.CameraSnapshots;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.WorldHandle;

export namespace Extrinsic::Runtime
{
    // Which transform operation the gizmo currently authors. The operation mode
    // is latched on BeginDrag so toolbar changes during a drag do not reinterpret
    // the same pointer delta as a different transform edit.
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

    // Whether the gizmo axes follow world space (`Global`) or the selection's
    // rotation (`Local`): a single entity's world rotation, or for groups the
    // chordal mean of the selected world rotations (falls back to `Global`
    // with a `GizmoBasisFallback` reason when that mean is unavailable).
    enum class GizmoOrientation : std::uint8_t
    {
        Global = 0,
        Local  = 1,
    };

    // Where the group pivot sits: the mean of selected world origins, or the
    // mean of per-entity world-bounds centers (`LocalBoundingAABB` through the
    // entity's world matrix; entities without valid bounds use their origin).
    enum class GizmoPivotMode : std::uint8_t
    {
        WorldOrigins  = 0,
        BoundsCenters = 1,
    };

    // Outcome of a session request. From `Begin` and `Preview`, anything but
    // `Ok` leaves ECS untouched. `DragCancel` is the exception: on a session
    // conflict it still restores the previews it owns and returns
    // `StaleSession` (see `DragCancel`).
    enum class GizmoStatus : std::uint8_t
    {
        Ok = 0,
        SessionActive,   // Begin while a session is already running.
        NoSession,       // Preview without a running session.
        EmptySelection,
        InvalidEntity,   // Selected handle is dead or has no Transform.
        BrokenHierarchy, // Dead/transform-less parent or a parent cycle.
        SingularParent,  // Parent world matrix cannot be inverted.
        NonFiniteMatrix,
        NonTrsResult,    // Local result needs shear/perspective; not storable as TRS.
        StaleSession,    // Registry, entity, parent chain or transform changed under the session.
    };

    struct GizmoResult
    {
        GizmoStatus Status{GizmoStatus::Ok};
        // Entity the failure refers to, when there is one.
        Extrinsic::ECS::EntityHandle Entity{Extrinsic::ECS::InvalidEntityHandle};

        [[nodiscard]] bool Succeeded() const noexcept { return Status == GizmoStatus::Ok; }
    };

    // Why a requested `Local` basis was replaced by the world basis.
    enum class GizmoBasisFallback : std::uint8_t
    {
        None = 0,
        RotationUnavailable, // A selected world matrix did not decompose.
        MeanDegenerate,      // ChordalMean reported DegenerateInput (e.g. antipodal rotations).
        MeanFailed,          // Any other failed or non-finite ChordalMean result.
    };

    // Copied gizmo frame for a selection. `Matrix` is `T(Pivot) * R(Basis)`
    // with unit scale (G0). `Primary` is the deterministic representative
    // (lowest entity handle) whose render id the packet carries.
    struct GizmoFrame
    {
        GizmoResult                  Result{GizmoStatus::EmptySelection};
        Extrinsic::ECS::EntityHandle Primary{Extrinsic::ECS::InvalidEntityHandle};
        glm::vec3                    Pivot{0.f};
        glm::mat3                    Basis{1.f};
        glm::mat4                    Matrix{1.f};
        GizmoPivotMode               PivotMode{GizmoPivotMode::WorldOrigins};
        GizmoOrientation             RequestedOrientation{GizmoOrientation::Global};
        GizmoOrientation             ActualOrientation{GizmoOrientation::Global};
        GizmoBasisFallback           BasisFallback{GizmoBasisFallback::None};

        [[nodiscard]] bool Available() const noexcept { return Result.Succeeded(); }
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
        // Rotation angle in radians per world-space axis parameter unit.
        float RotateRadiansPerWorldUnit = 1.0f;
        // Rotation snap increment in radians applied when `Snap` is held.
        float RotateSnapStepRadians = 0.2617993878f; // 15 degrees.
        // Axis-scale multiplier per world-space axis parameter unit.
        float ScaleFactorPerWorldUnit = 1.0f;
        // Scale snap increment applied when `Snap` is held.
        float ScaleSnapStep = 0.1f;
        // Lower bound for authored scale components.
        float MinScale = 0.001f;
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

    // Runtime / editor-owned transform-gizmo interaction (RUNTIME-084, UI-078).
    //
    // Core: a matrix drag session. `Begin` freezes the deduplicated, sorted
    // selection, mode, orientation, pivot mode and start state (current
    // authoring world matrices composed from local TRS along the parent chain,
    // never the cached WorldMatrix) and writes nothing. Each `Preview(Gt)`
    // recomputes from that start state: `D = Gt * G0^-1`, `Wi' = D * Wi0`,
    // `Li' = Wparent^-1 * Wi'`, writing only selected entities without a
    // selected ancestor. A tick whose local result is not storable as TRS
    // (shear, perspective, non-finite) is rejected for the whole group:
    // nothing is written and the last accepted preview stays. Preview,
    // commit and cancel first run one write-free session check: same
    // registry, targets still hold the last accepted TRS under unchanged
    // parent world matrices, and every selected entity and ancestor is alive
    // with an unchanged parent link (non-targets also with unchanged local
    // TRS). `DragCommit` records one batch; a no-op drag and
    // `DragCancel` leave no history and restore exact TRS.
    //
    // Adapter: `HitTest`/`BeginDrag`/`DragTick` keep the ray-driven sandbox
    // mouse path; `DragTick` turns axis, snap and ray into `Gt` and writes
    // only through `Preview`. Graphics never sees this state: only the frozen
    // `Graphics::TransformGizmoRenderPacket` field set is produced by
    // `TransformGizmoRenderPacketBuilder`.
    class GizmoInteraction
    {
    public:
        using Registry     = Extrinsic::ECS::Scene::Registry;
        using EntityHandle = Extrinsic::ECS::EntityHandle;

        GizmoInteraction() = default;
        explicit GizmoInteraction(const GizmoConfig& config) noexcept;

        // --- interaction-state accessors (read by the adapter at BeginDrag) ---
        void SetMode(GizmoMode mode) noexcept { m_Mode = mode; }
        [[nodiscard]] GizmoMode Mode() const noexcept { return m_Mode; }
        void SetOrientation(GizmoOrientation frame) noexcept { m_Orientation = frame; }
        [[nodiscard]] GizmoOrientation Orientation() const noexcept { return m_Orientation; }
        void SetPivotMode(GizmoPivotMode pivot) noexcept { m_PivotMode = pivot; }
        [[nodiscard]] GizmoPivotMode PivotMode() const noexcept { return m_PivotMode; }
        void SetAxisLock(GizmoAxis axis) noexcept { m_AxisLock = axis; }
        [[nodiscard]] GizmoAxis AxisLock() const noexcept { return m_AxisLock; }
        void SetModifierMask(std::uint32_t mask) noexcept { m_ModifierMask = mask; }
        [[nodiscard]] std::uint32_t ModifierMask() const noexcept { return m_ModifierMask; }

        [[nodiscard]] bool      IsDragging() const noexcept { return m_Dragging; }
        [[nodiscard]] GizmoAxis DragAxis() const noexcept { return m_DragAxis; }

        // Side-effect-free frame for `selected` from current authoring state.
        [[nodiscard]] GizmoFrame ComputeFrame(const Registry& registry,
                                              std::span<const EntityHandle> selected,
                                              GizmoOrientation orientation,
                                              GizmoPivotMode pivotMode) const;

        // --- matrix session ---
        [[nodiscard]] GizmoResult Begin(const Registry& registry,
                                        std::span<const EntityHandle> selected,
                                        GizmoMode mode,
                                        GizmoOrientation orientation,
                                        GizmoPivotMode pivotMode);
        // `gizmoMatrix` is the requested gizmo matrix Gt. Atomic: all targets
        // are written or none.
        [[nodiscard]] GizmoResult Preview(Registry& registry, const glm::mat4& gizmoMatrix);

        // Frozen session frame (Matrix = G0); meaningful while dragging.
        [[nodiscard]] const GizmoFrame& SessionFrame() const noexcept { return m_SessionFrame; }
        [[nodiscard]] GizmoMode SessionMode() const noexcept { return m_DragMode; }
        // Increments on every session start (successful `Begin`) and every
        // session end (commit, cancel, rollback), so a value read at any time
        // names exactly the current idle or running interval.
        [[nodiscard]] std::uint64_t SessionGeneration() const noexcept { return m_SessionGeneration; }
        // Last accepted Gt (G0 until a preview is accepted).
        [[nodiscard]] const glm::mat4& AcceptedGizmoMatrix() const noexcept { return m_AcceptedGizmo; }

        // --- hit testing (screen-space handle pick against the shared frame) ---
        [[nodiscard]] GizmoHitResult HitTest(const Registry& registry,
                                             const Extrinsic::Graphics::CameraViewSnapshot& camera,
                                             glm::vec2 cursorPixel,
                                             Core::Extent2D viewport,
                                             std::span<const EntityHandle> selected);

        // --- ray adapter ---
        // Begins a session with the current mode/orientation/pivot mode and
        // anchors the ray on the hit axis. False for a no-hit, degenerate ray,
        // or a refused `Begin`.
        bool BeginDrag(const Registry& registry,
                       const GizmoHitResult& hit,
                       const PickRay& ray,
                       std::span<const EntityHandle> selected);

        // Builds Gt from the latched axis, ray delta and snap, then `Preview`s
        // it. False when not dragging, the ray is degenerate, or the preview
        // was rejected (the last accepted preview stays).
        bool DragTick(Registry& registry, const PickRay& ray);

        // Records the accepted preview as one generation-validated history
        // transaction without publishing it again, then ends the session.
        // Targets whose accepted local matrix equals the original are
        // restored exactly and not recorded; an all-no-op drag records
        // nothing. A foreign registry writes nothing and keeps the session
        // (`StaleEntity`). A session conflict or a failed recording records
        // nothing and rolls back every target that still holds its accepted
        // preview; targets changed by someone else keep that change.
        [[nodiscard]] EditorCommandHistoryResult DragCommit(
            Registry& registry,
            WorldHandle world,
            EditorCommandHistory& history);

        // Copies the original TRS back to every target that still holds its
        // accepted preview and ends the session. Returns the session check:
        // `Ok`, or the conflict that left a foreign change in place. A foreign
        // registry writes nothing and keeps the session (`StaleSession`);
        // without a session `NoSession`.
        GizmoResult DragCancel(Registry& registry);

        [[nodiscard]] const GizmoInteractionDiagnostics& Diagnostics() const noexcept { return m_Diagnostics; }
        [[nodiscard]] GizmoConfig&       Config() noexcept { return m_Config; }
        [[nodiscard]] const GizmoConfig& Config() const noexcept { return m_Config; }

    private:
        struct SessionTarget
        {
            EntityHandle Entity{Extrinsic::ECS::InvalidEntityHandle};
            EntityHandle Parent{Extrinsic::ECS::InvalidEntityHandle};
            glm::vec3    OriginalPosition{0.f};
            glm::quat    OriginalRotation{1.f, 0.f, 0.f, 0.f};
            glm::vec3    OriginalScale{1.f};
            glm::vec3    AcceptedPosition{0.f};
            glm::quat    AcceptedRotation{1.f, 0.f, 0.f, 0.f};
            glm::vec3    AcceptedScale{1.f};
            glm::mat4    World0{1.f};
            glm::mat4    ParentWorld{1.f};
        };

        // Frozen selection entry or ancestor. Non-target links also freeze
        // their local TRS; write targets are checked against their accepted
        // preview instead.
        struct SessionLink
        {
            EntityHandle Entity{Extrinsic::ECS::InvalidEntityHandle};
            EntityHandle Parent{Extrinsic::ECS::InvalidEntityHandle};
            bool         Target{false};
            glm::vec3    Position{0.f};
            glm::quat    Rotation{1.f, 0.f, 0.f, 0.f};
            glm::vec3    Scale{1.f};
        };

        void EndSession() noexcept;
        [[nodiscard]] GizmoResult ValidateSession(const Registry& registry) const;
        void RestoreOwnedTargets(Registry& registry);

        GizmoConfig                 m_Config{};
        GizmoInteractionDiagnostics m_Diagnostics{};

        GizmoMode        m_Mode{GizmoMode::Translate};
        GizmoOrientation m_Orientation{GizmoOrientation::Global};
        GizmoPivotMode   m_PivotMode{GizmoPivotMode::WorldOrigins};
        GizmoAxis        m_AxisLock{GizmoAxis::None};
        std::uint32_t    m_ModifierMask{0u};

        bool            m_Dragging{false};
        GizmoMode       m_DragMode{GizmoMode::Translate};
        std::uint64_t   m_SessionGeneration{0u};
        const Registry* m_SessionRegistry{nullptr};
        GizmoFrame      m_SessionFrame{};
        glm::mat4       m_AcceptedGizmo{1.f};
        std::vector<SessionTarget> m_Targets{};
        std::vector<SessionLink>   m_Links{};

        // Ray-adapter anchor.
        GizmoAxis m_DragAxis{GizmoAxis::None};
        glm::vec3 m_DragAxisDir{1.f, 0.f, 0.f};
        float     m_DragStartParam{0.f};
    };

    // Produces `Graphics::TransformGizmoRenderPacket` records. The packet field
    // set is frozen by GRAPHICS-017Q; this builder maps only those fields
    // (stable id, gizmo transform, axis length, mode visibility flags).
    class TransformGizmoRenderPacketBuilder
    {
    public:
        using Registry     = Extrinsic::ECS::Scene::Registry;
        using EntityHandle = Extrinsic::ECS::EntityHandle;

        // Rebuilds the packets for `selected`: one group gizmo on the same
        // frame `gizmo.HitTest` uses, or, while a session runs, its frozen
        // mode and accepted gizmo matrix. Empty when no frame is available.
        // The span stays valid until the next `Build` / builder destruction.
        std::span<const Extrinsic::Graphics::TransformGizmoRenderPacket> Build(
            const Registry& registry,
            std::span<const EntityHandle> selected,
            const GizmoInteraction& gizmo);

        [[nodiscard]] std::span<const Extrinsic::Graphics::TransformGizmoRenderPacket> Packets() const noexcept
        {
            return m_Packets;
        }

    private:
        std::vector<Extrinsic::Graphics::TransformGizmoRenderPacket> m_Packets{};
    };
}

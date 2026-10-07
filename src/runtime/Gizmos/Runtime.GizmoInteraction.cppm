// Transform-gizmo drag sessions: pivot, basis, atomic TRS preview and one undo batch per drag.
module;

#include <cstdint>
#include <span>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

export module Extrinsic.Runtime.GizmoInteraction;

import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.WorldHandle;

export namespace Extrinsic::Runtime
{
    // Which transform operation a drag authors. `Begin` latches it, so a mode
    // change during a drag does not reinterpret the same gizmo motion.
    enum class GizmoMode : std::uint8_t
    {
        Translate = 0,
        Rotate    = 1,
        Scale     = 2,
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
    // (lowest entity handle) a frame-wide failure is attributed to.
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

    // Diagnostics counters surfaced for editor overlays / tests.
    struct GizmoInteractionDiagnostics
    {
        std::uint32_t DragsStarted       = 0u;
        std::uint32_t DragsCommitted     = 0u;
        std::uint32_t DragsCancelled     = 0u;
        std::uint32_t EditsEmitted       = 0u;
    };

    // `gizmoMatrix`'s rotation relative to `frame` (G0) snapped exactly to the
    // nearest multiple of `stepRadians`, about the frozen pivot. A matrix
    // cannot tell "θ about a" from "θ − 2π about a", so both are rounded and
    // the one closer to a multiple wins (a step need not divide 360°). A
    // rotation that snaps to none (or a whole turn) returns G0 exactly.
    // Precondition: `stepRadians` >= 0.001 degrees, the `sandbox.gizmo` floor.
    [[nodiscard]] glm::mat4 SnapGizmoRotation(const GizmoFrame& frame, const glm::mat4& gizmoMatrix,
                                              float stepRadians);

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
    // `DragCancel` leave no history and restore exact TRS. The Sandbox
    // editor frontend supplies `Gt`, snap included.
    class GizmoInteraction
    {
    public:
        using Registry     = Extrinsic::ECS::Scene::Registry;
        using EntityHandle = Extrinsic::ECS::EntityHandle;

        [[nodiscard]] bool IsDragging() const noexcept { return m_Dragging; }

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

        GizmoInteractionDiagnostics m_Diagnostics{};

        bool            m_Dragging{false};
        GizmoMode       m_DragMode{GizmoMode::Translate};
        std::uint64_t   m_SessionGeneration{0u};
        const Registry* m_SessionRegistry{nullptr};
        GizmoFrame      m_SessionFrame{};
        glm::mat4       m_AcceptedGizmo{1.f};
        std::vector<SessionTarget> m_Targets{};
        std::vector<SessionLink>   m_Links{};
    };
}

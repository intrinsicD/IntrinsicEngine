// Owns active-world viewport interaction and publishes selection/gizmo snapshots.
module;

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

export module Extrinsic.Runtime.SceneInteractionModule;

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Error;
import Extrinsic.ECS.Component.StableId;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.GizmoInteraction;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.RenderExtraction;
import Extrinsic.Runtime.ModuleLifecycle;
import Extrinsic.Runtime.PrimitiveSelectionRefinement;
import Extrinsic.Runtime.StableEntityLookup;

namespace Extrinsic::Runtime
{
    // Highlights of the selected primitives from the CPU `v:position`. Entities for which
    // `suppress` returns true (GRAPHICS-156: their blocks show uncommitted GPU positions the
    // CPU does not describe) contribute no highlight until the preview ends.
    export RuntimeSceneInteractionRenderSnapshot BuildPrimitiveSelectionRenderSnapshot(
        const ECS::Scene::Registry& scene, const SelectionController& selection, WorldHandle world,
        const std::function<bool(std::uint32_t stableEntityId)>& suppress = {});

    // What a primitive pick's pixels were rendered from: topology, the
    // displayed positions (RUNTIME-315: which source is bound, by binding
    // generation, and that source's revision), the transform and (GRAPHICS-156)
    // whether the entity showed uncommitted GPU positions. A stamp that changed
    // between the request and its readback discards the pick. Empty for a stale id.
    export [[nodiscard]] std::vector<std::uint64_t> BuildPrimitivePickStamp(
        const ECS::Scene::Registry& scene, std::uint32_t stableEntityId, bool uncommittedPositions);

    // Gizmo snap steps (`sandbox.gizmo`, schema `intrinsic.runtime.sandbox.gizmo` v1).
    // Every step is a positive, finite value representable as a normal float.
    // Rotation stays in degrees because the editor gizmo consumes degrees.
    export inline constexpr std::string_view kGizmoSnapConfigSectionName = "sandbox.gizmo";
    export inline constexpr std::string_view kGizmoSnapConfigSectionSchemaId =
        "intrinsic.runtime.sandbox.gizmo";
    export struct GizmoSnapConfig
    {
        float TranslateStep{0.25f};      // world units
        float RotateStepDegrees{15.0f};
        float ScaleStep{0.1f};           // scale factor increment
    };
    export [[nodiscard]] Core::Config::EngineConfigSectionValidationResult ValidateGizmoSnapConfigSection(
        std::string_view payload, std::string_view reference, std::string_view subject);
    // The applied value, or nullopt when the section is absent, of another schema, or invalid.
    export [[nodiscard]] std::optional<GizmoSnapConfig> GetGizmoSnapConfig(
        const Core::Config::EngineConfig& config);
    export void SetGizmoSnapConfig(Core::Config::EngineConfig& config, const GizmoSnapConfig& value);
    export [[nodiscard]] Core::Config::EngineConfigSectionRegistration MakeGizmoSnapConfigSectionRegistration();

    // Why the editor gizmo frontend has nothing to manipulate (UI-078).
    export enum class GizmoUiUnavailable : std::uint8_t
    {
        None = 0,
        NoBinding,         // No live active world/registry binding (or the module shut down).
        NoHistory,         // No EditorCommandHistory to record the drag.
        NoEntitySelection, // Selection target is not Entity, or no live entity is selected.
        InvalidFrame,      // ComputeFrame failed; `GizmoUiFrame::Frame.Result` names status and entity.
        NoCamera,          // No valid Main camera view for the bound world and a non-empty scene rectangle.
    };

    // Names the bound world, interaction epoch and gizmo session generation
    // (GizmoInteraction::SessionGeneration) a frontend call refers to. A
    // world switch, document replacement, or any session start or end
    // (commit, cancel, UI hide, focus loss) makes an older token stale; stale
    // calls write nothing and start nothing.
    export struct GizmoUiToken
    {
        WorldHandle World{};
        std::uint64_t InteractionEpoch{0u};
        std::uint64_t Session{0u};

        [[nodiscard]] friend bool operator==(const GizmoUiToken&, const GizmoUiToken&) noexcept = default;
    };

    // Copied per-UI-frame model for an editor gizmo frontend. All values are
    // runtime-typed; the frontend never sees the registry or selection.
    export struct GizmoUiFrame
    {
        GizmoUiUnavailable Unavailable{GizmoUiUnavailable::NoBinding};
        // Compare with the token `BeginGizmoDrag` returned: a mismatch means
        // the frontend's session ended elsewhere and must not be committed.
        // While idle, it is the only token `BeginGizmoDrag` accepts.
        GizmoUiToken Token{};
        bool Dragging{false};
        // While dragging: the frozen session mode and frame (G0); otherwise
        // ComputeFrame for the requested orientation and pivot mode.
        GizmoMode SessionMode{GizmoMode::Translate};
        GizmoFrame Frame{};
        // Last accepted Gt while dragging, else `Frame.Matrix`.
        glm::mat4 GizmoMatrix{1.0f};
        // Unjittered column-major Main camera view/projection for the scene
        // rectangle's aspect, read from the controller without Update: its
        // state BEFORE this frame's CameraModule update, which runs after the
        // UI. It equals this frame's render camera only when a viewport claim
        // suppresses that update (e.g. while the gizmo is hovered or dragged).
        glm::mat4 View{1.0f};
        glm::mat4 Projection{1.0f};
        bool Orthographic{false};
        // The engine's scene rectangle (ResolveSceneViewportPixels on this
        // frame's current editor claim, else the whole framebuffer) mapped
        // back to window coordinates relative to the client origin, i.e.
        // ImGui logical coordinates before the frontend adds its display origin.
        EditorSceneViewportRect SceneRect{};

        [[nodiscard]] bool Available() const noexcept { return Unavailable == GizmoUiUnavailable::None; }
    };

    export struct GizmoUiBeginResult
    {
        // Not None: refused before reaching the session core.
        GizmoUiUnavailable Unavailable{GizmoUiUnavailable::None};
        // The session core's status (StaleSession for a stale token).
        GizmoResult Result{};
        // Names the new session on success.
        GizmoUiToken Token{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Unavailable == GizmoUiUnavailable::None && Result.Succeeded();
        }
    };

    // Optional app-composed owner for every active-world interaction record.
    // The object has app-global lifetime; its mutable cohort binds to exactly
    // one WorldHandle/Registry pair and never retains per-world history.
    export class SceneInteractionModule final : public IRuntimeModule
    {
    public:
        SceneInteractionModule();
        ~SceneInteractionModule() override;

        SceneInteractionModule(const SceneInteractionModule&) = delete;
        SceneInteractionModule& operator=(
            const SceneInteractionModule&) = delete;

        [[nodiscard]] std::string_view Name() const noexcept override;
        [[nodiscard]] Core::Result OnRegister(EngineSetup& setup) override;
        [[nodiscard]] Core::Result OnResolve(EngineSetup& setup) override;
        void OnShutdown(RuntimeModuleShutdownContext& context) override;

        [[nodiscard]] std::optional<ECS::EntityHandle>
            ResolveEntityByStableId(ECS::Components::StableId id);
        [[nodiscard]] const StableEntityLookupDiagnostics&
            LookupDiagnostics() const noexcept;

        [[nodiscard]] GizmoInteraction& Interaction() noexcept;
        [[nodiscard]] const GizmoInteraction& Interaction() const noexcept;

        // Editor gizmo frontend (UI-078). Selection, registry and history are
        // resolved here; every call validates the binding and the token
        // first. Snap is the frontend's: it is already applied to Gt.
        [[nodiscard]] GizmoUiFrame PrepareGizmo(GizmoOrientation orientation, GizmoPivotMode pivotMode);
        // Starts a session on the current entity selection. `token` must be
        // the current idle token from PrepareGizmo (else StaleSession;
        // SessionActive while a session runs). G0 is recomputed from current
        // state, so call PrepareGizmo and BeginGizmoDrag back to back with no
        // transform writes in between, or the frontend's start matrix differs
        // from G0. Mode, orientation and pivot mode freeze until the end.
        [[nodiscard]] GizmoUiBeginResult BeginGizmoDrag(
            const GizmoUiToken& token, GizmoMode mode, GizmoOrientation orientation, GizmoPivotMode pivotMode);
        // Absolute gizmo matrix Gt; the core applies Gt * G0^-1. A rejected
        // Gt keeps the last accepted state.
        [[nodiscard]] GizmoResult PreviewGizmoDrag(const GizmoUiToken& token, const glm::mat4& gizmoMatrix);
        // Records the last accepted state as one undo entry (none for a
        // no-op) and ends the session. Stale token: StaleEntity, no write.
        [[nodiscard]] EditorCommandHistoryResult CommitGizmoDrag(const GizmoUiToken& token);
        // Restores the start state without history. Stale token: StaleSession, no write.
        GizmoResult CancelGizmoDrag(const GizmoUiToken& token);

        [[nodiscard]] const std::optional<PrimitiveSelectionResult>&
            LastRefinedPrimitive() const noexcept;
        [[nodiscard]] std::uint64_t
            LastRefinedPrimitiveGeneration() const noexcept;

        // Transient points and lines an editor tool draws over the scene (for example a
        // registration preview) until it clears them. Each owner's overlay is replaced
        // as a whole; all overlays are dropped when the bound world changes.
        struct PreviewPoint
        {
            glm::vec3 Position{0.0f};
            glm::vec4 Color{1.0f};
            float Radius{0.01f}; // world units
            bool DepthTested{true};
            bool Sphere{false};  // shaded sphere (the point renderer's sphere look) instead of a flat dot
        };
        struct PreviewLine
        {
            glm::vec3 Start{0.0f}, End{0.0f};
            glm::vec4 Color{1.0f};
            bool DepthTested{true};
        };
        void SetPreviewOverlay(std::string_view owner, std::span<const PreviewPoint> points,
                               std::span<const PreviewLine> lines = {});
        void ClearPreviewOverlay(std::string_view owner);
        // Points an owner's overlay currently draws (0 when it has none).
        [[nodiscard]] std::size_t PreviewOverlayPointCount(std::string_view owner) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl{};
    };
}

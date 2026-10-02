// Shared execution dependencies and attachment-checked commands, independent of method records.
module;
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>
export module Extrinsic.Runtime.EditorProcessing;
export import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.EditorCommandHistory;
export import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.JobService;
// These services are borrowed only; their existing owners expose matching C++ linkage.
extern "C++" {
    namespace Extrinsic::ECS::Scene { class Registry; }
    namespace Extrinsic::RHI { class IDevice; }
    namespace Extrinsic::Runtime {
        class SpatialIndexCache;
        class SelectionController;
        struct EditorProcessingCommandsAccess;
        struct EditorPointInputReadinessState;
    }
}
export namespace Extrinsic::Runtime
{
    // C++ language linkage so the private attachment interface can borrow the
    // one context the session prepares without importing this module.
    extern "C++"
    {
        struct EditorProcessingContext
        {
            ECS::Scene::Registry* Scene{};
            WorldHandle World{DefaultWorldHandle};
            EditorCommandHistory* CommandHistory{};
            SpatialIndexCache* SpatialIndices{};
            // Borrowed render device. Operations that offer a GPU backend gate on
            // IsOperational() and fall back to CPU; a null device is not an error.
            RHI::IDevice* Device{};
            EditorJobCommandSurface JobCommands{};
            const RuntimeEngineConfigControlState* EngineConfigControlState{};
            std::function<Core::Config::EngineConfigLoadResult(const std::string&, const std::string&)> PreviewEngineConfigDocument{};
            std::function<RuntimeEngineConfigApplyResult(const Core::Config::EngineConfigLoadResult&)> ApplyEngineConfigHotSubset{};
            std::function<bool()> AttachmentActive{};
            std::function<void()> InvalidateWorkspaceSnapshotCache{};
            bool EngineConfigCommandsAvailable{};
            SelectionController* Selection{};
            // Host-declared kernel availability. These gate execution inside the
            // mesh families and feed the editor model's method availability, so
            // they are plain shared data rather than a per-method capability
            // surface. A false flag means the operation refuses and reports why.
            bool MeshDenoiseKernelAvailable{true};
            bool MeshCurvatureKernelAvailable{true};
            bool MeshCurvatureDirectionsAvailable{true};
            bool CurvatureSegmentationKernelAvailable{true};
            bool MeshRemeshUniformKernelAvailable{true};
            bool MeshRemeshAdaptiveKernelAvailable{true};
            bool MeshRemeshProjectToSurfaceAvailable{true};
            bool MeshRemeshErrorBoundedSizingAvailable{true};
            bool MeshSubdivideLoopKernelAvailable{true};
            bool MeshSubdivideCatmullClarkKernelAvailable{true};
            bool MeshSubdivideSqrt3KernelAvailable{true};
            bool MeshSubdivideLoopFeatureEdgesAvailable{true};
            bool MeshSimplifyKernelAvailable{true};
            // Prepared sessions share deferred point-input verdicts. Standalone
            // contexts without this state perform synchronous preflight.
            std::shared_ptr<EditorPointInputReadinessState> PointInputReadiness{};
        };
    }

    // Why an action is disabled, as a stable code the UI and agents can branch on. `Unclassified`
    // marks a reason from a producer that only supplies text (a plain `{false, "..."}` readiness).
    enum class ActionReadinessCode : std::uint8_t
    {
        Ok, WorkspaceUnavailable, MissingEntity, WrongDomain, MissingProperty, IncompatibleProperty,
        ElementCountMismatch, InvalidConfig, ConflictingOptions, DeviceUnavailable, KernelUnavailable,
        JobActive, StaleInput, PendingVerdict, Unclassified,
    };
    // snake_case name of the code ("invalid_config"); the agent lane's `reasons[].code`.
    [[nodiscard]] std::string_view ToString(ActionReadinessCode code) noexcept;
    struct ActionReadinessReason
    {
        ActionReadinessCode Code{ActionReadinessCode::Unclassified};
        // The offending config field's schema key (ConfigFieldSpec::Name); empty = the whole action.
        std::string Field{};
        std::string Message{};
    };

    // Copied presentation state; commands still validate current inputs at apply time.
    // `Reasons` lists every independent failing check in check order; `DisabledReason` is the
    // first reason's message. Producers that set only `DisabledReason` leave `Reasons` empty
    // (`ActionReadinessReasons` then supplies one Unclassified reason).
    struct ActionReadiness
    {
        bool Enabled{};
        std::string DisabledReason{};
        std::vector<ActionReadinessReason> Reasons{};
    };
    // Enabled exactly when `reasons` is empty; `DisabledReason` is the first message.
    [[nodiscard]] ActionReadiness MakeActionReadiness(std::vector<ActionReadinessReason> reasons);
    // Every reason of a disabled readiness: `Reasons`, or one Unclassified reason carrying
    // `DisabledReason` from a text-only producer; empty when enabled.
    [[nodiscard]] std::vector<ActionReadinessReason> ActionReadinessReasons(const ActionReadiness& readiness);

    struct EditorPointInputReadinessStats
    {
        std::uint64_t ChecksQueued{}, PropertyScans{};
    };

    class EditorProcessingCommands final
    {
    public:
        EditorProcessingCommands() = default;
        [[nodiscard]] bool IsBound() const noexcept;
    private:
        std::shared_ptr<const EditorProcessingContext> m_Context{};
        friend struct EditorProcessingCommandsAccess;
        friend EditorProcessingCommands BindEditorProcessingCommands(EditorProcessingContext);
    };
    [[nodiscard]] EditorProcessingCommands BindEditorProcessingCommands(EditorProcessingContext);
    // Progress of the run `key` names (see `EditorJobCommandSurface::Progress`);
    // `State::None` for an unbound handle or a key that resolves to no job.
    [[nodiscard]] EditorOperationProgress GetEditorOperationProgress(
        const EditorProcessingCommands&, const EditorOperationRunKey& key);
    // `EditorJobCommandSurface::SceneEpoch`; 0 for an unbound or stale handle.
    [[nodiscard]] std::uint64_t GetEditorSceneEpoch(const EditorProcessingCommands&);
    // Every editor-owned job (`EditorJobCommandSurface::SnapshotAll`); empty for an
    // unbound or stale handle.
    [[nodiscard]] std::vector<EditorJobRecord> GetEditorJobs(const EditorProcessingCommands&);
    // `EditorJobCommandSurface::Cancel`; `Unavailable` for an unbound or stale handle.
    [[nodiscard]] EditorJobCancelStatus CancelEditorJob(const EditorProcessingCommands&, JobToken token);
    // `EditorJobCommandSurface::Stats`; zeroed for an unbound or stale handle.
    [[nodiscard]] JobServiceStats GetEditorJobStats(const EditorProcessingCommands&);
    // Whether the Jobs window's Cancel applies to `job`, with the runtime's reason when it does not
    // (already ended, not an editor job, cancel already requested, no attachment). A helper job
    // (`EditorJobIdentity::Auxiliary`) is cancelled on its own, so its run's cancel does not disable it.
    [[nodiscard]] ActionReadiness ResolveEditorJobCancelReadiness(const EditorProcessingCommands&, const EditorJobRecord& job);
    // Cancels the run `job` belongs to (its head job and every stage queued after it), as the agent's
    // `notifications/cancelled` does, so no later stage starts on a cancelled run; a helper job
    // (`EditorJobIdentity::Auxiliary`) cancels only itself because its run goes on. `Requested` when at
    // least one job accepted the cancel.
    [[nodiscard]] EditorJobCancelStatus CancelEditorJobRun(const EditorProcessingCommands&, const EditorJobRecord& job);
    // `CancelEditorRuns` over the handle's surface; `Unavailable` for an unbound or stale handle.
    [[nodiscard]] EditorRunCancelCount CancelEditorRunJobs(const EditorProcessingCommands&, std::span<const JobToken> runs);
    // `EditorJobCommandSurface::RunCancelRequested`; false for an unbound or stale handle.
    [[nodiscard]] bool IsEditorRunCancelRequested(const EditorProcessingCommands&, JobToken run);
    // True when an attached handle can preview and hot-apply an engine config
    // document. Every family's `ApplyEditor*Config` needs exactly this, so panels
    // gate their controls on it instead of discovering the rejection.
    [[nodiscard]] bool AreEditorProcessingConfigCommandsAvailable(
        const EditorProcessingCommands&) noexcept;
    // Config-backed actions need both the live config lane and their method preflight.
    // Missing config commands take priority; otherwise preserve the method's reason.
    [[nodiscard]] ActionReadiness ResolveEditorProcessingActionReadiness(
        const EditorProcessingCommands&, ActionReadiness method);
    // A queued job publishes on a later frame, after the call that queued it (an agent call under
    // "Agent: ") has ended. Wraps `desc.PublishCompletion` so history entries recorded while it
    // runs carry `prefix` (the history's prefix at submission); an empty prefix changes nothing.
    // `active` (optional) guards against a history that no longer belongs to the live session.
    void CarryEditorLabelPrefix(JobDesc& desc, EditorCommandHistory* history, std::string prefix,
                                std::function<bool()> active = {});
    // Live finite vec3 rows on every resolved element domain of the entity, with
    // each entry's property revision and membership folded into the generation.
    // Prepared sessions omit pending entries until the command drain validates
    // them; standalone contexts validate synchronously. Every
    // method whose point input accepts any such property shares this catalog;
    // methods that additionally trial-capture an output or require a minimum
    // sample count own their own narrower catalog.
    [[nodiscard]] GeometryPropertyCatalogSnapshot GetEditorPointInputCatalog(
        const EditorProcessingCommands&, std::uint32_t stableId);
    [[nodiscard]] EditorPointInputReadinessStats GetEditorPointInputReadinessStats(
        const EditorProcessingCommands&);
}

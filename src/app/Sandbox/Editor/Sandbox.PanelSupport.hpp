// Shared app-private panel context, controls and action/view models.
// Include after module imports; standard and GLM headers belong in the caller's
// global module fragment. C++ linkage lets all panel implementations share it.
#pragma once


extern "C++"
{
namespace Extrinsic::Runtime
{
    struct EditorPointCloudServicePreparedFrame;
}

namespace Extrinsic::Sandbox::Editor
{
    inline constexpr std::array<const char*, 6> kColormapNames{{
        "Viridis", "Inferno", "Plasma", "Jet", "Coolwarm", "Heat"}};

    // Borrows panel storage only for the current draw call.
    struct TextureBakeUiState
    {
        std::optional<Runtime::EditorUvRegenerationCommandResult>*
            LastUvRegenerationResult{nullptr};
        std::optional<Runtime::EditorUvRegenerationCommandResult>*
            LastUvExtentAdoption{nullptr};
        std::int32_t* SourceIndex{nullptr};
        std::int32_t* TargetSemanticIndex{nullptr};
        std::int32_t* EncoderIndex{nullptr};
        std::int32_t* StorageIndex{nullptr};
        std::int32_t* ColormapIndex{nullptr};
        std::int32_t* NormalSpaceIndex{nullptr};
        std::uint32_t* AdditionalConsumerMask{nullptr};
        std::int32_t* Width{nullptr};
        std::int32_t* Height{nullptr};
        std::int32_t* Padding{nullptr};
        bool* UvForceRegenerate{nullptr};
        bool* UvPreserveAuthored{nullptr};
    };

    inline constexpr auto kUniformColorSource =
        static_cast<decltype(Runtime::EditorVisualizationConfigModel{}.Source)>(1);

    inline constexpr auto kScalarFieldSource =
        static_cast<decltype(Runtime::EditorVisualizationConfigModel{}.Source)>(2);

    void DrawBoundRenderStateRows(const Runtime::EditorBoundRenderStateModel& bound);

    void DrawDiagnostics(const std::vector<Runtime::EditorDiagnostic>& diagnostics);

    void DrawDomainWindowHeader(
        const Runtime::EditorDomainWindowModel& model);

    [[nodiscard]] bool DomainWindowReady(
        const Runtime::EditorDomainWindowModel& model) noexcept;

    void DrawVec3(const char* label, const glm::vec3 value);

    [[nodiscard]] Runtime::EditorVisualizationConfigCommand
    MakeVisualizationConfigCommandFromModel(
        const std::uint32_t stableEntityId,
        const Runtime::EditorVisualizationConfigModel& model,
        const Runtime::EditorVisualizationTarget target);

    [[nodiscard]] Runtime::EditorVisualizationConfigCommand
    MakeUniformVisualizationConfigCommandFromModel(
        const std::uint32_t stableEntityId,
        const Runtime::EditorVisualizationConfigModel& model,
        const Runtime::EditorVisualizationTarget target,
        const glm::vec4 color);

    [[nodiscard]] bool DrawDismissLastResultButton(const char* label);

    // Overlay text of every progress bar (asset queue rows and operation
    // panels): "NN%" when determinate, else `fallbackLabel`, followed by the
    // elapsed time when `elapsedSeconds` is given.
    [[nodiscard]] std::string FormatProgressOverlay(
        bool determinate,
        float normalized,
        std::string_view fallbackLabel,
        std::optional<double> elapsedSeconds = std::nullopt);

    // What `DrawOperationProgress` shows for one read-model value. A negative
    // `Fraction` is an indeterminate (animated) bar.
    struct OperationProgressView
    {
        bool Visible{false};
        bool Bar{false};            // Queued / Running: a bar; a finished run: a status line
        float Fraction{-1.0f};
        std::string Overlay{};
        std::string Diagnostic{};
        bool ShowCancel{false};
    };
    [[nodiscard]] OperationProgressView DescribeOperationProgress(
        const Runtime::EditorOperationProgress& progress, bool hasCancelHandler);

    // The one progress widget of every method panel: nothing for `State::None`;
    // a bar with overlay for Queued/Running; a status line for a finished run;
    // a Cancel button only while the run is active and the panel supplies a
    // cancel path (`onCancel`; the read model itself has none until
    // RUNTIME-279). `id` (required) keeps several widgets in one window apart. The
    // overlay shows the run's label for indeterminate bars.
    void DrawOperationProgress(
        const Runtime::EditorOperationProgress& progress,
        const std::function<void()>& onCancel,
        const char* id);

    // A panel-owned iterative run (ICP, CPD) as a Running read-model value: a
    // determinate fraction of `maxIterations` (0: indeterminate).
    [[nodiscard]] Runtime::EditorOperationProgress MakeIterationProgress(
        std::size_t completedIterations, std::uint32_t maxIterations, double elapsedSeconds, std::string label);

    // The one pattern of every method panel that shows its run's progress. The run's key is
    // captured when the run is SUBMITTED and QUEUED (never from the editable draft); with nothing
    // watched, the draft's own (entity, output) is asked instead, so a run started elsewhere (an
    // agent or batch call) on that output shows too. The widget shows only while the run's entity
    // is selected. The runtime drops a finished job a frame after it ends, so the slot keeps the
    // last projection of the current key until its next run; it is dropped when the scene epoch
    // (`live.Epoch`) changes. A run that vanishes while still active leaves no outcome. A GPU
    // transaction waiting for Accept reads "awaiting accept" instead of its finished compute job,
    // and a discarded result is forgotten.
    class OperationRunSlot
    {
    public:
        // `DrawLive` with this entity shows regardless of the selection (a panel-global run).
        static constexpr std::uint32_t kAnyEntity = 0xFFFFFFFFu;

        // Call where the command was submitted and came back queued. `key` names that run: its
        // output, the correlation id its submission returned, or a job token.
        void Watch(std::uint32_t entity, Runtime::EditorOperationRunKey key);
        void WatchOutput(const std::uint32_t entity, std::string outputName)
        {
            Watch(entity, Runtime::EditorOutputRef{entity, std::move(outputName)});
        }
        // `WatchOutput` when `result` is a queued (Pending) answer of the submission; nothing else
        // left a job to show.
        template <class Result>
        void WatchOutputIfQueued(const std::optional<Result>& result, const std::uint32_t entity, std::string outputName)
        {
            if (result.has_value() && result->Status == Runtime::EditorCommandStatus::Pending)
                WatchOutput(entity, std::move(outputName));
        }
        // A submission refused as a duplicate (Pending, nothing queued: the output's active run, maybe
        // another caller's, keeps its own callback). Follows that run and shows `refusal` with its
        // progress; never a result of this panel (RUNTIME-313).
        void WatchDuplicate(std::uint32_t entity, std::string outputName, std::string refusal)
        {
            WatchOutput(entity, std::move(outputName));
            m_Note = std::move(refusal);
        }
        [[nodiscard]] const std::string& Note() const noexcept { return m_Note; }
        // Every own submission starts here: an earlier duplicate refusal no longer describes it,
        // whatever this one answers (queued, refused by apply, failed at once).
        void ClearNote() noexcept { m_Note.clear(); }
        [[nodiscard]] bool Watching() const noexcept { return m_Watched.has_value(); }
        // The watched run's key, or null.
        [[nodiscard]] const Runtime::EditorOperationRunKey* WatchedKey() const noexcept
        {
            return m_Watched ? &m_Watched->Key : nullptr;
        }
        [[nodiscard]] bool WatchesOutput(std::uint32_t entity, const std::string& outputName) const;
        // From the transaction's phase each frame; Accepting/Applied read through the job as usual.
        void AwaitingAccept(const bool waiting) noexcept { m_AwaitingAccept = waiting; }
        // A discarded (or otherwise withdrawn) result must not read as a finished run.
        void Forget();

        // The current run's projection through the panel's commands; stamped with the scene epoch
        // even when nothing is asked.
        [[nodiscard]] Runtime::EditorOperationProgress Query(
            const Runtime::EditorProcessingCommands& commands, const Runtime::EditorOutputRef* draft = nullptr) const;
        // Query + DrawLive: the one call after a panel's action button. `draft` is the panel's
        // current (entity, output), used while nothing is watched. `onCancel` as in
        // `DrawOperationProgress`. True when a run was shown.
        bool Draw(const Runtime::EditorProcessingCommands& commands, std::uint32_t selectedEntity, const char* id,
                  const Runtime::EditorOutputRef* draft = nullptr, const std::function<void()>& onCancel = {});
        // For runs whose projection the panel supplies itself (transaction snapshots, ICP/CPD bars).
        void DrawLive(const Runtime::EditorOperationProgress& live, std::uint32_t selectedEntity,
                      const std::function<void()>& onCancel, const char* id);
        // The remembered projection for `key` after seeing `live` (exposed for tests).
        [[nodiscard]] const Runtime::EditorOperationProgress& Observe(
            const Runtime::EditorOperationProgress& live, const std::string& key);

    private:
        struct Watched
        {
            std::uint32_t Entity{0u};
            Runtime::EditorOperationRunKey Key{Runtime::EditorRunCorrelation{}};
            std::string Description{};
        };
        std::optional<Watched> m_Watched{};
        std::string m_Note{};
        bool m_AwaitingAccept{false};
        Runtime::EditorOperationProgress m_Held{};
        std::string m_HeldKey{};
        std::uint64_t m_Epoch{0u};
    };

    // UI-060: the Jobs window's memory. The runtime drops a finished job a frame after it ends, so the
    // window keeps the last `kFinishedLimit` finished rows it saw; a job that vanished while still
    // active leaves no row (its outcome was never seen). Everything is dropped when the scene epoch
    // changes (scene new/load/close, workspace reattach).
    class JobsHistory
    {
    public:
        static constexpr std::size_t kFinishedLimit = 32u;
        void Observe(std::span<const Runtime::EditorJobRecord> live, std::uint64_t epoch);
        // Submission order, oldest first.
        [[nodiscard]] const std::vector<Runtime::EditorJobRecord>& Rows() const noexcept { return m_Rows; }
    private:
        std::vector<Runtime::EditorJobRecord> m_Rows{};
        std::uint64_t m_Epoch{0u};
    };
    struct JobsWindowState
    {
        JobsHistory History{};
        // The last Cancel press whose answer was not `Requested`, shown under the table.
        std::string CancelNote{};
    };
    // The table of running and recent editor jobs with a Cancel per active row (the whole run, see
    // `CancelEditorJobRun`; a disabled Cancel shows the runtime's reason), and the collapsed job-service
    // counters. Draws content only; the caller owns the ImGui window.
    void DrawJobsWindow(const Runtime::EditorProcessingCommands& commands, JobsWindowState& state);
    // The backend column: one word when requested and resolved agree, else "requested -> resolved";
    // an unknown side reads "-" (no producer reports domains yet, RUNTIME-317).
    [[nodiscard]] std::string FormatJobBackend(std::optional<Runtime::EditorJobDomain> requested,
                                               std::optional<Runtime::EditorJobDomain> resolved);

    template <typename Result, typename Sink>
    void PublishCommandResult(std::optional<Result>& destination, Result result, const Sink& sink)
    {
        destination = result;
        if (sink)
            sink(std::move(result));
    }

    // True when an editor job exists now that was not among `before`.
    [[nodiscard]] bool QueuedEditorJob(const Runtime::EditorProcessingCommands& commands,
                                       const std::vector<Runtime::EditorJobRecord>& before);

    // A panel action that may queue a job, watched as {entity, output}: applies the request, runs
    // it and publishes the answer. Pending with no new editor job is a duplicate refusal: the
    // output's active run (another click, an agent call) keeps its own callback. Publishing it
    // would leave "Pending" as the panel's result for good, so the run slot follows the active run
    // and shows the refusal. `state` provides ConfigDiagnostic, LastResult and Run.
    template <typename State, typename Request, typename Apply, typename Execute, typename Sink>
    void ApplyQueuedProcessingExecution(const Runtime::EditorProcessingCommands& commands, State& state,
        const Request& request, Apply apply, Execute execute, const Sink& sink, const char* rejected,
        const std::uint32_t entity, std::string output)
    {
        state.Run.ClearNote();
        const bool applied = apply(request).Succeeded();
        state.ConfigDiagnostic = applied ? "" : rejected;
        if (!applied) return;
        const auto before = Runtime::GetEditorJobs(commands);
        auto result = execute();
        if (result.Status == Runtime::EditorCommandStatus::Pending && !QueuedEditorJob(commands, before))
        {
            state.Run.WatchDuplicate(entity, std::move(output), result.Message);
            return;
        }
        PublishCommandResult(state.LastResult, std::move(result), sink);
        state.Run.WatchOutputIfQueued(state.LastResult, entity, std::move(output));
    }

    void DrawDisabledReasonTooltip(std::string_view disabledReason);

    // UI-071: the one Stop/Accept/Discard row of a two-phase GPU transaction (Run, then Accept or
    // Discard). Families adapt their snapshot to this view; the helper owns which buttons are
    // enabled in which phase, where an Accept refusal is shown and how the upload counters read.
    struct GpuTransactionIo
    {
        std::uint64_t UploadBytes{}, CacheHits{};
        std::optional<std::uint64_t> CpuStageUploadBytes{}, CpuReadbackBytes{}; // shown only when the family reports them
    };
    struct GpuTransactionRowView
    {
        Runtime::EditorGpuTransactionPhase Phase{Runtime::EditorGpuTransactionPhase::Running};
        bool CanAccept{false};
        // The runtime's answer for a result that waits but cannot be accepted (stale inputs, a
        // run still going); empty when there is nothing to say. Never composed by the UI.
        std::string_view AcceptRefusal{};
        bool HasStop{false}; // the family can stop a run early and keep its preview (else Discard cancels)
        std::optional<GpuTransactionIo> Io{};
    };
    enum class GpuTransactionRowAction : std::uint8_t { None, Stop, Accept, Discard };
    struct GpuTransactionRowState
    {
        bool StopEnabled{}, AcceptEnabled{}, DiscardEnabled{}, ShowRefusal{};
    };
    // Stop runs only while the device work runs; Accept only when the runtime says so; Discard
    // while the transaction is live (running, waiting or accepting), never once it is terminal;
    // the refusal line shows whenever Accept is refused with a reason.
    [[nodiscard]] GpuTransactionRowState ResolveGpuTransactionRowState(const GpuTransactionRowView& view) noexcept;
    [[nodiscard]] std::string FormatGpuTransactionIo(const GpuTransactionIo& io);
    // For observations that report flags instead of a phase (K-Means, point-cloud consolidation).
    [[nodiscard]] constexpr Runtime::EditorGpuTransactionPhase GpuTransactionPhaseOf(
        const bool readyToAccept, const bool accepting) noexcept
    {
        return accepting ? Runtime::EditorGpuTransactionPhase::Accepting
             : readyToAccept ? Runtime::EditorGpuTransactionPhase::ReadyToAccept
                             : Runtime::EditorGpuTransactionPhase::Running;
    }
    static_assert(GpuTransactionPhaseOf(false, false) == Runtime::EditorGpuTransactionPhase::Running);
    static_assert(GpuTransactionPhaseOf(true, false) == Runtime::EditorGpuTransactionPhase::ReadyToAccept);
    static_assert(GpuTransactionPhaseOf(false, true) == Runtime::EditorGpuTransactionPhase::Accepting);
    static_assert(GpuTransactionPhaseOf(true, true) == Runtime::EditorGpuTransactionPhase::Accepting, "accepting wins over ready");
    // Draws [Stop] Accept Discard on one line (IDs suffixed `##idSuffix`), the Accept refusal as a
    // tooltip on the disabled button plus one inline line, and the counters line. Returns the
    // button pressed this frame; the caller invokes its family's command.
    [[nodiscard]] GpuTransactionRowAction DrawGpuTransactionControls(const GpuTransactionRowView& view, const char* idSuffix);

    // UI-074: shared "Color interpretation" combo and its help text. Behavior documented here is the
    // CPU encoder's (Runtime.VisualizationRecipes.cpp, AppendColorPacket): Components passes vec4 as
    // (r,g,b,a), vec3 as (r,g,b,1), vec2 as (x,y,0,1) unrescaled, and integers/bools/integral floats as hashed
    // label colors (non-integral floats are rejected there); NormalDirection normalizes (zero length -> +Z) and
    // maps n*0.5+0.5 per axis. Every domain's Appearance panel calls this one helper.
    [[nodiscard]] std::string_view ColorInterpretationComboTooltip() noexcept;
    [[nodiscard]] std::string_view ColorInterpretationOptionTooltip(int interpretation) noexcept;
    // `interpretation` is the VisualizationConfig::ColorInterpretation integer code. Returns true when changed.
    [[nodiscard]] bool DrawColorInterpretationCombo(int& interpretation);
    [[nodiscard]] bool DrawProcessingActionButton(
        const char* label, const Runtime::ActionReadiness& readiness);
    // UI-071: the one way a panel's own gating (config control missing, nothing to undo, a draft that does not
    // validate) reaches `DrawProcessingActionButton`: the first blocker that applies supplies the reason the
    // tooltip shows; none applying leaves the action enabled. Runtime readiness, where it exists, goes through
    // `ResolveEditorProcessingActionReadiness` instead and is never restated here.
    struct ActionBlocker
    {
        bool Blocks{false};
        std::string_view Reason{};
    };
    [[nodiscard]] Runtime::ActionReadiness ReadinessUnlessBlocked(std::initializer_list<ActionBlocker> blockers);
    // The reason every Run action shows while its own GPU transaction is live (running, waiting or accepting).
    inline constexpr std::string_view kPendingGpuRunReason = "Accept or Discard the pending GPU run before starting another.";
    // `readiness` unless the panel's own transaction is live: a runtime refusal keeps its reason, otherwise the
    // action is disabled with `kPendingGpuRunReason`. Never wrap the button in a bare BeginDisabled: that hides the reason.
    [[nodiscard]] Runtime::ActionReadiness ReadinessWhileGpuRunPending(Runtime::ActionReadiness readiness, bool pending);

    // UI-057: parameter controls driven by a config section's field table. Hovering a
    // control shows the field's description, accepted values and default; numeric input
    // is clamped to the declared bounds and enum combos are labeled from EnumNames, so
    // the panel, the validator and the agent schema read the same declaration.
    [[nodiscard]] std::string FormatConfigFieldHint(const Runtime::ConfigFieldSpec& field, std::string_view defaultValue);
    void DrawConfigFieldHint(const Runtime::ConfigFieldSpec* field, std::string_view defaultValue);
    // Hint of the field `name` on the last item, for controls the numeric/enum widgets below do not draw
    // (property pickers, name inputs, sliders). A missing name draws nothing.
    void DrawSpecFieldHint(std::span<const Runtime::ConfigFieldSpec> fields, std::string_view name,
                           std::string_view defaultValue = {});
    bool DrawSpecInputDouble(const char* label, std::span<const Runtime::ConfigFieldSpec> fields, std::string_view name,
                             double& value, double defaultValue, const char* format = "%.6g");
    // Drag controls whose bounds are the table's: the value is clamped to the declared closed range before
    // drawing and the drag cannot leave it; a bound the table leaves open stays open.
    bool DrawSpecDragInt(const char* label, std::span<const Runtime::ConfigFieldSpec> fields, std::string_view name,
                         int& value, int defaultValue, float speed = 1.0f);
    bool DrawSpecDragDouble(const char* label, std::span<const Runtime::ConfigFieldSpec> fields, std::string_view name,
                            double& value, double defaultValue, float speed, const char* format = "%.3f");
    bool DrawSpecInputUInt(const char* label, std::span<const Runtime::ConfigFieldSpec> fields, std::string_view name,
                           std::uint32_t& value, std::uint32_t defaultValue);
    // `value` is the payload's integer code (first name = the field's Min).
    // `visibleCount` (> 0) offers only the first names, for a mode in which later values are not selectable.
    bool DrawSpecEnumCombo(const char* label, std::span<const Runtime::ConfigFieldSpec> fields, std::string_view name,
                           int& value, int defaultValue, int visibleCount = -1);
    template <class TEnum>
    bool DrawSpecEnumCombo(const char* label, std::span<const Runtime::ConfigFieldSpec> fields, std::string_view name,
                           TEnum& value, TEnum defaultValue, int visibleCount = -1)
    {
        int code = static_cast<int>(value);
        if (!DrawSpecEnumCombo(label, fields, name, code, static_cast<int>(defaultValue), visibleCount)) return false;
        value = static_cast<TEnum>(code);
        return true;
    }
    void DrawSpecCheckbox(const char* label, std::span<const Runtime::ConfigFieldSpec> fields, std::string_view name,
                          bool& value, bool defaultValue, bool& changed);

    extern "C++"
    {
        struct SandboxEditorFrame final : Runtime::EditorWorkspaceSnapshot
        {
            SandboxEditorFrame() = default;
            explicit SandboxEditorFrame(const Runtime::EditorWorkspaceSnapshot& frame);
        };

        // Most view data is copied; the point-cloud service frame and live command
        // handles are borrowed only for the prepared-frame draw visit.
        struct SandboxEditorContext final
        {
            SandboxEditorContext() = default;
            SandboxEditorContext(
                const Runtime::EditorWorkspaceSnapshotPreparedFrame& workspace,
                const Runtime::EditorSceneEditingPreparedFrame& scene,
                const Runtime::EditorProcessingCommands& processing,
                const Runtime::EditorPointFieldPreparedFrame& pointFields,
                const Runtime::EditorPointAnalysisPreparedFrame& pointAnalysis,
                const Runtime::EditorPointSetPreparedFrame& pointSet,
                const Runtime::EditorPointConstructionPreparedFrame& pointConstruction,
                Runtime::EditorPointCloudServicePreparedFrame& pointCloudService,
                const Runtime::EditorNormalPreparedFrame& normals,
                const Runtime::EditorRegistrationPreparedFrame& registration,
                const Runtime::EditorMeshFieldPreparedFrame& meshFields,
                const Runtime::EditorMeshTopologyPreparedFrame& meshTopology,
                const Runtime::EditorParameterizationPreparedFrame& parameterization,
                const Runtime::EditorVisualizationEditingPreparedFrame& visualization,
                const Runtime::EditorRenderRecipeEditingPreparedFrame& renderRecipe,
                SandboxEditorFrame& frame);

            Runtime::EditorSceneEditingCommands SceneCommands{};
            // Shared processing handle for surfaces every family reuses, such as
            // the common point-input catalog, discovery and primitive selection.
            Runtime::EditorProcessingCommands Processing{};
            Runtime::EditorPointFieldPreparedFrame PointFields{};
            Runtime::EditorPointAnalysisPreparedFrame PointAnalysis{};
            Runtime::EditorPointSetPreparedFrame PointSet{};
            Runtime::EditorPointConstructionPreparedFrame PointConstruction{};
            // Borrows shell-owned frame storage for the current draw visit.
            const Runtime::EditorPointCloudServicePreparedFrame* PointCloudService{nullptr};
            Runtime::EditorNormalPreparedFrame Normals{};
            Runtime::EditorRegistrationPreparedFrame Registration{};
            Runtime::EditorMeshFieldPreparedFrame MeshFields{};
            Runtime::EditorMeshTopologyPreparedFrame MeshTopology{};
            Runtime::EditorParameterizationPreparedFrame Parameterization{};
            Runtime::EditorVisualizationEditingCommands VisualizationCommands{};
            Runtime::EditorRenderRecipeEditingCommands RenderRecipeCommands{};
            Runtime::EditorWorkspaceSnapshotQueries SnapshotQueries{};
            Runtime::EditorAssetImportQueueCommandSurface
                AssetImportQueueCommands{};
            Runtime::EditorDocumentCommandSurface DocumentCommands{};
            Runtime::EditorRenderRecipeDraftSnapshot RenderRecipeDraft{};
            bool SceneAvailable{false};
            bool ProcessingConfigCommandsAvailable{false};
            bool RenderRecipeCommandsAvailable{false};
            bool RenderArtifactCommandsAvailable{false};
            const Runtime::EditorSelectionModel* Selection{nullptr};
            const Runtime::EditorDocumentModel* Document{nullptr};
            Runtime::EditorWorkspaceSnapshotStats* ModelBuildStats{nullptr};
            // Claims the scene rectangle (window coordinates: x, y, width,
            // height) for this UI frame; unclaimed frames render full-window.
            std::function<void(float, float, float, float)> ClaimSceneViewport{};
        };

        // Camera panel's "View" row (UI-070): the view presets frame the selection (the whole scene
        // when nothing is selected) and "Focus selection" frames the selection, both through
        // ApplyEditorCameraPoseCommand, the command the agent's set_camera uses. The last
        // refusal is shown under the row and cleared when the controller kind changes. The Focus
        // button is the last item drawn, disabled with a reason while nothing is selected.
        struct CameraViewUiState
        {
            Runtime::EditorCommandStatus Status{Runtime::EditorCommandStatus::Applied};
            std::optional<Runtime::EditorCameraControllerKind> Kind{};
        };
        void DrawCameraViewControls(const SandboxEditorContext& context,
                                    std::span<const std::uint32_t> selectedStableIds,
                                    Runtime::EditorCameraControllerKind controllerKind,
                                    CameraViewUiState& state);

    }

    // Each drawing surface retains its own persistent rename draft and diagnostic, and the
    // run of the last bake it submitted (UI-073), watched by the bake's run-job token.
    struct TextureBakeMutationUiState
    {
        std::string RenameTarget{};
        std::array<char, 128> RenameBuffer{};
        std::string MutationDiagnostic{};
        // The last refused Bake request's reason (e.g. "still loading ...
        // retry"); cleared by the next accepted request.
        std::string BakeDiagnostic{};
        OperationRunSlot BakeRun{};
    };

    // The run of the bake writing `outputName` on `entity` that the editor job surface knows (one
    // submitted through the editor's bake command), with a Cancel of that run (UI-073): the slot asks
    // for the output's newest run. Bakes the runtime starts itself (surface appearance, asset import)
    // carry no editor identity and are not found. True when a run was shown.
    bool DrawTextureBakeOutputRun(OperationRunSlot& slot, const Runtime::EditorProcessingCommands& commands,
                                  std::uint32_t entity, const std::string& outputName, const char* id);

    void DrawTextureBakeControls(
        const Runtime::EditorTextureBakeControlsModel& model,
        const SandboxEditorContext* context,
        TextureBakeUiState* state,
        TextureBakeMutationUiState& mutation);

    void DrawUniformVisualizationColorEdit(
        const Runtime::EditorVisualizationConfigModel& visualization,
        const SandboxEditorContext& context,
        std::uint32_t selectedStableId,
        Runtime::EditorVisualizationTarget target,
        bool canEditVisualization);

    // Both scalar blocks require the caller's scalar-source visibility check.
    void DrawScalarFieldColorControls(
        const Runtime::EditorVisualizationConfigModel& visualization,
        const SandboxEditorContext& context,
        std::uint32_t selectedStableId,
        Runtime::EditorVisualizationTarget target,
        bool canEditVisualization);

    void DrawScalarFieldBinAndIsolineControls(
        const Runtime::EditorVisualizationConfigModel& visualization,
        const SandboxEditorContext& context,
        std::uint32_t selectedStableId,
        Runtime::EditorVisualizationTarget target,
        bool canEditVisualization);

    // Storage the shared UV-regeneration block reads and writes. Every pointer
    // must be bound; callers pass either their panel-lifetime members or the
    // same frame-local fallbacks the rest of their bake panel uses, so an
    // adopted atlas extent lands where the bake request reads it.
    struct SandboxUvRegenerationControls
    {
        std::optional<Runtime::EditorUvRegenerationCommandResult>*
            LastResult{nullptr};
        std::optional<Runtime::EditorUvRegenerationCommandResult>*
            LastExtentAdoption{nullptr};
        std::int32_t* BakeWidth{nullptr};
        std::int32_t* BakeHeight{nullptr};
        std::int32_t* BakePadding{nullptr};
        bool* UvForceRegenerate{nullptr};
        bool* UvPreserveAuthored{nullptr};
    };

    // The one "Regenerate UVs" control: submission of the persisted atlas
    // configuration (edited and validated in the Parameterize (UV) window)
    // with its terminal callback, atlas-extent adoption, status and
    // dismissal. Every
    // texture-bake panel drives this helper, so the command, the session sink
    // that carries a queued job's terminal result, and the dismissal that
    // clears it cannot drift apart between panels.
    void DrawSandboxUvRegenerationControls(
        const Runtime::EditorTextureBakeControlsModel& model,
        const SandboxEditorContext* context,
        const SandboxUvRegenerationControls& controls);

    template <typename Config, typename Result>
    struct ProcessingDraftState
    {
        std::optional<std::vector<std::uint32_t>> LastSelectedEntity{};
        std::optional<Result> LastResult{};
        Config Draft{};
        std::string LastApplied{}, ConfigDiagnostic{}, VisualizationDiagnostic{};
        OperationRunSlot Run{};

        bool Synchronize(const Config& active, const std::string& serialized)
        {
            if (serialized == LastApplied) return false;
            Draft = active;
            LastApplied = serialized;
            ConfigDiagnostic.clear();
            return true;
        }
    };

    // The method catalog performs canonical preflight; it is queried only while
    // the combo is open. Unlike the fixed-domain selector, this may change domain.
    bool DrawProcessingPointInput(const char* label,
        const std::function<Runtime::GeometryPropertyCatalogSnapshot()>& getCatalog,
        Runtime::GeometryPropertyRef& property,
        std::optional<Runtime::GeometryElementDomain> domain = std::nullopt);

    bool DrawProcessingPropertyName(const char* label, std::string& name);
    bool DrawProcessingScalarOutput(const char* label, Runtime::GeometryPropertyRef& ref);
    struct ProcessingEntityInput
    {
        std::uint32_t Entity{};
        std::optional<std::vector<std::uint32_t>> PreviousSelection{};
    };
    [[nodiscard]] Runtime::EditorWorkspaceSnapshot BuildProcessingInputWorkspace(
        const SandboxEditorContext& context);
    bool SynchronizeProcessingEntity(const Runtime::EditorSelectionModel& selection,
        std::optional<std::vector<std::uint32_t>>& previousSelection, std::uint32_t& entity,
        std::size_t slot = 0u);
    bool DrawProcessingEntity(const char* label, const SandboxEditorContext& context,
        std::uint32_t& entity, std::optional<std::vector<std::uint32_t>>& previousSelection,
        std::optional<Runtime::EditorDomainWindowKind> domain = std::nullopt,
        std::size_t slot = 0u);
    void DrawProcessingCpuBackend();

    bool DrawProcessingPropertyInput(const char* label,
        const Runtime::EditorPropertyCatalogModel& catalog, Runtime::GeometryPropertyRef& property,
        bool (*accepts)(const Runtime::GeometryPropertyRef&) = nullptr,
        std::uint32_t maxComponents = 4u);
    [[nodiscard]] Runtime::EditorCommandStatus ShowProcessingProperty(
        const SandboxEditorContext& context, std::uint32_t entity,
        const Runtime::GeometryPropertyRef& property,
        bool normalDirection = false);
    // Show-property button: label defaults to "Show <property name>". `forceShow` applies the recipe without a
    // click (a follow-the-selector panel); the result is the applied status, empty when nothing was requested.
    // The status name lands in `diagnostic`; render it with DrawProcessingDisplayDiagnostic.
    std::optional<Runtime::EditorCommandStatus> DrawProcessingPropertyShowButton(
        const SandboxEditorContext& context, std::uint32_t entity, const Runtime::GeometryPropertyRef& property,
        std::string& diagnostic, const char* label = nullptr, bool normalDirection = false, bool forceShow = false);
    void DrawProcessingDisplayDiagnostic(const std::string& diagnostic, const char* prefix = "Display");

    // Dismissal clears both the panel result and the session slot that
    // rebuilds it. Draw this control after all readers of the panel result.
    template <typename ResultT, typename Slot, typename Dismiss>
    void DrawDismissLastResultButton(
        const char* const label,
        std::optional<ResultT>& panelResult,
        const Slot slot,
        const Dismiss& dismiss)
    {
        if (!DrawDismissLastResultButton(label))
            return;
        panelResult.reset();
        if (dismiss) dismiss(slot);
    }

    struct SandboxParameterizationStrategyOption
    {
        Runtime::EditorParameterizationStrategy Strategy{
            Runtime::EditorParameterizationStrategy::Lscm};
        std::string_view Label{};
        std::string_view StableToken{};
    };

    [[nodiscard]] std::array<SandboxParameterizationStrategyOption, 4u>
    SandboxParameterizationStrategyOptions() noexcept;

    struct SandboxParameterizationPanelActionResult
    {
        Runtime::RuntimeEngineConfigApplyResult Config{};
        std::optional<Runtime::EditorParameterizationResult> Execution{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Config.Succeeded() && Execution.has_value() &&
                   Execution->Succeeded();
        }
    };

    [[nodiscard]] SandboxParameterizationPanelActionResult
    ApplySandboxParameterizationPanelAction(
        const SandboxEditorContext& context,
        std::uint32_t stableEntityId,
        const Runtime::ParameterizationConfig& config);

    struct SandboxParameterizationUvPane
    {
        glm::vec2 Min{0.0f};
        glm::vec2 Max{0.0f};
        float Padding{20.0f};
        float Zoom{1.0f};
        glm::vec2 Pan{0.0f};
        bool IncludeUnitSquare{false};
    };

    struct SandboxParameterizationUvProjection
    {
        bool Valid{false};
        bool FitsPane{false};
        glm::vec2 PaneCenter{0.0f};
        glm::vec2 UvCenter{0.0f};
        float Scale{1.0f};
        float Zoom{1.0f};
        glm::vec2 Pan{0.0f};
        std::vector<glm::vec2> Vertices{};
        std::vector<std::array<std::uint32_t, 3u>> Triangles{};
        std::string Message{};
    };

    [[nodiscard]] SandboxParameterizationUvProjection
    BuildSandboxParameterizationUvProjection(
        const Runtime::EditorParameterizationViewModel& model,
        const SandboxParameterizationUvPane& pane);

    [[nodiscard]] glm::vec2 ProjectSandboxParameterizationUvPoint(
        const SandboxParameterizationUvProjection& projection,
        glm::vec2 uv) noexcept;

    // Status, requested/actual method and objective, fallback and diagnostic
    // of one atlas generation result.
    void DrawSandboxUvAtlasResult(
        const Runtime::EditorUvRegenerationCommandResult& result);

    // Display names for the atlas method/objective selectors and reports.
    [[nodiscard]] const char* SandboxUvAtlasMethodLabel(
        Geometry::UvAtlas::UvAtlasMethod method) noexcept;
    [[nodiscard]] const char* SandboxUvAtlasDistortionLabel(
        Geometry::UvAtlas::UvAtlasDistortion distortion) noexcept;

    struct SandboxParameterizationResultSummary
    {
        bool Succeeded{false};
        bool HasDiagnostics{false};
        std::string StrategyToken{};
        std::string CommandStatus{};
        std::string SolverStatus{};
        std::string Message{};
        std::size_t EvaluatedFaceCount{0u};
        std::size_t SkippedFaceCount{0u};
        std::size_t FlippedElementCount{0u};
        std::size_t BoundaryEdgeCount{0u};
        double MeanConformalDistortion{0.0};
        double MeanAreaDistortion{0.0};
        double MeanStretch{0.0};
    };

    [[nodiscard]] SandboxParameterizationResultSummary
    BuildSandboxParameterizationResultSummary(
        const Runtime::EditorParameterizationResult& result);

    [[nodiscard]] bool IsFiniteVec2(glm::vec2 value) noexcept;
}

} // extern "C++"

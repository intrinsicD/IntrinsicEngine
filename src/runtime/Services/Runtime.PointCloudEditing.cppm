// ARCH-022: point-cloud edits for plugins. Captures an entity's live points and normals, gives neighbourhood rows
// (k nearest or within a radius) on the CPU or the GPU, and publishes new positions into a vec3 output, or values
// into several scalar outputs, with one undo entry; or runs a point scalar method on the GPU with a resident preview
// until Accept or Discard, a one-shot step run the host's shared run controls drive. It wraps the editor's shared point helpers, so a plugin imports no editor module, and it
// holds no plugin callable: undo entries hold host data, so they work after the plugin that recorded them is unloaded.
module;

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

export module Extrinsic.Runtime.PointCloudEditing;

export import Extrinsic.Runtime.GeometryAvailability;
export import Extrinsic.Runtime.GeometryProperty.Types;
export import Geometry.PointLBVH;
export import Extrinsic.Graphics.PointQuery;
export import Extrinsic.Graphics.PointScalarAnalysis;
import Extrinsic.Graphics.GpuPropertyResidency;
export import Extrinsic.Runtime.PluginContract;
export import Extrinsic.Runtime.StepRunControls; // the host's run-control row and the shared execution options
import Extrinsic.Runtime.AgentOperations;
import Extrinsic.Runtime.EditorWorkspaceAttachment;
import Extrinsic.Runtime.ModuleLifecycle;

export namespace Extrinsic::Runtime
{
    // What to read and where to write: a vec3 `Output` (Publish), or with `ScalarOutputs` one scalar column each
    // (PublishScalars; `Output` is unused then). Unknown domains resolve to the entity's primary point domain, the
    // normals' and the outputs' to the positions'. An empty `Normals` name takes the positions alone.
    struct PointEditRequest
    {
        std::uint32_t StableEntityId{0u};
        GeometryPropertyRef Positions{};
        GeometryPropertyRef Normals{};
        GeometryPropertyRef Output{};
        std::vector<GeometryPropertyRef> ScalarOutputs{};
        std::size_t MinimumLiveCount{2u};
        std::string Noun{}; // a missing entity answers "<Noun> target entity is stale or missing." (default "The")
        bool CopyValues{true};    // false: readiness only, nothing copied
        bool AcquireIndex{false}; // the cached spatial index of the points (CPU and GPU neighbourhoods)
        // With AcquireIndex: also its CPU hierarchy. False (ARCH-023: a capture only GPU rows read): none is built for
        // it; CpuIndex answers null and CpuRadiusRows refuses.
        bool CpuIndex{true};
        // The entity's local Transform (an unparented entity only): its matrix in the view, and the capture is stale
        // once the entity gets a parent or its Transform changes, appears or goes.
        bool CaptureTransform{false};
    };

    struct PointEdit; // a capture, owned by the service's code

    struct PointEditView
    {
        std::span<const glm::vec3> Points{};
        std::span<const glm::vec3> PointNormals{};
        std::size_t SlotCount{0u}, LiveCount{0u};
        bool ValidLbvh{false};  // the points fit an LBVH (count and coordinates)
        bool IndexReused{false};
        bool HasZeroNormals{false}; // a live normal of length zero
        GeometryPropertyRef Output{}; // with its resolved domain
        std::span<const GeometryPropertyRef> ScalarOutputs{}; // with their resolved domains
        glm::dmat4 Model{1.0}; // with CaptureTransform: the local Transform's matrix (identity without a Transform)
    };

    // Neighbour rows within a radius, compact indices: point i's neighbours are Indices[Offsets[i], Offsets[i+1]),
    // ascending, self excluded.
    struct PointNeighbourRows
    {
        std::vector<std::uint32_t> Offsets{0u}, Indices{};
        std::size_t MaximumNeighbors{0u}; // the largest support found, before a lowest-ID limit
    };

    enum class PointRowsState : std::uint8_t { Pending, Ready, Failed };

    // Cumulative over a capture's GPU rows (ARCH-023: a hybrid method's per-pass traffic): host bytes written for the
    // queries and for the private moving workspace's points (its row map once, at creation), and the packed results
    // read back.
    struct PointGpuRowStats
    {
        std::size_t QueryBatches{0u}, WorkspaceBuilds{0u};
        double Milliseconds{0.0};
        std::uint64_t QueryUploadBytes{0u}, WorkspaceUploadBytes{0u}, ReadbackBytes{0u};
    };

    // ARCH-023: a capture's GPU neighbour rows computed by its run's worker step, with no frame, owner callback or
    // render thread (PointCloudEditingService::PrepareGpuRows on the main thread at the run's start, then the Compute
    // functions below on the worker, one call at a time). It holds the prepared device query sessions and the
    // capture's source rows only (no capture, ECS or cache state); dropping it (any thread) releases the sessions once
    // their last page completed.
    struct PointGpuRows;
    struct PointGpuRowsRequest
    {
        // kNN rows: either strategy; radius rows: Lbvh only (brute force is refused).
        Graphics::PointQueryStrategy Strategy{Graphics::PointQueryStrategy::Lbvh};
        std::uint32_t BatchSize{1u}; // queries per page
        // kNN rows: neighbours per point, self included (1..64). 0 selects radius rows.
        std::uint32_t Width{0u};
        // kNN: later passes query the moved points (a private target over them, rebuilt on the worker); without it
        // every pass queries the cached index with the captured points.
        bool Moving{false};
        // Radius rows: candidates kept per query, and a positive lowest-ID limit (0: the complete support).
        std::uint32_t Capacity{0u}, LowestIdLimit{0u};
    };
    // Worker: one pass of kNN rows (`width` compact indices per point of `points`, self included): the first pass
    // queries the cached index with the captured points, later passes (a Moving request) the moved `points`. Ready;
    // Failed with the reason; Pending when AbortGpuRows ended it first (the run ended; nothing to report).
    [[nodiscard]] PointRowsState ComputeGpuKnnRows(PointGpuRows& rows,
        std::span<const glm::vec3> points,
        std::vector<std::uint32_t>& out,
        std::string& diagnostic);
    // Worker: every radius row of the captured points (self excluded, lowest-ID limit as requested), as
    // CpuRadiusRows gives them. Ready, Failed with the reason, or Pending (aborted).
    [[nodiscard]] PointRowsState ComputeGpuRadiusRows(
        PointGpuRows& rows, float radius, PointNeighbourRows& out, std::string& diagnostic);
    // Any thread (the run's Discard): a waiting or later Compute returns Pending.
    void AbortGpuRows(PointGpuRows& rows) noexcept;
    // Any thread: the traffic so far (QueryBatches: pages).
    [[nodiscard]] PointGpuRowStats GpuRowStats(const PointGpuRows& rows) noexcept;

    // Unrepresentable: a value does not fit a scalar output's storage exactly (nothing is written).
    enum class PointPublishStatus : std::uint8_t { Applied, Stale, Rejected, Unrepresentable };
    // Busy: not now (no free ring slot, or another result holds the ring); a later call may show it.
    enum class PointPreviewStatus : std::uint8_t { Shown, Busy, Unavailable };

    // A point scalar method (one value per point into one scalar output), named as its definition names it: `Noun`
    // ("Radii") names the output, the target and the Jobs row ("<Noun> estimation"), `Method` ("Point spacing") its
    // sample requirement, `History` the undo entry of a GPU run. `Params` are the device kernel's.
    struct ScalarAnalysisRequest
    {
        std::uint32_t StableEntityId{0u};
        GeometryPropertyRef Positions{};
        GeometryPropertyRef Output{}; // any scalar kind on the CPU; the GPU writes float outputs only
        Graphics::PointScalarGpuParams Params{};
        std::string Noun{}, Method{}, History{};
        std::string Owner{"Runtime.GpuTransaction"}; // registered plugin name; runtime callers keep the default
        // Fewer live samples are refused with `TooFewSamples` (empty: "<Method> requires at least two live samples.").
        std::size_t MinimumLiveCount{2u};
        std::string TooFewSamples{};
    };
    // Where the method runs: the readiness differs (the CPU index and the device have limits).
    enum class ScalarAnalysisBackend : std::uint8_t { CpuReference, CpuIndex, Gpu };
    enum class ScalarAnalysisPhase : std::uint8_t { Running, ReadyToAccept, Accepting, Applied, Discarded, Failed };

    struct ScalarAnalysisState
    {
        ScalarAnalysisPhase Phase{ScalarAnalysisPhase::Failed};
        std::string Message{}, AcceptRefusal{};
        bool CanAccept{false};
        bool Stale{false};    // it ended because an input or the output changed
        bool Detached{false}; // its workspace detached: discarded, nothing published
        bool IndexReused{false};
        std::size_t SlotCount{0u}, LiveCount{0u};
        GeometryPropertyRef Output{}; // with its resolved domain
        std::uint64_t GpuInputUploadBytes{0u}, GpuInputCacheHits{0u}, CpuStageReadbackBytes{0u};
        std::uint32_t GpuQueryBatches{0u};
        Graphics::PointScalarGpuStats Statistics{};
        std::uint64_t StepRun{0u}; // the run's step run (run_control, run_observe)
    };

    // A refused start: why, and whether another run owns the output (then that run's result answers, not this one).
    struct ScalarAnalysisRefusal
    {
        std::string Message{};
        bool Busy{false};
        bool Failed{false}; // admitted, but its work was not submitted (a failure, not invalid input)
    };

    // One GPU run of a point scalar method (main thread). Its own Jobs run drives it, with the Accept stage joined
    // to it; the Jobs window's cancel ends it there. Move-only: destroying it discards a run that has not ended
    // (an Accept already publishing finishes), so the owner's lifetime bounds the preview. It also ends, Detached,
    // when its workspace detaches or the service stops using that workspace (UseEditorWorkspace, Release).
    class ScalarAnalysis
    {
    public:
        struct Run;
        ScalarAnalysis() noexcept;
        explicit ScalarAnalysis(std::shared_ptr<Run> run) noexcept;
        ScalarAnalysis(ScalarAnalysis&&) noexcept;
        ScalarAnalysis& operator=(ScalarAnalysis&&) noexcept;
        ~ScalarAnalysis();
        [[nodiscard]] explicit operator bool() const noexcept { return m_Run != nullptr; }

        // The run's state now. After its workspace detached it is discarded here (Detached).
        [[nodiscard]] ScalarAnalysisState Poll();
        // Publishes the preview into the output, as one undo entry, once its readback landed (Accepting until then);
        // refused with the reason while it is not ready or an input changed.
        ScalarAnalysisState Accept();
        // Releases the preview; the output and the history stay unchanged.
        void Discard();
        // The step-run service its one-shot step run belongs to (run id: Poll().StepRun); null without a run.
        [[nodiscard]] StepRunsService* StepRuns() const noexcept;

    private:
        std::shared_ptr<Run> m_Run; // the only owner; the service keeps a weak reference to end it
    };

    // The editor session a result belongs to: the workspace the service used and that workspace's attachment.
    struct PointSession
    {
        std::function<bool()> Active{};
        const void* Workspace{nullptr};
    };

    // A plugin window's picker over an InputCatalog (ImGui, the host's context): true when the choice changed `ref`;
    // a `ref` the catalog does not list stays as it is.
    bool PointPropertyCombo(const char* label, GeometryPropertyRef& ref, const GeometryPropertyCatalogSnapshot& catalog);
    // A plugin window's scalar output (ImGui): its name and storage (bool, int32, uint32, uint64, float, double);
    // true when either changed.
    bool PointScalarOutputInput(const char* label, GeometryPropertyRef& ref);
    // A plugin window's controls of a scalar run not yet ended (ImGui): the host's step-run row of a GPU run
    // (`analysis`) or of a CPU run (`stepRun`, its ScalarCpuRun's id), the actions MCP run_control issues: run or step
    // its one step, pause, stop, restart, Accept with one undo entry, Discard.
    void DrawScalarAnalysisControls(
        ScalarAnalysis& analysis, std::uint64_t stepRun, std::string_view idSuffix, StepRunControlsState& row);

    // A scalar plugin's CPU run (ARCH-023; kernel density, point spacing and density weights): a one-shot worker run
    // of the host's step runs (StartOneShotWorkerRun), so it takes the run controls and the execution options a GPU
    // run takes. Its one step computes the column (one value per captured point) on a worker; Accept publishes it
    // into the capture's scalar output with one undo entry, nothing before. Its holder job names `Output` (a second
    // run there is refused). It is valid while `Session` and the capture are current: an edit of an input or
    // the output, a detach or a workspace switch discards it.
    struct ScalarCpuRun
    {
        std::shared_ptr<PointEdit> Edit{};
        PointSession Session{};
        PluginJobOutput Output{};
        // The Jobs row ("Radii estimation"), the undo entry, the output's noun ("Radii") and the method in a sentence
        // ("point spacing").
        std::string Label{}, History{}, Noun{}, Method{};
        std::string Owner{"point_cloud_editing"}; // registered plugin name, independent of Label
        StepRunExecution Execution{}; // its auto_accept and start apply
        // Worker: the column into `values`; false when it failed (the plugin keeps the reason).
        std::function<bool(std::vector<float>& values)> Compute{};
        // Main thread, once, at the settled end (StepRunCallbacks::Finished; at Accept for a publication): the step has
        // returned and the holder released the output, so Compute's state may be read or freed and a new run on the
        // output is admitted (once Finish returns, for a publication). Status applied, stale, invalid, failed,
        // discarded or cancelled, with the host's reason (empty for applied and for a failed step, which the plugin
        // words). `ownResult`: the run's own step decided it (a publication's answer, or Compute failing), so
        // Compute's result describes it; otherwise it ended before (discard, cancel, stale, a refused step).
        std::function<void(std::string_view status, std::string message, bool ownResult)> Finish{};
    };

    class PointCloudEditingService
    {
    public:
        PointCloudEditingService();
        ~PointCloudEditingService();
        PointCloudEditingService(const PointCloudEditingService&) = delete;
        PointCloudEditingService& operator=(const PointCloudEditingService&) = delete;

        // Main thread. Null with a diagnostic when the entity, the references or the rows do not fit.
        [[nodiscard]] std::shared_ptr<PointEdit> Capture(const PointEditRequest& request, std::string& diagnostic);
        [[nodiscard]] PointEditView View(const PointEdit& edit) const;
        // Main thread: inputs and output unchanged since the capture.
        [[nodiscard]] bool Current(const PointEdit& edit);
        // The cached CPU LBVH over the captured points (with AcquireIndex), indices compact; any thread.
        [[nodiscard]] const Geometry::PointLBVH::Index* CpuIndex(const PointEdit& edit) const;
        // Main thread: the cached spatial index of a capture made without AcquireIndex, as AcquireIndex would have
        // acquired it, so a caller can check its own limits first; false with the reason.
        [[nodiscard]] bool AcquireIndex(PointEdit& edit, std::string& diagnostic);
        // Main thread: GPU neighbourhood queries can run (the spatial cache's device and the job service).
        [[nodiscard]] bool GpuQueriesAvailable();
        // Rows within `radius` from the cached CPU index (with AcquireIndex and CpuIndex), any thread. A positive
        // `lowestIdLimit` keeps a point's lowest-ID neighbours up to that many; 0 requires the complete support. False
        // with a diagnostic when a support is incomplete. GPU rows: PrepareGpuRows, then ComputeGpuRadiusRows.
        [[nodiscard]] bool CpuRadiusRows(const PointEdit& edit, float radius, std::uint32_t lowestIdLimit,
                                         PointNeighbourRows& rows, std::string& diagnostic) const;
        // Main thread, a run's start (ARCH-023): the worker rows of a capture made with AcquireIndex (see
        // PointGpuRows), under the device batches' admission. Null with the reason.
        [[nodiscard]] std::shared_ptr<PointGpuRows> PrepareGpuRows(
            PointEdit& edit, const PointGpuRowsRequest& request, std::string& diagnostic);
        // Main thread: writes `live` (one position per captured point) into the output for the live slots, records
        // one undo entry named `label` and marks the entity dirty. Stale when an input or the output changed;
        // Rejected (nothing written) for a non-finite position.
        [[nodiscard]] PointPublishStatus Publish(PointEdit& edit, std::span<const glm::vec3> live, std::string label);
        // Main thread, an in-place output (the positions): shows `live` (one position per captured point) as the
        // positions' ring front, which the renderer draws, without changing the positions or the history. Busy or
        // Unavailable (no operational device, not in place, inputs changed) with the reason otherwise. The capture
        // keeps its ring until DiscardPreview or Publish (which releases it first); previewBuffers must be 2 or 3.
        [[nodiscard]] PointPreviewStatus PreviewPositions(
            PointEdit& edit, std::span<const glm::vec3> live, std::string& diagnostic, std::uint32_t previewBuffers);
        // Main thread: releases the capture's preview ring (only that ring; nothing without one).
        void DiscardPreview(PointEdit& edit);
        // Main thread: writes one column per scalar output (one value per captured point) for the live slots, all or
        // none, as one undo entry named `label`. Deleted slots keep their values.
        [[nodiscard]] PointPublishStatus PublishScalars(PointEdit& edit, std::span<const std::vector<float>> columns,
                                                        std::string label);
        // Main thread: starts `run` (a ScalarCpuRun) on the step runs of its capture's session, its jobs submitted
        // through `jobs` (a plugin: PluginStepRunJobs(holder), whose holder becomes the plugin run's Handle); its step
        // run's id, or 0 with the reason.
        [[nodiscard]] std::uint64_t StartScalarCpuRun(ScalarCpuRun run, StepRunJobs jobs, std::string& refusal);
        // Main thread, side-effect free: empty when the method can run on `backend`, else why (the target, the
        // capture, fewer than MinimumLiveCount live samples, the LBVH limits; on the GPU its admission: a float output, normal
        // coordinates and parameters, k <= 63 for methods 0 and 1, row and workspace limits, an operational float64
        // device with the framed spatial cache and jobs).
        // `current` checks a fresh copy of the values, as a start does, instead of the editor's cached verdict (which
        // may still be pending).
        [[nodiscard]] std::string ScalarAnalysisReadiness(const ScalarAnalysisRequest& request,
                                                          ScalarAnalysisBackend backend, bool current = false);
        // Main thread: the GPU run after the same checks, a one-shot step run (kOneShotStepRunExecution): of
        // `execution` only its start (ready: nothing computed before run or step n) and automatic Accept (any but
        // never: as soon as the one step ended, unless stopped) apply. The output shows its preview until Accept or
        // Discard. Empty with the reason in `refusal` otherwise.
        [[nodiscard]] ScalarAnalysis StartScalarAnalysis(
            const ScalarAnalysisRequest& request, const StepRunExecution& execution, ScalarAnalysisRefusal& refusal);
        // A run over given values (one per slot) on a test residency, captured as a CPU run is; no device work.
        [[nodiscard]] ScalarAnalysis StartScalarAnalysisForTest(const ScalarAnalysisRequest& request,
                                                                std::vector<float> values,
                                                                Graphics::GpuPropertyResidency& residency,
                                                                ScalarAnalysisRefusal& refusal);
        // Main thread: the session results are kept for; current while that workspace is still the one used and
        // still attached (a detach and reattach of the same workspace makes a new session).
        [[nodiscard]] PointSession CurrentSession();
        [[nodiscard]] bool SessionCurrent(const PointSession& session) const;
        // Main thread: shows a property of the entity in the viewport (the processing panels' "Show"); false with
        // the reason when the editor refuses.
        [[nodiscard]] bool ShowProperty(std::uint32_t stableEntityId, const GeometryPropertyRef& property,
                                        std::string& diagnostic);
        // The entity's vec3 point properties with at least `minimumLiveCount` live rows, for pickers; with `domain`,
        // only that domain's (a normals picker offers the positions' domain).
        [[nodiscard]] GeometryPropertyCatalogSnapshot InputCatalog(
            std::uint32_t stableEntityId, std::size_t minimumLiveCount,
            std::optional<GeometryElementDomain> domain = std::nullopt);
        // The app's editor workspace (the Sandbox shell's), so edits share its history guards and session.
        void UseEditorWorkspace(EditorWorkspaceAttachment& workspace) noexcept;
        // Before that workspace detaches: back to the service's own.
        void ReleaseEditorWorkspace(const EditorWorkspaceAttachment& workspace) noexcept;

        struct Impl;

    private:
        std::unique_ptr<Impl> m_Impl;
        friend class PointCloudEditingModule;
    };

    // A scalar plugin's GPU run in its run book (ARCH-022 Q90; shared by kernel density, point spacing and density
    // weights): `Run` is a PluginRun with the service's run (`Analysis`) and its editor session (`Session`), its Result
    // has Status, Message, LiveCount and WrittenCount, and `fold` copies a GPU state into it (texts, counts,
    // statistics). Main thread. Refresh folds the state into Pending, and into Final once the run ended.
    template <class Run, class Fold>
    void RefreshScalarPluginRun(PluginRunBook<Run>& book, Run& run, Fold&& fold)
    {
        if (run.Final || !run.Analysis) return;
        const auto state = run.Analysis.Poll();
        auto r = run.Pending;
        fold(r, state);
        if constexpr (requires { r.StepRun; })
            r.StepRun = state.StepRun;
        switch (state.Phase)
        {
        case ScalarAnalysisPhase::Running:
        case ScalarAnalysisPhase::ReadyToAccept:
        case ScalarAnalysisPhase::Accepting: run.Pending = std::move(r); return;
        case ScalarAnalysisPhase::Applied:
            r.Status = "applied";
            r.WrittenCount = r.LiveCount;
            break;
        case ScalarAnalysisPhase::Discarded: r.Status = state.Stale ? "stale" : "discarded"; break;
        case ScalarAnalysisPhase::Failed: r.Status = "failed"; break;
        }
        book.Finish(run, std::move(r));
    }
    // Publishes a ready preview (the plugin's own Accept, as run_control's Accept does); a refused Accept leaves its
    // reason in Pending.
    template <class Run, class Fold>
    void AcceptScalarPluginRun(PluginRunBook<Run>& book, Run& run, Fold&& fold)
    {
        if (run.Final || !run.Analysis) return;
        const auto state = run.Analysis.Accept();
        if (state.Phase == ScalarAnalysisPhase::ReadyToAccept) run.Pending.Message = state.Message; // why not
        RefreshScalarPluginRun(book, run, fold);
    }
    // Discards a GPU run's preview, or cancels a CPU run's step run (its holder job); false when nothing was active.
    template <class Run, class Fold>
    bool CancelScalarPluginRun(PluginRunBook<Run>& book, Run& run, Fold&& fold)
    {
        if (run.Final) return false;
        if (!run.Analysis) return CancelPluginRun(run);
        run.Analysis.Discard();
        RefreshScalarPluginRun(book, run, fold);
        return true;
    }
    // A scalar plugin CPU run's end (its ScalarCpuRun::Finish): `r` (the plugin's result) takes the status, the
    // host's reason unless empty and the run's id, the Jobs row its outcome, and becomes the run's Final.
    template <class Run, class Result>
    void FinishScalarCpuPluginRun(
        PluginRunBook<Run>& book, Run& run, Result r, const std::string_view status, std::string message)
    {
        if (run.Final)
            return;
        r.Status = std::string{status};
        if (!message.empty())
            r.Message = std::move(message);
        r.StepRun = run.Pending.StepRun;
        if (run.Handle.IsValid())
            ReportPluginJobOutcome(run.Handle, PluginJobDomain::Cpu, r.Message);
        book.Finish(run, std::move(r));
    }
    // A scalar plugin tool's input schema: the one-shot execution arguments auto_accept and start.
    [[nodiscard]] inline std::string ScalarToolSchemaJson()
    {
        return R"({"type":"object","properties":{)" + StepRunExecutionSchemaProperties(1u) +
            R"(},"additionalProperties":false})";
    }

    // A scalar plugin tool's run (MCP), on every backend. Without execution arguments it is a batch
    // (kOneShotBatchExecution) that waits for the run's end (WaitForPluginRun). With auto_accept or start (its schema:
    // StepRunExecutionSchemaProperties(1)) the arguments layer over the section's own options (`section`), as every
    // agent start does; a result that does not compute at once and accept at its end answers at once with the run's
    // state, whose "run" run_control and run_observe drive. `start(execution)` starts the plugin's run with the
    // section's other settings and these options; `text` renders a Result; `refresh`/`cancel` take the run.
    template <class Start, class Text, class Refresh, class Cancel>
    [[nodiscard]] AgentOperationOutcome RunScalarPluginTool(const std::string_view arguments,
        const StepRunExecution& section,
        Start start,
        Text text,
        Refresh refresh,
        Cancel cancel)
    {
        StepRunExecution execution =
            NamesStepRunExecutionArguments(arguments, 1u) ? OneShotExecution(section) : kOneShotBatchExecution;
        std::string refusal;
        if (!ReadStepRunExecutionArguments(arguments, 1u, execution, refusal))
            return {.IsError = true, .Text = refusal, .ErrorCode = "invalid_params"};
        const bool batch = execution.Start == StepRunStart::Run && execution.AutoAccept != StepRunAutoAccept::Never;
        const auto run = start(execution);
        using Result = std::remove_cvref_t<decltype(run->Pending)>;
        if (batch)
            return WaitForPluginRun(
                std::shared_ptr<PluginRun<Result>>(run),
                text,
                [run, refresh]
                {
                    refresh(*run);
                },
                [run, cancel]
                {
                    return cancel(*run);
                });
        refresh(*run);
        const Result& result = run->Final ? *run->Final : run->Pending;
        const bool failed = run->Final && run->Final->Status != "applied";
        return {.IsError = failed, .Text = text(result), .ErrorCode = failed ? run->Final->Status : std::string{}};
    }

    // The book's latest run while the service still works in its editor session; one of an earlier session is gone.
    template <class Run>
    [[nodiscard]] std::shared_ptr<Run> LatestScalarPluginRun(PluginRunBook<Run>& book)
    {
        return book.Latest([](const Run& run)
        {
            auto* service = PluginService<PointCloudEditingService>();
            return service != nullptr && service->SessionCurrent(run.Session);
        });
    }

    class PointCloudEditingModule final : public IRuntimeModule
    {
    public:
        PointCloudEditingModule();
        ~PointCloudEditingModule() override;
        // After "Runtime.SceneDocumentModule" in name order, so it shuts down (and detaches its own workspace)
        // before the document it observes.
        [[nodiscard]] std::string_view Name() const noexcept override { return "Runtime.Services.PointCloudEditing"; }
        [[nodiscard]] RuntimeModuleResult OnRegister(EngineSetup& setup) override;
        [[nodiscard]] RuntimeModuleResult OnResolve(EngineSetup& setup) override;
        void OnShutdown(RuntimeModuleShutdownContext& context) override;

    private:
        PointCloudEditingService m_Service{};
    };
}

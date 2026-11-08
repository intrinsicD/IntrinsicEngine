module;

#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <bit>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include <glm/glm.hpp>
#include <entt/entity/entity.hpp>
#include "Modules/Clustering/Runtime.KMeansPaging.TestSupport.hpp"
module Extrinsic.Runtime.ClusteringModule;
import Geometry.Validation;
import :GpuBackend;
import Geometry.KMeans;
import Extrinsic.Runtime.EditorProcessing;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.PointScalarTransaction;
import Extrinsic.Runtime.StepRuns;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.GpuPropertyBinding;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.TransferQueue;
import Extrinsic.RHI.Types;
#include "Services/Runtime.WorkerGpuPages.hpp"
#include "Modules/Clustering/Runtime.ClusteringGpuState.Internal.hpp"
namespace Extrinsic::Runtime
{
    struct ClusteringGpuState::Impl
    {
        struct Operation
        {
            explicit Operation(RHI::IDevice& device) : Pages(device) {}
            KMeansSnapshot Snapshot{};
            EditorProcessingContext Context{};
            EditorProcessingCommands Commands{};
            EditorPointScalarTransactionHandle Transaction{};
            std::shared_ptr<KMeansGpuWorkspace> Workspace{};
            std::optional<Graphics::GpuPropertyView> Input{}, Base{};
            std::optional<EditorPointScalarBack> Back{}; // the terminal preview's slot (owner)
            std::shared_ptr<SpatialGpuResult> Page{};    // an owner page (the terminal labels) in flight
            std::function<bool()> Current{};
            std::function<EditorCommandStatus(const Geometry::KMeans::KMeansResult&)> Publish{};
            Geometry::KMeans::KMeansResult Geometry{};
            std::vector<glm::vec3> Seeds{}; // the initial centroids, kept for a restart (cluster count x 12 bytes)
            KMeansRunCompleted Result{};
            KMeansGpuPhase Phase{KMeansGpuPhase::Initialize};
            KMeansPagingLimits Limits{KMeansPagingForTesting};
            std::uint32_t Row{}, Inner{}, Count{}, Clusters{}, PreviewDeferrals{};
            bool Boundary{}, Presented{}, Terminal{}, FinalAssignment{}, Ready{}, Accepting{}, Discard{}, Recorded{};
            // ARCH-023: the iterations run as the step run's worker Step (WorkerStep), each waiting for its own
            // pages; the owner (main thread) keeps live ECS, residency, publication, the terminal labels and every
            // release. While a worker segment runs, only the worker touches the page planner (Phase, Row, Inner,
            // Boundary, Presented, Moved, PreviewOnly) and the iteration results (Result.Inertia/Converged/
            // MaxDistanceIndex, Geometry); the owner reads them once no segment is in flight.
            RHI::ITransferQueue* Transfer{}; // captured at Start: a reseed waits and uploads, the owner polls fences
            // The worker's pages and preview copies (the owner's preview at a rest submits here too; never both at
            // once; ARCH-023 B2): the first accepted one waits for every graphics-lane transfer issued before it (the
            // Start uploads of input, seeds and slot map; a reseed's), later ones follow the compute lane alone, so a
            // preview or renderer upload issued meanwhile does not hold them. A discard, a stale source or shutdown
            // aborts them (a waiting worker step gives up); a finished run seals them before its slots are stamped.
            WorkerGpuPages Pages;
            std::atomic<std::uint32_t> Iterations{};
            std::uint32_t OwnerSubmissions{}; // the owner's terminal pages (the worker's pages and copies: Pages)
            std::uint32_t FoldedIterations{}, FoldedSubmissions{}; // what the module's stats already counted
            std::uint64_t FoldedReadbackBytes{};
            std::string WorkerDiagnostic{}; // why the worker's step failed (read once it returned)
            std::atomic<bool> WorkerFailed{};
            // A worker page that was recorded but whose submission was refused: like a failed recorded owner page,
            // its workspace stays retired until the device-idle shutdown.
            bool RecordedFailure{};
            // A restart re-uploads the initial centroids on the worker, after the older pages completed.
            bool ReseedPending{};
            // A preview copy's pages alone: the planner stops after its Presentation instead of fusing the next
            // Assign (the owner publishes the copy once its fence completed).
            bool PreviewOnly{};
            bool PreviewFailed{}; // a completed preview could not be published (the owner ends the run)
            StepPreviewMailbox<EditorPointScalarBack> Mailbox{};
            // An end decided while a worker segment runs: applied once no segment is in flight.
            std::optional<std::pair<KMeansRunStatus, std::string>> Ending{};
            // The step run: its controls decide each next iteration. `Moved`: the last update moved a centroid, so
            // labels for those centroids need a final assignment. `CommitRequested`: Accept (the run's Commit) is
            // preparing and reading labels.
            StepRunsService* Steps{};
            std::uint64_t Run{};
            StepRunDriver Driver{};
            JobToken Holder{}; // the step run's first editor job: its run's token
            bool Moved{}, CommitRequested{};
        };
        RHI::IDevice& Device;
        StepRunsService* StepRuns{};
        std::shared_ptr<bool> Alive{std::make_shared<bool>(true)};
        std::shared_ptr<Operation> Active{};
        // Recorder failures can follow recorded commands. Retain their workspaces and
        // leases until the participant's device-idle shutdown.
        std::vector<std::shared_ptr<Operation>> Retired{};
        // Finished runs whose last page is still in flight (a discard releases what a run shows at once): their
        // workspace returns, and their step run retires, once the page completed.
        std::vector<std::shared_ptr<Operation>> Settling{};
        // One run at a time: the last run's pipelines, kept only when no submission recorded with
        // them can still execute. Its buffers went back to the method scratch cache when parked.
        std::shared_ptr<KMeansGpuWorkspace> Idle{};
        void Park(std::shared_ptr<KMeansGpuWorkspace> workspace)
        {
            workspace->ReleaseScratch();
            Idle = std::move(workspace);
        }
        std::optional<ClusteringGpuResult> Completed{};
        ClusteringModuleStats& Stats;
        Impl(RHI::IDevice& d, ClusteringModuleStats& stats) : Device(d), Stats(stats) {}
        ~Impl()
        {
            *Alive = false;
            // Every step run of this state ends now and drops its callables (they capture this state), also with a
            // page still in flight (its recorder no longer records, Alive).
            for (const auto& settling : Settling)
                settling->Driver.Abandon();
            if (Active)
            {
                Active->Pages.Seal(); // a waiting worker step returns; nothing more is submitted
                Active->Driver.Abandon();
                DiscardEditorPointScalar(Active->Commands, Active->Transaction);
            }
        }
        // A worker segment of the run may be running (its Step touches the planner and the workspace).
        [[nodiscard]] static bool WorkerBusy(const Operation& a)
        {
            const auto run = a.Steps->Observe(a.Run);
            return run && run->WorkerInFlight;
        }
        // The worker's counts, folded into the result and the module's stats (owner).
        void Fold(Operation& a)
        {
            const std::uint32_t iterations = a.Iterations.load(std::memory_order_acquire);
            if (iterations > a.FoldedIterations)
                Stats.GpuIterations += iterations - a.FoldedIterations;
            a.FoldedIterations = iterations;
            a.Result.Iterations = iterations;
            const std::uint32_t submissions = a.Pages.Submitted() + a.OwnerSubmissions;
            Stats.GpuSubmissions += submissions - a.FoldedSubmissions;
            a.FoldedSubmissions = submissions;
            a.Result.GpuSubmissions = submissions;
            const std::uint64_t bytes = a.Pages.ReadbackBytes();
            a.Result.CpuStageReadbackBytes += bytes - a.FoldedReadbackBytes;
            a.FoldedReadbackBytes = bytes;
        }
        // Residency slots the worker read or wrote, stamped with every compute submission so far before their views
        // (and leases) go: none is reused or freed before that work completed.
        void StampAndRelease(Operation& a, std::optional<EditorPointScalarBack> back = {})
        {
            auto* residency = a.Context.SpatialIndices ? a.Context.SpatialIndices->PropertyResidency() : nullptr;
            if (residency != nullptr)
            {
                const auto frame = Device.GetGlobalFrameNumber();
                residency->BeginSubmission();
                for (const auto* view : {a.Input ? &*a.Input : nullptr, a.Base ? &*a.Base : nullptr})
                    if (view != nullptr)
                        residency->NoteUse(view->Buffer, frame);
                if (back)
                    for (const auto& view : {back->Typed, back->Presentation, back->Companion})
                        if (view.Buffer.IsValid())
                            residency->NoteUse(view.Buffer, frame);
                residency->EndSubmission(Device.GetTransferQueue().ComputeFence());
            }
            back.reset();
        }
        // Empties the preview mailbox (owner, no worker step running), stamping its slot first.
        void ClearMailbox(Operation& a)
        {
            if (auto slot = a.Mailbox.Clear())
                StampAndRelease(a, std::move(slot->first));
        }
        void Finish(KMeansRunStatus status, std::string why)
        {
            if (!Active)
                return;
            // A worker step may still record: the run ends now (its step run is told and the worker gives up its
            // wait), its resources once the segment returned (AdvanceActive applies the end).
            if (WorkerBusy(*Active))
            {
                if (!Active->Ending)
                    Active->Ending.emplace(status, why);
                Active->Pages.Abort();
                Active->Result.Message = std::move(why);
                Tell(*Active, status);
                return;
            }
            auto a = std::move(Active);
            a->Pages.Seal(); // no worker page or copy follows: the compute fence the stamps take covers them all
            Fold(*a);
            ClearMailbox(*a);
            a->Result.Status = status;
            a->Result.Message = std::move(why);
            a->Result.Error =
                status == KMeansRunStatus::Applied ? Core::ErrorCode::Success : Core::ErrorCode::InvalidState;
            DiscardEditorPointScalar(a->Commands, a->Transaction);
            Tell(*a, status);
            StampAndRelease(*a);
            a->Input.reset();
            a->Base.reset();
            if (!Settle(a))
            {
                // An abandoned worker page is still on the device: the step run retires once it completed.
                a->Driver.Recording(true);
                Settling.push_back(a);
            }
            Completed = ClusteringGpuResult{.Published = std::move(a->Result)};
        }
        // A finished run's last page completed (or none was in flight): its workspace is free again, a failed
        // recorded one stays retired, and its step run may retire. False while a page (the owner's, or the worker's
        // last page or preview copy) is in flight: a reused workspace must not be written before it completed.
        bool Settle(const std::shared_ptr<Operation>& a)
        {
            if (a->Page && a->Page->State != SpatialQueryState::Ready && a->Page->State != SpatialQueryState::Failed)
                return false;
            if (!a->Transfer->IsComplete(a->Pages.Last()))
                return false;
            if (a->RecordedFailure || (a->Recorded && a->Page && a->Page->State == SpatialQueryState::Failed))
                Retired.push_back(a);
            else if (a->Workspace && !Idle)
                Park(std::move(a->Workspace));
            a->Driver.Recording(false);
            return true;
        }
        // The run's end as its step run sees it: a publication answers its pending Commit; a cancel or a stale source
        // discards it; a failure fails its Commit or its iteration. Ignored by a step run that ended already.
        void Tell(Operation& a, const KMeansRunStatus status)
        {
            std::string refusal;
            if (status == KMeansRunStatus::Applied)
                a.Driver.CommitFinished(true);
            else if (a.CommitRequested && status != KMeansRunStatus::Cancelled)
                a.Driver.CommitFinished(false, a.Result.Message);
            else if (status == KMeansRunStatus::GeometryProcessingFailed)
                (void)a.Driver.Stepped({}, StepOutcome::Failed);
            else
                (void)a.Steps->Control(a.Run, StepRunAction::Discard, 1u, refusal);
            // A run whose holder ended with its Commit pending has no pump: an answer settles it (ignored otherwise).
            if (status != KMeansRunStatus::Applied)
                a.Driver.CommitFinished(false, a.Result.Message);
        }
        // Stop, the iteration cap or convergence ended the step run (it is not discarded).
        [[nodiscard]] static bool Ended(const Operation& a)
        {
            const auto run = a.Steps->Observe(a.Run);
            return run && run->End != StepRunEnd::None && !IsTerminal(run->Phase);
        }
        // The labels of the current centroids: a final assignment when the last update moved one, then the terminal
        // preview, which reads the centroids back and makes the run ready.
        static void BeginFinal(Operation& a)
        {
            a.Terminal = true;
            a.FinalAssignment = a.Moved;
            a.Phase = a.Moved ? KMeansGpuPhase::Assign : KMeansGpuPhase::Preview;
            a.Row = a.Inner = 0;
        }
        bool Settled() const noexcept { return !Active && !Completed && Settling.empty(); }
        bool Supersede(const RunKMeans& request, const WorldHandle world)
        {
            const auto a = Active;
            if (!a)
                return !Settled();
            const auto& output = a->Snapshot.Command.Properties.OutputLabels;
            if (a->Snapshot.World != world || a->Snapshot.Command.StableEntityId != request.StableEntityId ||
                output.Domain != request.Properties.OutputLabels.Domain || output.Name != request.Properties.OutputLabels.Name)
                return false;
            std::string refusal;
            if (!a->CommitRequested)
                (void)a->Steps->Control(a->Run, StepRunAction::Discard, 1u, refusal); // its pump discards the run
            return true;
        }
        std::string ReadinessDiagnostic(const RunKMeans& request, const EditorProcessingContext& context) const
        {
            if (Active || Completed)
                return "A K-Means result awaits completion or Accept or Discard.";
            // These requests fall back before retained resources matter in Start.
            if (!Device.IsOperational() || !Device.SupportsShaderFloat64() || !context.SpatialIndices ||
                !context.SpatialIndices->GpuQueriesAvailable() || !context.SpatialIndices->PropertyResidency() ||
                request.Properties.OutputLabels.ValueKind != Geometry::PropertyValueKind::UInt32)
                return {};
            if (!Retired.empty())
                return "A failed recording retains GPU resources until device shutdown.";
            return {};
        }
        ClusteringGpuSubmission Start(KMeansSnapshot& snapshot,
            const EditorProcessingContext& context,
            std::function<bool()> current,
            std::function<EditorCommandStatus(const Geometry::KMeans::KMeansResult&)> publish)
        {
            if (auto reason = ReadinessDiagnostic(snapshot.Command, context); !reason.empty())
                return {.Refused = true, .Diagnostic = std::move(reason)};
            if (!Device.IsOperational() || !Device.SupportsShaderFloat64() || !context.SpatialIndices ||
                !context.SpatialIndices->GpuQueriesAvailable() || !context.SpatialIndices->PropertyResidency())
                return {
                    .Diagnostic = "K-Means requires an operational float64 device and SpatialIndexCache residency."};
            if (snapshot.Command.Properties.OutputLabels.ValueKind != Geometry::PropertyValueKind::UInt32)
                return {
                    .Diagnostic =
                        "GPU K-Means requires UInt32 label storage; the CPU reference preserves the configured scalar type."};
            if (StepRuns == nullptr)
                return {.Diagnostic = "K-Means step runs are unavailable."};
            auto& steps = *StepRuns;
            if (!snapshot.Input || snapshot.Input->Points.empty() ||
                snapshot.Input->Points.size() > std::numeric_limits<std::uint32_t>::max() ||
                snapshot.Input->SlotCount > std::numeric_limits<std::uint32_t>::max())
                return {.Refused = true, .Diagnostic = "K-Means row count is not representable by the device."};
            auto a = std::make_shared<Operation>(Device);
            a->Context = context;
            a->Commands = BindEditorProcessingCommands(context);
            a->Current = std::move(current);
            a->Publish = std::move(publish);
            a->Count = std::uint32_t(snapshot.Input->Points.size());
            a->Clusters = std::min(a->Count, snapshot.Params.ClusterCount);
            a->Limits.SubmissionPairs = std::max(1u, a->Limits.SubmissionPairs);
            a->Limits.PagePairs = std::clamp(a->Limits.PagePairs, 1u, a->Limits.SubmissionPairs);
            auto& r = *context.SpatialIndices->PropertyResidency();
            const auto before = r.Stats();
            const auto entity = SelectionController::ToEntityHandle(snapshot.Command.StableEntityId);
            {
                const ScopedMicros timer{&Stats.GpuInputResolveMicros};
                a->Input = ResolveGpuPropertyInput(
                    r, *context.Scene, context.World, entity, snapshot.Command.Properties.InputPositions);
            }
            if (!a->Input)
                return {.Refused = true, .Diagnostic = "K-Means resident input is busy or unavailable."};
            std::vector<glm::vec3> seeds = std::move(snapshot.Seeds);
            if (seeds.empty())
            {
                const ScopedMicros timer{&Stats.GpuSeedingMicros};
                seeds = Geometry::KMeans::BuildInitialCentroids(snapshot.Input->Points, {}, snapshot.Params, a->Clusters);
            }
            if (seeds.size() != a->Clusters)
                return {.Diagnostic = "K-Means GPU workspace or seed preparation failed."};
            a->Seeds = seeds;
            // A failed Prepare may leave partial state; that workspace is dropped.
            const bool identity = snapshot.Input->SlotCount == a->Count;
            const auto refusals = context.SpatialIndices->Stats().ScratchRefusals;
            bool prepared = false;
            {
                const ScopedMicros timer{&Stats.GpuWorkspacePrepareMicros};
                auto* scratch = context.SpatialIndices->MethodScratch();
                if (!Idle && !scratch)
                    return {.Refused = true, .Diagnostic = "K-Means method scratch buffers are unavailable."};
                a->Workspace = Idle ? std::move(Idle) : std::make_shared<KMeansGpuWorkspace>(Device, *scratch);
                prepared = a->Workspace->Prepare(
                    a->Count, seeds, identity ? std::span<const std::uint32_t>{} : snapshot.Input->Slots);
            }
            if (!prepared)
            {
                // A budget refusal fails the request with its byte counts instead of falling back to the CPU.
                const auto diagnostic = context.SpatialIndices->MethodScratchDiagnostic(
                    refusals, "K-Means GPU workspace or seed preparation failed.");
                return {
                    .Refused = context.SpatialIndices->Stats().ScratchRefusals != refusals, .Diagnostic = diagnostic};
            }
            const auto refuse = [&](std::string why) -> ClusteringGpuSubmission
            {
                Park(std::move(a->Workspace));
                return {.Refused = true, .Diagnostic = std::move(why)};
            };
            a->Result.CpuStageUploadBytes = a->Workspace->CpuUploadBytes();
            a->Result.GpuWorkspaceBuffersCreated = a->Workspace->PreparedBufferCreations();
            a->Result.GpuWorkspacePipelinesCreated = a->Workspace->PreparedPipelineCreations();
            auto typed = snapshot.Command.Properties.OutputLabels;
            typed.ValueKind = Geometry::PropertyValueKind::UInt32;
            // Existing integer bytes preserve deleted slots in every preview and Accept.
            if (!identity && snapshot.BeforeOutputs.Labels.Exists)
            {
                {
                    const ScopedMicros timer{&Stats.GpuInputResolveMicros};
                    a->Base = ResolveGpuPropertyInput(r, *context.Scene, context.World, entity, typed);
                }
                if (!a->Base)
                    return refuse("Prior K-Means labels are busy or unavailable for deleted-slot preservation.");
            }
            a->Result.GpuInputUploadBytes = r.Stats().UploadBytes - before.UploadBytes;
            a->Result.GpuInputCacheHits = r.Stats().Hits - before.Hits;
            EditorPointScalarTransactionSnapshot admission;
            std::weak_ptr<Operation> weak = a;
            a->Transaction = BeginEditorPointScalarPublication(a->Commands,
                {.EntityId = snapshot.Command.StableEntityId,
                    .Count = std::uint32_t(snapshot.Input->SlotCount),
                    .PreviewBuffers = snapshot.Command.Parameters.Execution.PreviewBuffers,
                    .Output = typed,
                    .Label = "Run K-Means clustering",
                    .Current = a->Current,
                    .Publish =
                        [weak, stats = &Stats](std::span<const std::byte> bytes)
                    {
                        auto op = weak.lock();
                        if (!op)
                            return EditorCommandStatus::StaleEntity;
                        // A discard or cancel that won first leaves nothing to publish; from here the publication
                        // decides the step run's end.
                        if (!op->Driver.Publishing())
                            return EditorCommandStatus::StaleEntity;
                        // The publication's time: the label decode and CommitKMeansOutputs.
                        const ScopedMicros timer{&stats->GpuAcceptPublishMicros};
                        op->Geometry.Labels.resize(op->Count);
                        for (std::uint32_t i = 0; i < op->Count; ++i)
                        {
                            std::uint32_t label{};
                            std::memcpy(&label, bytes.data() + std::size_t(op->Snapshot.Input->Slots[i]) * 4, 4);
                            if (label >= op->Clusters)
                                return EditorCommandStatus::GeometryProcessingFailed;
                            op->Geometry.Labels[i] = label;
                        }
                        return op->Publish(op->Geometry);
                    }},
                admission,
                [this, alive = Alive, weak](EditorPointScalarTransactionSnapshot result)
                {
                    const auto op = weak.lock();
                    if (!*alive || !op || Active != op || op->Discard)
                        return;
                    op->Result.CpuStageReadbackBytes += result.CpuStageReadbackBytes;
                    // An unpublished end is "cancelled or stale"; a cancel accepted for one of the run's jobs (its
                    // Accept stage alone, say) makes it a cancel, as the agent lane relabels runs.
                    const bool cancelled = result.Phase == EditorGpuTransactionPhase::Discarded &&
                        (result.Status != EditorCommandStatus::StaleEntity ||
                            IsEditorRunCancelRequested(op->Commands, op->Holder));
                    Finish(cancelled                                               ? KMeansRunStatus::Cancelled
                            : result.Status == EditorCommandStatus::StaleEntity    ? KMeansRunStatus::StaleSource
                            : result.Status == EditorCommandStatus::Applied ||
                                result.Status == EditorCommandStatus::NoChange
                            ? KMeansRunStatus::Applied
                            : KMeansRunStatus::GeometryProcessingFailed,
                        result.Message);
                });
            if (!a->Transaction)
                return refuse(admission.Message);
            // The step run: its holder is the run's first editor job, the Accept stage joins it.
            const auto& command = snapshot.Command;
            EditorJobIdentity jobIdentity{.EntityId = command.StableEntityId,
                .Scope = ToEditorJobScope(command.Properties.OutputLabels.Domain),
                .OutputSemantic = GeometryPresentationSlotSemantic::ScalarField,
                .OutputName = command.Properties.OutputLabels.Name,
                .RequestedDomain = EditorJobDomain::GpuCompute};
            const auto holder = std::make_shared<JobToken>();
            std::string refusal;
            a->Steps = &steps;
            a->Transfer = &Device.GetTransferQueue();
            a->Run = steps.Start(
                {.Label = "K-Means",
                    .Owner = "Runtime.ClusteringModule",
                    .Output = {.StableEntityId = command.StableEntityId, .Property = command.Properties.OutputLabels},
                    // ARCH-023: each step is one Lloyd iteration on a worker, its pages waited for there (no frame
                    // paces it); previews PreviewEvery iterations apart when the owner offered a free slot (0: no
                    // live preview; the terminal labels are the result, not a preview). Start: ready or running.
                    .Execution = command.Parameters.Execution,
                    .DevicePreview = true,
                    .MetricNames = {"inertia", "max_shift_squared", "", ""}},
                {.Step =
                        [a](StepRunMetric& metric)
                    {
                        return WorkerStep(*a, metric);
                    },
                    .Preview =
                        [a](StepPreview& preview)
                    {
                        return WorkerPreview(*a, preview);
                    },
                    .Show =
                        [this, alive = Alive, weak](const StepPreview& preview)
                    {
                        const auto op = weak.lock();
                        return *alive && op && Active == op ? Show(*op, preview) : StepRunShow::Never;
                    },
                    .Commit =
                        [this, alive = Alive, weak](std::string& diagnostic)
                    {
                        const auto op = weak.lock();
                        if (!*alive || !op || Active != op)
                        {
                            diagnostic = "The K-Means run ended.";
                            return StepRunCommit::Refused;
                        }
                        // The next Advance prepares the labels, then reads them back (not from here: an end it decides
                        // would answer this Commit while it runs).
                        op->CommitRequested = true;
                        return StepRunCommit::Pending;
                    },
                    .Discard =
                        [this, alive = Alive, weak]
                    {
                        const auto op = weak.lock();
                        if (!*alive || !op || Active != op)
                            return;
                        // What the run shows goes at once (its rings are freed fence-safely), also with a page or a
                        // worker step in flight; the run finishes now or once that work completed.
                        op->Discard = true;
                        op->Pages.Abort();
                        op->Driver.Recording(true); // the owner retires the last device page after the worker exits
                        DiscardEditorPointScalar(op->Commands, op->Transaction);
                        Advance();
                    },
                    // run_control restart, once nothing is in flight: the initial centroids again (uploaded by the
                    // next step, after the older pages completed) and the first iteration next; the previews already
                    // shown stay until the new computation replaces them.
                    .Restart =
                        [this, alive = Alive, weak](std::string& diagnostic)
                    {
                        const auto op = weak.lock();
                        if (!*alive || !op || Active != op)
                        {
                            diagnostic = "The K-Means run ended.";
                            return false;
                        }
                        if (op->CommitRequested) // the step run refuses a restart while accepting; kept as a guard
                        {
                            diagnostic = "K-Means is reading its labels back for Accept.";
                            return false;
                        }
                        ClearMailbox(*op);
                        Fold(*op);
                        op->Phase = KMeansGpuPhase::Initialize;
                        op->Row = op->Inner = 0;
                        op->Boundary = op->Presented = op->FinalAssignment = op->Moved = op->PreviewOnly = false;
                        op->ReseedPending = true;
                        op->Terminal = op->Ready = false; // a completed run restarts before Accept
                        op->PreviewDeferrals = 0;
                        op->Geometry = {};
                        op->Iterations.store(0u, std::memory_order_release);
                        op->FoldedIterations = 0u;
                        op->Result.Iterations = 0;
                        op->Result.Converged = false;
                        op->Result.Centroids.clear();
                        return true;
                    }},
                EditorStepRunJobs(context.JobCommands, jobIdentity, holder),
                refusal);
            if (a->Run == 0u)
            {
                DiscardEditorPointScalar(a->Commands, a->Transaction);
                return refuse(refusal);
            }
            a->Driver = steps.Driver(a->Run);
            a->Result.StepRun = a->Run;
            a->Holder = *holder;
            jobIdentity.Run = *holder;
            JoinEditorPointScalarRun(a->Transaction, std::move(jobIdentity));
            // The caller moves the full before-state into the publication callback
            // only after admission. Iteration retains just its live-slot map/config.
            a->Snapshot.Command = snapshot.Command;
            a->Snapshot.World = snapshot.World;
            a->Snapshot.Correlation = snapshot.Correlation;
            a->Snapshot.Input = snapshot.Input; // shared, not copied
            a->Snapshot.Params = snapshot.Params;
            auto& result = a->Result;
            result.Correlation = a->Snapshot.Correlation;
            result.World = a->Snapshot.World;
            result.StableEntityId = command.StableEntityId;
            result.Properties = command.Properties;
            result.Parameters = command.Parameters;
            result.RequestedBackend = result.ActualBackend = ClusteringBackend::VulkanCompute;
            result.ImplementationId = "vulkan_resident_paged_lloyd";
            result.BackendDiagnostic =
                "Resident stride-12 positions; ordered paged Lloyd phases share bounded submissions up to each host convergence readback.";
            result.LabelCount = a->Count;
            result.ClusterCount = a->Clusters;
            Active = std::move(a);
            return {.Accepted = true};
        }
        // The step run's Commit, once the labels are ready: reads them back and publishes (its transaction).
        void Accept()
        {
            const auto a = Active;
            if (!a || !a->Ready || a->Accepting)
                return;
            const auto snapshot = SnapshotEditorPointScalar(a->Commands, a->Transaction);
            if (!snapshot.CanAccept)
            {
                Finish(KMeansRunStatus::StaleSource, snapshot.AcceptRefusalReason);
                return;
            }
            a->Accepting = true;
            const auto result = AcceptEditorPointScalar(a->Commands, a->Transaction);
            if (Active == a && result.Status != EditorCommandStatus::Pending)
                Finish(result.Status == EditorCommandStatus::StaleEntity ? KMeansRunStatus::StaleSource
                                                                         : KMeansRunStatus::GeometryProcessingFailed,
                    result.Message);
        }
        KMeansGpuObservation Observe(CommandCorrelationId id, KMeansGpuAction action)
        {
            auto a = Active;
            if (!a || a->Snapshot.Correlation != id)
                return {};
            // The panel's buttons are the step run's controls (they change only the step run's state here); Accept
            // only while it can publish now. Resting: paused at an iteration boundary, before it ended (an ended run
            // prepares its final labels and is ready once they are).
            Fold(*a);
            const auto run = a->Steps->Observe(a->Run);
            const bool idle = run && !run->SegmentInFlight && run->Phase != StepRunPhase::Running &&
                run->End == StepRunEnd::None && !a->Terminal;
            const bool resting = idle && !a->Ready && a->Result.Iterations > 0u;
            const bool accepting = a->Accepting || a->CommitRequested;
            const bool ready = a->Ready || resting;
            const auto scalar = SnapshotEditorPointScalar(a->Commands, a->Transaction);
            const bool canAccept = ready && !a->CommitRequested && (a->Ready ? scalar.CanAccept : a->Current());
            std::string refusal;
            if (action == KMeansGpuAction::Stop)
                (void)a->Steps->Control(a->Run, StepRunAction::Stop, 1u, refusal);
            if (action == KMeansGpuAction::Discard)
                (void)a->Steps->Control(a->Run, StepRunAction::Discard, 1u, refusal); // its pump discards the run
            if (action == KMeansGpuAction::Accept && canAccept)
                (void)a->Steps->Control(a->Run, StepRunAction::Accept, 1u, refusal);
            return {.Running = !ready && !accepting,
                .ReadyToAccept = ready,
                .Accepting = accepting,
                .CanAccept = canAccept,
                .Message = idle && a->Result.Iterations == 0u ? "Paused before its first iteration."
                    : !ready                                  ? "GPU K-Means pages running."
                    : resting ? "Paused at an iteration boundary; Accept prepares its labels."
                    : scalar.AcceptRefusalReason.empty() ? "GPU labels await Accept or Discard."
                                                         : scalar.AcceptRefusalReason,
                .StepRun = a->Run,
                .Iterations = a->Result.Iterations,
                .Submissions = a->Result.GpuSubmissions,
                .Previews = a->Result.GpuPreviews,
                .InputUploadBytes = a->Result.GpuInputUploadBytes,
                .InputCacheHits = a->Result.GpuInputCacheHits,
                .CpuStageUploadBytes = a->Result.CpuStageUploadBytes,
                .CpuStageReadbackBytes = a->Result.CpuStageReadbackBytes};
        }
        // Records the next bounded page of the planner (Phase, Row, Inner) into `cmd`; `back` is the slot a preview
        // or the terminal labels write. Commands only, no owner state, so the worker's step and preview record here
        // as the owner's terminal pages do (their recorder notes the slot uses).
        static RHI::BufferHandle RecordPages(
            Operation* a, RHI::ICommandContext& cmd, const EditorPointScalarBack* back = nullptr)
        {
            a->Recorded = true;
            a->Boundary = a->Presented = false;
            // Terminal preview pages read the final centroids back instead of
            // diagnostics; Advance consumes them only after presentation completes.
            const bool terminalPreview =
                a->Terminal && (a->Phase == KMeansGpuPhase::Preview || a->Phase == KMeansGpuPhase::Presentation);
            if (a->Phase == KMeansGpuPhase::Preview && a->Row == 0)
            {
                const auto shader = RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite;
                cmd.BufferBarrier(
                    back->Typed.Buffer, shader | RHI::MemoryAccess::TransferRead, RHI::MemoryAccess::TransferWrite);
                if (a->Snapshot.Input->SlotCount != a->Count)
                {
                    if (a->Base)
                    {
                        cmd.BufferBarrier(a->Base->Buffer,
                            shader | RHI::MemoryAccess::TransferWrite,
                            RHI::MemoryAccess::TransferRead);
                        cmd.CopyBuffer(a->Base->Buffer, back->Typed.Buffer, 0, 0, a->Base->Bytes);
                    }
                    else
                        cmd.FillBuffer(back->Typed.Buffer, 0, back->Typed.Bytes, 0);
                }
                cmd.BufferBarrier(back->Typed.Buffer, RHI::MemoryAccess::TransferWrite, shader);
            }
            std::uint64_t budget = a->Limits.SubmissionPairs;
            std::uint32_t depth = KMeansSubmissionSerialDepth;
            // Device-only phase transitions continue in this command buffer under
            // the shared pair/depth budgets. Recording stops at the host boundaries:
            // completed Update, final assignment Reduce, and terminal Presentation.
            for (;;)
            {
                const auto outer = a->Phase == KMeansGpuPhase::Update ? a->Clusters
                    : a->Phase == KMeansGpuPhase::Presentation        ? std::uint32_t(a->Snapshot.Input->SlotCount)
                                                                      : a->Count;
                const auto inner = a->Phase == KMeansGpuPhase::Assign ? a->Clusters
                    : a->Phase == KMeansGpuPhase::Update              ? a->Count
                                                                      : 1u;
                if (a->Row == outer)
                {
                    if (a->Phase == KMeansGpuPhase::Update ||
                        (a->FinalAssignment && a->Phase == KMeansGpuPhase::Reduce))
                    {
                        a->Boundary = true;
                        break;
                    }
                    if (a->Phase == KMeansGpuPhase::Presentation)
                    {
                        a->Presented = true;
                        if (a->Terminal || a->PreviewOnly)
                            break;
                    }
                    a->Phase = a->Phase == KMeansGpuPhase::Initialize ? KMeansGpuPhase::Assign
                        : a->Phase == KMeansGpuPhase::Assign          ? KMeansGpuPhase::Reduce
                        : a->Phase == KMeansGpuPhase::Reduce          ? KMeansGpuPhase::Update
                        : a->Phase == KMeansGpuPhase::Preview         ? KMeansGpuPhase::Presentation
                                                                      : KMeansGpuPhase::Assign;
                    a->Row = a->Inner = 0;
                    continue;
                }
                const auto scan = std::min({inner, a->Limits.PagePairs, KMeansPageScanRows});
                // An Update tile holds at most 64 centroids (one combine lane each); a smaller tile keeps
                // `PagePairs / rows >= scan`, so its point chunks are today's.
                const auto rows = std::min({outer - a->Row,
                    std::max(1u, a->Limits.PagePairs / scan),
                    a->Phase == KMeansGpuPhase::Update ? KMeansBatchPages : 65535u * 64u,
                    a->Phase == KMeansGpuPhase::Reduce ? KMeansPageScanRows
                                                       : std::numeric_limits<std::uint32_t>::max()});
                // Page shape never depends on the remaining budget, so ordered
                // double combines group identically however phases share submissions.
                const auto columns = std::min({inner - a->Inner, scan, a->Limits.PagePairs / rows});
                if (a->Phase == KMeansGpuPhase::Reduce || a->Phase == KMeansGpuPhase::Update)
                {
                    // A batch: up to 64 consecutive pages of that shape (Reduce: row pages; Update: the tile's point
                    // chunks), their partials (pass A) added in page order by the combine (pass B), both recorded
                    // together or not at all so no partial crosses a submission.
                    const bool reduce = a->Phase == KMeansGpuPhase::Reduce;
                    const auto span = reduce
                        ? std::min({std::max(1u, a->Limits.PagePairs / scan), 65535u * 64u, KMeansPageScanRows})
                        : std::min(scan, a->Limits.PagePairs / rows);
                    const auto left = reduce ? outer - a->Row : inner - a->Inner;
                    const std::uint64_t pagePairs = reduce ? span : std::uint64_t(rows) * span;
                    const auto pageSerial = (span + 63) / 64 + 7;
                    const auto pages = std::uint32_t(std::min<std::uint64_t>({KMeansBatchPages,
                        (std::uint64_t(left) + span - 1) / span,
                        budget / pagePairs,
                        depth > pageSerial + 7 ? depth - pageSerial - 7 : 0u}));
                    if (pages == 0)
                        break;
                    const auto covered = std::uint32_t(std::min<std::uint64_t>(std::uint64_t(pages) * span, left));
                    depth -= pageSerial + pages + 7;
                    budget -= reduce ? covered : std::uint64_t(rows) * covered;
                    KMeansGpuPage page{a->Phase, a->Row, reduce ? span : rows, a->Inner, reduce ? 1u : span, pages};
                    (void)a->Workspace->Record(cmd, *a->Input, page);
                    page.Phase = reduce ? KMeansGpuPhase::ReduceCombine : KMeansGpuPhase::UpdateCombine;
                    (void)a->Workspace->Record(cmd, *a->Input, page);
                    if (reduce)
                        a->Row += covered;
                    else if ((a->Inner += covered) == inner)
                    {
                        a->Inner = 0;
                        a->Row += rows;
                    }
                    continue;
                }
                // Conservatively sum the longest lane path of every dispatch,
                // even though workgroups can overlap on the device.
                const auto serial = a->Phase == KMeansGpuPhase::Assign ? columns + 1 : 1u;
                if (std::uint64_t(rows) * columns > budget || serial > depth)
                    break;
                depth -= serial;
                budget -= std::uint64_t(rows) * columns;
                const bool preview = a->Phase == KMeansGpuPhase::Preview || a->Phase == KMeansGpuPhase::Presentation;
                (void)a->Workspace->Record(cmd,
                    *a->Input,
                    {a->Phase, a->Row, rows, a->Inner, columns},
                    preview ? back->Typed : Graphics::GpuPropertyView{},
                    preview ? back->Presentation : Graphics::GpuPropertyView{});
                a->Inner += columns;
                if (a->Inner == inner)
                {
                    a->Inner = 0;
                    a->Row += rows;
                }
            }
            return terminalPreview ? a->Workspace->Centroids() : a->Workspace->Diagnostics();
        }
        // The step run's Step, on a worker: one Lloyd iteration (Initialize first), its bounded pages submitted and
        // waited for here (Pages: a wait gives up once the run is aborted, its page stays in flight and the owner
        // settles on Pages.Last()) in the existing page shape and order up to the completed Update; only the
        // diagnostics (one KMeansGpuDiagnostics a page, pulled) come back. Owner state is never touched.
        static StepOutcome WorkerStep(Operation& a, StepRunMetric& metric)
        {
            const auto fail = [&a](std::string why)
            {
                a.WorkerDiagnostic = std::move(why);
                a.WorkerFailed.store(true, std::memory_order_release);
                return StepOutcome::Failed;
            };
            constexpr const char* deviceLost = "K-Means page did not complete: the device was lost.";
            if (a.Pages.Aborted())
                return StepOutcome::Failed;
            if (std::exchange(a.ReseedPending, false))
            {
                // A restart: the older pages (preview copies too) completed before the centroids are written again;
                // the next page waits for that upload (the graphics lane's last ticket). An abort meanwhile stays, so
                // that page is never submitted.
                switch (WaitForComputePage(*a.Transfer,
                    a.Pages.Last(),
                    [&a]
                    {
                        return a.Pages.Aborted();
                    }))
                {
                case ComputePageWait::Complete:  break;
                case ComputePageWait::Failed:    return fail(deviceLost);
                case ComputePageWait::Abandoned:
                case ComputePageWait::TimedOut:  return StepOutcome::Failed;
                }
                if (!a.Workspace->Reseed(*a.Transfer, a.Seeds))
                    return fail("K-Means GPU workspace restart failed.");
                a.Pages.OrderNextAfterIssuedTransfers();
            }
            for (;;)
            {
                // Every page's bytes are taken (its staging slot, record and command buffer return without a
                // frame); only a boundary page's count.
                KMeansGpuDiagnostics d{};
                switch (a.Pages.Run(
                    [&a](RHI::ICommandContext& cmd)
                    {
                        return RecordPages(&a, cmd);
                    },
                    std::as_writable_bytes(std::span{&d, 1u})))
                {
                case WorkerGpuPageStatus::Complete: break;
                case WorkerGpuPageStatus::Aborted:  return StepOutcome::Failed; // the owner gave the run up
                case WorkerGpuPageStatus::Refused:
                    a.RecordedFailure = a.Recorded;
                    return fail("K-Means page submission rejected.");
                case WorkerGpuPageStatus::Failed:
                case WorkerGpuPageStatus::TimedOut:
                    return fail("K-Means page completion or diagnostic readback failed.");
                }
                if (!a.Boundary)
                    continue;
                a.Row = a.Inner = 0;
                a.Boundary = false;
                if (d.Invalid || !std::isfinite(d.Inertia) || !std::isfinite(d.MaxShift))
                    return fail("K-Means device numerical failure.");
                a.Result.Inertia = d.Inertia;
                a.Result.MaxDistanceIndex = d.MaxDistanceIndex;
                a.Geometry.Inertia = d.Inertia;
                a.Geometry.MaxDistanceIndex = d.MaxDistanceIndex;
                const std::uint32_t iterations = a.Iterations.fetch_add(1u, std::memory_order_acq_rel) + 1u;
                a.Result.Converged = (d.Changed & 1u) == 0 ||
                    d.MaxShift <= a.Snapshot.Params.ConvergenceTolerance * a.Snapshot.Params.ConvergenceTolerance;
                a.Geometry.Iterations = iterations;
                a.Geometry.Converged = a.Result.Converged;
                a.Moved = (d.Changed & 2u) != 0;
                a.Phase = KMeansGpuPhase::Assign;
                metric.Values = {double(d.Inertia), double(d.MaxShift), 0.0, 0.0};
                return a.Result.Converged ? StepOutcome::Converged : StepOutcome::Continue;
            }
        }
        // The step run's Preview, after a step on the worker (or at a rest on the owner): the current centroids'
        // labels into the slot the owner offered, as their own pages (never fused with an iteration), not waited
        // for; Show publishes them once the fence completed. No slot offered: no copy (the run goes on).
        static bool WorkerPreview(Operation& a, StepPreview& preview)
        {
            EditorPointScalarBack back{};
            if (a.Terminal ||
                !a.Mailbox.Borrow(
                    [&back](const EditorPointScalarBack& slot)
                    {
                        back = slot; // the views alone: the leases stay in the mailbox
                        back.Typed.Lease.reset();
                        back.Presentation.Lease.reset();
                        back.Companion.Lease.reset();
                    }))
                return false;
            a.Phase = KMeansGpuPhase::Preview;
            a.Row = a.Inner = 0;
            a.PreviewOnly = true;
            const auto record = [&a, &back](RHI::ICommandContext& cmd)
            {
                return RecordPages(&a, cmd, &back);
            };
            // Completion-only copies the owner's Show polls; one refused (or any once the run is aborted) drops the
            // preview: the slot is offered again, the run goes on.
            std::uint64_t fence = 0u;
            do
                fence = a.Pages.SubmitCopy(record).Value;
            while (fence != 0u && !a.Presented);
            a.PreviewOnly = a.Presented = false;
            a.Phase = KMeansGpuPhase::Assign;
            a.Row = a.Inner = 0;
            a.Mailbox.Recorded(fence);
            preview.Fence = fence;
            return fence != 0u;
        }
        // The step run's Show (owner): a completed copy is published; an incomplete one waits; one no longer in the
        // mailbox (superseded by the terminal labels or a restart) is never shown.
        StepRunShow Show(Operation& a, const StepPreview& preview)
        {
            if (preview.Fence == 0u)
                return StepRunShow::Never;
            if (!a.Transfer->IsComplete(RHI::ReadbackToken{preview.Fence}))
                return StepRunShow::Later;
            auto slot = a.Mailbox.Take(preview.Fence);
            if (!slot)
                return StepRunShow::Never;
            slot.reset(); // its leases go before the publication (the copy completed)
            bool published = false;
            {
                const ScopedMicros timer{&Stats.GpuPreviewPublishMicros};
                published = PublishEditorPointScalarBack(
                    a.Transaction, false, std::array{0.f, float(std::max(1u, a.Clusters - 1u))});
            }
            if (!published)
            {
                a.PreviewFailed = true; // the next Advance ends the run (not from inside the run's own pump)
                return StepRunShow::Never;
            }
            ++a.Result.GpuPreviews;
            ++Stats.GpuPreviews;
            return StepRunShow::Shown;
        }
        // Main thread, frame maintenance (and a discard): settles finished runs, applies ends, offers the worker a
        // preview slot, prepares the terminal labels, and tells its step run whether an owner page is in flight.
        void Advance()
        {
            std::erase_if(Settling, [this](const auto& settling) { return Settle(settling); });
            AdvanceActive();
            if (const auto a = Active)
                a->Driver.Recording(bool(a->Page) || a->Discard);
        }
        void AdvanceActive()
        {
            const auto a = Active;
            if (!a)
                return;
            if (a->Page && a->Page->State != SpatialQueryState::Ready && a->Page->State != SpatialQueryState::Failed)
                return;
            Fold(*a);
            const auto run = a->Steps->Observe(a->Run);
            const bool busy = run && run->WorkerInFlight;
            // An end decided while a worker step ran is applied once it returned.
            if (a->Ending)
            {
                if (!busy)
                    Finish(a->Ending->first, std::move(a->Ending->second));
                return;
            }
            // The run ended on its own side: a failed step (its reason is the worker's), a discard or cancel (also of
            // a pending Commit, whose run has no pump left to call Discard).
            if (run && IsTerminal(run->Phase) && run->Phase != StepRunPhase::Committed && !a->Discard)
            {
                a->Discard = true;
                a->Pages.Abort();
            }
            if (a->Discard || (a->Context.AttachmentActive && !a->Context.AttachmentActive()))
            {
                const bool failed = !busy && a->WorkerFailed.load(std::memory_order_acquire);
                Finish(failed ? KMeansRunStatus::GeometryProcessingFailed : KMeansRunStatus::Cancelled,
                    failed ? a->WorkerDiagnostic : "K-Means discarded; CPU outputs retained.");
                return;
            }
            if (a->PreviewFailed)
            {
                Finish(KMeansRunStatus::GeometryProcessingFailed, "K-Means preview publication refused.");
                return;
            }
            if (a->Ready)
            {
                if (a->CommitRequested && !a->Accepting)
                    Accept();
                return; // Manual stale results remain inspectable with Accept disabled.
            }
            if (!a->Current())
            {
                Finish(KMeansRunStatus::StaleSource, "K-Means source or output changed.");
                return;
            }
            // Live preview: a free slot in the mailbox for the worker's next copy (or a rest's), never waited for; a
            // busy ring offers none and that preview is dropped. Preview off acquires nothing.
            if (a->Snapshot.Command.Parameters.Execution.PreviewEvery != 0u && !a->Terminal && !a->CommitRequested &&
                run && run->End == StepRunEnd::None && a->Mailbox.Empty())
                if (auto back = AcquireEditorPointScalarBack(a->Transaction))
                    (void)a->Mailbox.Offer(std::move(*back));
            if (busy)
                return; // the worker iterates

            if (a->Page)
            {
                if (a->Page->State == SpatialQueryState::Failed)
                {
                    Finish(KMeansRunStatus::GeometryProcessingFailed, a->Page->Diagnostic);
                    return;
                }
                a->Result.CpuStageReadbackBytes += a->Page->Data.size();
                if (a->Presented) // the terminal labels: their preview publishes, the centroids come back
                {
                    a->Presented = false;
                    a->Back.reset();
                    bool published = false;
                    {
                        const ScopedMicros timer{&Stats.GpuPreviewPublishMicros};
                        published = PublishEditorPointScalarBack(
                            a->Transaction, true, std::array{0.f, float(std::max(1u, a->Clusters - 1u))});
                    }
                    if (!published)
                    {
                        Finish(KMeansRunStatus::GeometryProcessingFailed, "K-Means preview publication refused.");
                        return;
                    }
                    ++a->Result.GpuPreviews;
                    ++Stats.GpuPreviews;
                    if (a->Page->Data.size() != std::size_t(a->Clusters) * 12)
                    {
                        Finish(KMeansRunStatus::GeometryProcessingFailed, "K-Means centroid readback size mismatch.");
                        return;
                    }
                    a->Geometry.Centroids.resize(a->Clusters);
                    std::memcpy(a->Geometry.Centroids.data(), a->Page->Data.data(), a->Page->Data.size());
                    for (const auto& centroid : a->Geometry.Centroids)
                        if (!Geometry::Validation::IsFinite(centroid))
                        {
                            Finish(KMeansRunStatus::GeometryProcessingFailed,
                                "K-Means centroid readback contains non-finite values.");
                            return;
                        }
                    a->Result.Centroids = a->Geometry.Centroids;
                    a->Ready = true;
                    a->Page.reset();
                    if (a->CommitRequested)
                        Accept();
                    return;
                }
                if (a->Boundary) // the final assignment's Reduce: labels and diagnostics of the final centroids
                {
                    a->Row = a->Inner = 0;
                    a->Boundary = false;
                    if (a->Page->Data.size() != sizeof(KMeansGpuDiagnostics))
                    {
                        Finish(KMeansRunStatus::GeometryProcessingFailed, "K-Means diagnostic readback size mismatch.");
                        return;
                    }
                    KMeansGpuDiagnostics d;
                    std::memcpy(&d, a->Page->Data.data(), sizeof(d));
                    if (d.Invalid || !std::isfinite(d.Inertia) || !std::isfinite(d.MaxShift))
                    {
                        Finish(KMeansRunStatus::GeometryProcessingFailed, "K-Means device numerical failure.");
                        return;
                    }
                    // Refresh labels and diagnostics for the final centroids; this is not another Lloyd update or
                    // convergence decision.
                    a->Result.Inertia = d.Inertia;
                    a->Result.MaxDistanceIndex = d.MaxDistanceIndex;
                    a->Geometry.Inertia = d.Inertia;
                    a->Geometry.MaxDistanceIndex = d.MaxDistanceIndex;
                    a->FinalAssignment = false;
                    a->Phase = KMeansGpuPhase::Preview;
                }
                a->Page.reset();
            }
            if (!a->Terminal)
            {
                // Accept (from a rest), or the run ended (convergence, the cap, Stop): the labels of the current
                // centroids. Otherwise the run rests or waits for its next segment, nothing for the owner to do.
                if (!a->CommitRequested && !Ended(*a))
                    return;
                if (a->Result.Iterations == 0u)
                {
                    Finish(KMeansRunStatus::Cancelled, "K-Means stopped before its first iteration.");
                    return;
                }
                ClearMailbox(*a); // a recorded, unpublished live preview is superseded by the terminal labels
                BeginFinal(*a);
            }
            if (a->Phase == KMeansGpuPhase::Preview && !a->Back)
            {
                a->Back = AcquireEditorPointScalarBack(a->Transaction);
                if (!a->Back)
                {
                    if (++a->PreviewDeferrals >= 600)
                        Finish(KMeansRunStatus::GeometryProcessingFailed,
                            "K-Means terminal preview residency remained busy for 600 retries.");
                    return;
                }
            }
            a->PreviewDeferrals = 0;
            const bool terminalPreview =
                a->Phase == KMeansGpuPhase::Preview || a->Phase == KMeansGpuPhase::Presentation;
            const auto bytes =
                terminalPreview ? std::uint64_t(a->Clusters) * 12 : std::uint64_t(sizeof(KMeansGpuDiagnostics));
            a->Recorded = false;
            a->Page = a->Context.SpatialIndices->QueueGpuCompute(bytes,
                [this, alive = Alive, a](RHI::ICommandContext& cmd, const SpatialGpuIndexView&)
                {
                    if (!*alive)
                        return RHI::BufferHandle{};
                    // The owner's recorder, inside the cache's submission scope: the slots it uses are noted.
                    auto& residency = *a->Context.SpatialIndices->PropertyResidency();
                    const auto frame = Device.GetGlobalFrameNumber();
                    for (const auto* view : {&*a->Input, a->Base ? &*a->Base : nullptr})
                        if (view != nullptr)
                            residency.NoteUse(view->Buffer, frame);
                    if (a->Back)
                    {
                        residency.NoteUse(a->Back->Typed.Buffer, frame);
                        residency.NoteUse(a->Back->Presentation.Buffer, frame);
                    }
                    return RecordPages(a.get(), cmd, a->Back ? &*a->Back : nullptr);
                });
            if (!a->Page)
            {
                Finish(KMeansRunStatus::GeometryProcessingFailed, "K-Means page submission rejected.");
                return;
            }
            ++a->OwnerSubmissions;
        }
    };
    ClusteringGpuState::ClusteringGpuState(RHI::IDevice& d, RHI::ITransferQueue&, ClusteringModuleStats& stats)
        : m_Impl(std::make_unique<Impl>(d, stats))
    {
    }
    ClusteringGpuState::~ClusteringGpuState() = default;
    void ClusteringGpuState::UseStepRuns(StepRunsService* steps) noexcept
    {
        m_Impl->StepRuns = steps;
    }
    ClusteringGpuSubmission ClusteringGpuState::Start(KMeansSnapshot& s,
        const EditorProcessingContext& c,
        std::function<bool()> current,
        std::function<EditorCommandStatus(const Geometry::KMeans::KMeansResult&)> publish)
    {
        return m_Impl->Start(s, c, std::move(current), std::move(publish));
    }
    KMeansGpuObservation ClusteringGpuState::GpuRun(CommandCorrelationId id, KMeansGpuAction action)
    {
        return m_Impl->Observe(id, action);
    }
    void ClusteringGpuState::RecordFrameCommands(RHI::ICommandContext&) {}
    void ClusteringGpuState::DrainCompletedTransfers()
    {
        m_Impl->Advance();
    }
    std::optional<ClusteringGpuResult> ClusteringGpuState::ConsumeCompleted()
    {
        return std::exchange(m_Impl->Completed, std::nullopt);
    }
    bool ClusteringGpuState::HasInFlightWork() const noexcept
    {
        return bool(m_Impl->Active) || bool(m_Impl->Completed) || !m_Impl->Retired.empty() ||
            !m_Impl->Settling.empty();
    }
    bool ClusteringGpuState::Supersede(const RunKMeans& request, const WorldHandle world)
    {
        return m_Impl->Supersede(request, world);
    }
    bool ClusteringGpuState::Settled() const noexcept
    {
        return m_Impl->Settled();
    }
    std::string ClusteringGpuState::ReadinessDiagnostic(
        const RunKMeans& request, const EditorProcessingContext& context) const
    {
        return m_Impl->ReadinessDiagnostic(request, context);
    }
}

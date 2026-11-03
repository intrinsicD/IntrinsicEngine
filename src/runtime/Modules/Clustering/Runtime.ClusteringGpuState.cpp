module;

#include <array>
#include <algorithm>
#include <chrono>
#include <bit>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
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
#include "Modules/Clustering/Runtime.ClusteringGpuState.Internal.hpp"
namespace Extrinsic::Runtime
{
    namespace
    {
        // A step run's jobs as editor jobs of `surface` under `identity`: rows of its Jobs window, a cancel cancelling
        // the whole run. `holder` gets the first job. (One GPU run at a time and its transaction's reservation of
        // the output admit it; a finished run's holder can still be listed for a frame.)
        StepRunJobs EditorStepRunJobs(EditorJobCommandSurface surface, EditorJobIdentity identity,
                                      std::shared_ptr<JobToken> holder)
        {
            return {.Submit = [surface, identity, holder](PluginJob job, std::string*) -> PluginJobHandle
                    {
                        auto id = identity;
                        id.Run = job.Run.Token;
                        auto held = std::make_shared<PluginJob>(std::move(job));
                        auto finalized = std::make_shared<bool>(false);
                        JobDesc desc{};
                        desc.DebugName = held->Label;
                        desc.Work = [held](const JobCancellation& cancellation)
                        {
                            held->Work([&cancellation] { return cancellation.IsCancelled(); });
                            return JobResultEnvelope::Make(true);
                        };
                        if (held->IsReadyToApply) desc.IsReadyToApply = [held] { return held->IsReadyToApply(); };
                        desc.PublishCompletion = [held, finalized](KernelEventBus&, const JobResultEnvelope&)
                        {
                            *finalized = true;
                            return held->Completion(true);
                        };
                        desc.FinalizeUnpublishedOnMainThread = [held, finalized]
                        {
                            if (!std::exchange(*finalized, true)) (void)held->Completion(false);
                        };
                        const JobToken token = surface.Submit ? surface.Submit(std::move(desc), std::move(id)) : JobToken{};
                        if (!id.Run.IsValid()) *holder = token;
                        return {token};
                    },
                    .Cancel = [surface](const PluginJobHandle job)
                    { return CancelEditorRuns(surface, std::span<const JobToken>{&job.Token, 1u}).Requested > 0u; }};
        }
    }

    struct ClusteringGpuState::Impl
    {
        struct Operation
        {
            KMeansSnapshot Snapshot{};
            EditorProcessingContext Context{};
            EditorProcessingCommands Commands{};
            EditorPointScalarTransactionHandle Transaction{};
            std::shared_ptr<KMeansGpuWorkspace> Workspace{};
            std::optional<Graphics::GpuPropertyView> Input{}, Base{};
            std::optional<EditorPointScalarBack> Back{};
            std::shared_ptr<SpatialGpuResult> Page{};
            std::function<bool()> Current{};
            std::function<EditorCommandStatus(const Geometry::KMeans::KMeansResult&)> Publish{};
            Geometry::KMeans::KMeansResult Geometry{};
            KMeansRunCompleted Result{};
            KMeansGpuPhase Phase{KMeansGpuPhase::Initialize};
            KMeansPagingLimits Limits{KMeansPagingForTesting};
            std::uint32_t Row{}, Inner{}, Count{}, Clusters{}, PreviewDeferrals{};
            bool Boundary{}, Presented{}, Terminal{}, FinalAssignment{}, Ready{}, Accepting{}, Discard{}, Recorded{};
            // The step run: its controls decide each next iteration. A run rests (no page) at an iteration boundary
            // until it may continue, is stopped or accepted; `Moved`: the last update moved a centroid, so labels for
            // those centroids need a final assignment. `RestPreview`: the page recorded before resting is its
            // boundary's preview alone. `CommitRequested`: Accept (the run's Commit) is preparing and reading labels.
            StepRunsService* Steps{};
            std::uint64_t Run{};
            StepRunDriver Driver{};
            JobToken Holder{}; // the step run's first editor job: its run's token
            bool Resting{true}, Moved{}, RestPreview{}, CommitRequested{};
        };
        RHI::IDevice& Device;
        std::shared_ptr<bool> Alive{std::make_shared<bool>(true)};
        std::shared_ptr<Operation> Active{};
        // Recorder failures can follow recorded commands. Retain their workspaces and
        // leases until the participant's device-idle shutdown.
        std::vector<std::shared_ptr<Operation>> Retired{};
        // Finished runs whose last page is still in flight: their workspace returns (and their step run retires)
        // once it completed.
        std::vector<std::shared_ptr<Operation>> Settling{};
        // One run at a time: the last run's buffers and pipelines, kept only
        // when no submission recorded with them can still execute.
        std::shared_ptr<KMeansGpuWorkspace> Idle{};
        std::optional<ClusteringGpuResult> Completed{};
        explicit Impl(RHI::IDevice& d) : Device(d) {}
        ~Impl()
        {
            *Alive = false;
            if (Active)
                DiscardEditorPointScalar(Active->Commands, Active->Transaction);
        }
        void Finish(KMeansRunStatus status, std::string why)
        {
            auto a = std::move(Active);
            if (!a)
                return;
            a->Result.Status = status;
            a->Result.Message = std::move(why);
            a->Result.Error =
                status == KMeansRunStatus::Applied ? Core::ErrorCode::Success : Core::ErrorCode::InvalidState;
            DiscardEditorPointScalar(a->Commands, a->Transaction);
            Tell(*a, status);
            if (!Settle(a))
                Settling.push_back(a);
            Completed = ClusteringGpuResult{.Published = std::move(a->Result)};
        }
        // A finished run's last page completed (or none was in flight): its workspace is free again, a failed
        // recorded one stays retired, and its step run may retire. False while the page is in flight.
        bool Settle(const std::shared_ptr<Operation>& a)
        {
            if (a->Page && a->Page->State != SpatialQueryState::Ready && a->Page->State != SpatialQueryState::Failed)
                return false;
            if (a->Recorded && a->Page && a->Page->State == SpatialQueryState::Failed)
                Retired.push_back(a);
            else if (a->Workspace && !Idle)
                Idle = std::move(a->Workspace);
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
            else if (a.Steps)
                (void)a.Steps->Control(a.Run, StepRunAction::Discard, 1u, refusal);
        }
        // Stop, the iteration cap or convergence ended the step run (it is not discarded).
        [[nodiscard]] static bool Ended(const Operation& a)
        {
            const auto run = a.Steps ? a.Steps->Observe(a.Run) : std::nullopt;
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
            StepRunsService& steps,
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
            if (snapshot.Points.empty() || snapshot.Points.size() > std::numeric_limits<std::uint32_t>::max() ||
                snapshot.SlotCount > std::numeric_limits<std::uint32_t>::max())
                return {.Refused = true, .Diagnostic = "K-Means row count is not representable by the device."};
            auto a = std::make_shared<Operation>();
            a->Context = context;
            a->Commands = BindEditorProcessingCommands(context);
            a->Current = std::move(current);
            a->Publish = std::move(publish);
            a->Count = std::uint32_t(snapshot.Points.size());
            a->Clusters = std::min(a->Count, snapshot.Params.ClusterCount);
            a->Limits.SubmissionPairs = std::max(1u, a->Limits.SubmissionPairs);
            a->Limits.PagePairs = std::clamp(a->Limits.PagePairs, 1u, a->Limits.SubmissionPairs);
            auto& r = *context.SpatialIndices->PropertyResidency();
            const auto before = r.Stats();
            const auto entity = SelectionController::ToEntityHandle(snapshot.Command.StableEntityId);
            a->Input = ResolveGpuPropertyInput(
                r, *context.Scene, context.World, entity, snapshot.Command.Properties.InputPositions);
            if (!a->Input)
                return {.Refused = true, .Diagnostic = "K-Means resident input is busy or unavailable."};
            auto seeds = Geometry::KMeans::BuildInitialCentroids(snapshot.Points, {}, snapshot.Params, a->Clusters);
            if (seeds.size() != a->Clusters)
                return {.Diagnostic = "K-Means GPU workspace or seed preparation failed."};
            // A failed Prepare may leave partial state; that workspace is dropped.
            a->Workspace = Idle ? std::move(Idle) : std::make_shared<KMeansGpuWorkspace>(Device);
            const bool identity = snapshot.SlotCount == a->Count;
            if (!a->Workspace->Prepare(a->Count, seeds, identity ? std::span<const std::uint32_t>{} : snapshot.Slots))
                return {.Diagnostic = "K-Means GPU workspace or seed preparation failed."};
            const auto refuse = [&](std::string why) -> ClusteringGpuSubmission
            {
                Idle = std::move(a->Workspace);
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
                a->Base = ResolveGpuPropertyInput(r, *context.Scene, context.World, entity, typed);
                if (!a->Base)
                    return refuse("Prior K-Means labels are busy or unavailable for deleted-slot preservation.");
            }
            a->Result.GpuInputUploadBytes = r.Stats().UploadBytes - before.UploadBytes;
            a->Result.GpuInputCacheHits = r.Stats().Hits - before.Hits;
            EditorPointScalarTransactionSnapshot admission;
            std::weak_ptr<Operation> weak = a;
            a->Transaction = BeginEditorPointScalarPublication(a->Commands,
                {.EntityId = snapshot.Command.StableEntityId,
                    .Count = std::uint32_t(snapshot.SlotCount),
                    .Output = typed,
                    .Label = "Run K-Means clustering",
                    .Current = a->Current,
                    .Publish =
                        [weak](std::span<const std::byte> bytes)
                    {
                        auto op = weak.lock();
                        if (!op)
                            return EditorCommandStatus::StaleEntity;
                        op->Driver.Publishing(); // from here the publication decides the step run's end
                        op->Geometry.Labels.resize(op->Count);
                        for (std::uint32_t i = 0; i < op->Count; ++i)
                        {
                            std::uint32_t label{};
                            std::memcpy(&label, bytes.data() + std::size_t(op->Snapshot.Slots[i]) * 4, 4);
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
                    if (!*alive || !op || Active != op)
                        return;
                    op->Result.CpuStageReadbackBytes += result.CpuStageReadbackBytes;
                    // An unpublished end is "cancelled or stale"; a cancel accepted for one of the run's jobs (its
                    // Accept stage alone, say) makes it a cancel, as the agent lane relabels runs.
                    const auto& jobs = op->Context.JobCommands;
                    const bool cancelled = result.Phase == EditorGpuTransactionPhase::Discarded &&
                        (result.Status != EditorCommandStatus::StaleEntity ||
                            (jobs.RunCancelRequested && jobs.RunCancelRequested(op->Holder)));
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
            a->Run = steps.Start(
                {.Label = "K-Means",
                    .Owner = "Runtime.ClusteringModule",
                    .Output = {.StableEntityId = command.StableEntityId, .Property = command.Properties.OutputLabels},
                    .IterationCap = snapshot.Params.MaxIterations,
                    .AcceptOnEnd = command.AutoAccept,
                    // Pages limit the rate: one submission a frame, previews GpuPreviewInterval iterations apart.
                    .PreviewInterval = std::chrono::nanoseconds{0},
                    .MetricNames = {"inertia", "max_shift_squared", "", ""}},
                {.Commit =
                        [this, alive = Alive, weak](std::string& diagnostic)
                    {
                        const auto op = weak.lock();
                        if (!*alive || !op || Active != op)
                        {
                            diagnostic = "The K-Means run ended.";
                            return StepRunCommit::Refused;
                        }
                        op->CommitRequested = true; // Advance prepares the labels, then reads them back
                        return StepRunCommit::Pending;
                    },
                    .Discard =
                        [this, alive = Alive, weak]
                    {
                        const auto op = weak.lock();
                        if (!*alive || !op || Active != op)
                            return;
                        if (op->Accepting)
                            DiscardEditorPointScalar(op->Commands, op->Transaction);
                        else
                        {
                            op->Discard = true;
                            Advance();
                        }
                    }},
                EditorStepRunJobs(context.JobCommands, jobIdentity, holder),
                refusal);
            if (a->Run == 0u)
            {
                DiscardEditorPointScalar(a->Commands, a->Transaction);
                return refuse(refusal);
            }
            a->Driver = steps.Driver(a->Run);
            a->Holder = *holder;
            jobIdentity.Run = *holder;
            JoinEditorPointScalarRun(a->Transaction, std::move(jobIdentity));
            (void)steps.Control(a->Run, StepRunAction::Run, 0u, refusal);
            // The caller moves the full before-state into the publication callback
            // only after admission. Iteration retains just its live-slot map/config.
            a->Snapshot.Command = snapshot.Command;
            a->Snapshot.World = snapshot.World;
            a->Snapshot.Correlation = snapshot.Correlation;
            a->Snapshot.Slots = snapshot.Slots;
            a->Snapshot.SlotCount = snapshot.SlotCount;
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
            // The panel's buttons are the step run's controls; Accept only while it can publish now.
            const auto canAccept = [&]
            {
                const bool resting = a->Resting && !a->Ready && a->Result.Iterations > 0u;
                return (a->Ready || resting) && !a->CommitRequested &&
                    (a->Ready ? SnapshotEditorPointScalar(a->Commands, a->Transaction).CanAccept : a->Current());
            };
            std::string refusal;
            if (action == KMeansGpuAction::Stop)
                (void)a->Steps->Control(a->Run, StepRunAction::Stop, 1u, refusal);
            if (action == KMeansGpuAction::Discard)
                (void)a->Steps->Control(a->Run, StepRunAction::Discard, 1u, refusal); // its pump discards the run
            if (action == KMeansGpuAction::Accept && canAccept())
                (void)a->Steps->Control(a->Run, StepRunAction::Accept, 1u, refusal);
            const bool resting = a->Resting && !a->Ready && a->Result.Iterations > 0u;
            const bool accepting = a->Accepting || a->CommitRequested;
            const bool ready = a->Ready || resting;
            const auto scalar = SnapshotEditorPointScalar(a->Commands, a->Transaction);
            return {.Running = !ready && !accepting,
                .ReadyToAccept = ready,
                .Accepting = accepting,
                .CanAccept = canAccept(),
                .Message = a->Resting && a->Result.Iterations == 0u ? "Paused before its first iteration."
                    : !ready ? "GPU K-Means pages running."
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
        RHI::BufferHandle Record(const std::shared_ptr<Operation>& a, RHI::ICommandContext& cmd)
        {
            a->Recorded = true;
            a->Boundary = a->Presented = false;
            auto& residency = *a->Context.SpatialIndices->PropertyResidency();
            residency.NoteUse(a->Input->Buffer, Device.GetGlobalFrameNumber());
            // Terminal preview pages read the final centroids back instead of
            // diagnostics; Advance consumes them only after presentation completes.
            const bool terminalPreview =
                a->Terminal && (a->Phase == KMeansGpuPhase::Preview || a->Phase == KMeansGpuPhase::Presentation);
            if (a->Phase == KMeansGpuPhase::Preview && a->Row == 0)
            {
                const auto shader = RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite;
                cmd.BufferBarrier(
                    a->Back->Typed.Buffer, shader | RHI::MemoryAccess::TransferRead, RHI::MemoryAccess::TransferWrite);
                if (a->Snapshot.SlotCount != a->Count)
                {
                    if (a->Base)
                    {
                        cmd.BufferBarrier(a->Base->Buffer,
                            shader | RHI::MemoryAccess::TransferWrite,
                            RHI::MemoryAccess::TransferRead);
                        residency.NoteUse(a->Base->Buffer, Device.GetGlobalFrameNumber());
                        cmd.CopyBuffer(a->Base->Buffer, a->Back->Typed.Buffer, 0, 0, a->Base->Bytes);
                    }
                    else
                        cmd.FillBuffer(a->Back->Typed.Buffer, 0, a->Back->Typed.Bytes, 0);
                }
                cmd.BufferBarrier(a->Back->Typed.Buffer, RHI::MemoryAccess::TransferWrite, shader);
            }
            std::uint64_t budget = a->Limits.SubmissionPairs;
            std::uint32_t depth = KMeansSubmissionSerialDepth;
            // Device-only phase transitions continue in this command buffer under
            // the shared pair/depth budgets. Recording stops at the host boundaries:
            // completed Update, final assignment Reduce, and terminal Presentation.
            for (;;)
            {
                const auto outer = a->Phase == KMeansGpuPhase::Update ? a->Clusters
                    : a->Phase == KMeansGpuPhase::Presentation        ? std::uint32_t(a->Snapshot.SlotCount)
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
                        if (a->Terminal || a->RestPreview)
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
                const auto rows = std::min({outer - a->Row,
                    std::max(1u, a->Limits.PagePairs / scan),
                    a->Phase == KMeansGpuPhase::Update ? 65535u : 65535u * 64u,
                    a->Phase == KMeansGpuPhase::Reduce ? KMeansPageScanRows
                                                       : std::numeric_limits<std::uint32_t>::max()});
                // Page shape never depends on the remaining budget, so ordered
                // double combines group identically however phases share submissions.
                const auto columns = std::min({inner - a->Inner, scan, a->Limits.PagePairs / rows});
                // Conservatively sum the longest lane path of every dispatch,
                // even though workgroups can overlap on the device.
                const auto serial = a->Phase == KMeansGpuPhase::Assign ? columns + 1
                    : a->Phase == KMeansGpuPhase::Update               ? (columns + 63) / 64 + 7
                    : a->Phase == KMeansGpuPhase::Reduce               ? (rows + 63) / 64 + 7
                                                                       : 1u;
                if (std::uint64_t(rows) * columns > budget || serial > depth)
                    break;
                depth -= serial;
                budget -= std::uint64_t(rows) * columns;
                const bool preview = a->Phase == KMeansGpuPhase::Preview || a->Phase == KMeansGpuPhase::Presentation;
                (void)a->Workspace->Record(cmd,
                    *a->Input,
                    {a->Phase, a->Row, rows, a->Inner, columns},
                    preview ? a->Back->Typed : Graphics::GpuPropertyView{},
                    preview ? a->Back->Presentation : Graphics::GpuPropertyView{});
                a->Inner += columns;
                if (a->Inner == inner)
                {
                    a->Inner = 0;
                    a->Row += rows;
                }
            }
            if (a->Back)
            {
                residency.NoteUse(a->Back->Typed.Buffer, Device.GetGlobalFrameNumber());
                residency.NoteUse(a->Back->Presentation.Buffer, Device.GetGlobalFrameNumber());
            }
            return terminalPreview ? a->Workspace->Centroids() : a->Workspace->Diagnostics();
        }
        // Main thread, frame maintenance (and a discard): settles finished runs, moves the active one on, and tells
        // its step run whether a page is in flight.
        void Advance()
        {
            std::erase_if(Settling, [this](const auto& settling) { return Settle(settling); });
            AdvanceActive();
            if (const auto a = Active)
                a->Driver.Recording(bool(a->Page));
        }
        void AdvanceActive()
        {
            const auto a = Active;
            if (!a)
                return;
            if (a->Page && a->Page->State != SpatialQueryState::Ready && a->Page->State != SpatialQueryState::Failed)
                return;
            if (a->Discard || (a->Context.AttachmentActive && !a->Context.AttachmentActive()))
            {
                Finish(KMeansRunStatus::Cancelled, "K-Means discarded; CPU outputs retained.");
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
            if (a->Page)
            {
                if (a->Page->State == SpatialQueryState::Failed)
                {
                    Finish(KMeansRunStatus::GeometryProcessingFailed, a->Page->Diagnostic);
                    return;
                }
                a->Result.CpuStageReadbackBytes += a->Page->Data.size();
                // A non-terminal preview recorded before the next iteration in
                // the same submission publishes before that iteration's diagnostics.
                if (a->Presented)
                {
                    a->Presented = false;
                    a->Back.reset();
                    if (!PublishEditorPointScalarBack(
                            a->Transaction, a->Terminal, std::array{0.f, float(std::max(1u, a->Clusters - 1u))}))
                    {
                        Finish(KMeansRunStatus::GeometryProcessingFailed, "K-Means preview publication refused.");
                        return;
                    }
                    ++a->Result.GpuPreviews;
                    if (a->RestPreview)
                    {
                        a->RestPreview = false;
                        a->Resting = true;
                        a->Phase = KMeansGpuPhase::Assign;
                        a->Row = a->Inner = 0;
                    }
                    if (a->Terminal)
                    {
                        if (a->Page->Data.size() != std::size_t(a->Clusters) * 12)
                        {
                            Finish(
                                KMeansRunStatus::GeometryProcessingFailed, "K-Means centroid readback size mismatch.");
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
                }
                if (a->Boundary)
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
                    a->Result.Inertia = d.Inertia;
                    a->Result.MaxDistanceIndex = d.MaxDistanceIndex;
                    a->Geometry.Inertia = d.Inertia;
                    a->Geometry.MaxDistanceIndex = d.MaxDistanceIndex;
                    if (a->FinalAssignment)
                    {
                        // Refresh labels and diagnostics for the final centroids;
                        // this is not another Lloyd update or convergence decision.
                        a->FinalAssignment = false;
                        a->Phase = KMeansGpuPhase::Preview;
                    }
                    else
                    {
                        ++a->Result.Iterations;
                        a->Result.Converged = (d.Changed & 1u) == 0 ||
                            d.MaxShift <=
                                a->Snapshot.Params.ConvergenceTolerance * a->Snapshot.Params.ConvergenceTolerance;
                        a->Geometry.Iterations = a->Result.Iterations;
                        a->Geometry.Converged = a->Result.Converged;
                        a->Moved = (d.Changed & 2u) != 0;
                        // The step run counts the iteration and says whether the next one may be recorded now.
                        const bool more = a->Driver.Stepped(
                            {.Values = {double(d.Inertia), double(d.MaxShift), 0.0, 0.0}},
                            a->Result.Converged ? StepOutcome::Converged : StepOutcome::Continue);
                        if (more)
                            a->Phase = a->Result.Iterations % a->Snapshot.Command.Parameters.GpuPreviewInterval == 0 &&
                                    a->Driver.MayPreview()
                                ? KMeansGpuPhase::Preview
                                : KMeansGpuPhase::Assign;
                        else if (Ended(*a))
                            BeginFinal(*a);
                        else
                        {
                            // Rests at this boundary: its preview alone (not fused with the next Assign), then nothing
                            // until the run continues, stops or is accepted.
                            a->Phase = KMeansGpuPhase::Assign;
                            a->RestPreview = a->Driver.MayPreview();
                            a->Resting = !a->RestPreview;
                            if (a->RestPreview)
                                a->Phase = KMeansGpuPhase::Preview;
                        }
                    }
                }
                a->Page.reset();
            }
            if (a->Resting)
            {
                if (a->CommitRequested || Ended(*a))
                {
                    if (a->Result.Iterations == 0u)
                    {
                        Finish(KMeansRunStatus::Cancelled, "K-Means stopped before its first iteration.");
                        return;
                    }
                    BeginFinal(*a); // Accept from a rest, or Stop while resting: labels of the current centroids
                }
                else if (!a->Driver.MayStart())
                    return;
                a->Resting = false;
            }
            if (a->Phase == KMeansGpuPhase::Preview && !a->Back)
            {
                a->Back = AcquireEditorPointScalarBack(a->Transaction);
                if (!a->Back)
                {
                    if (a->Terminal)
                    {
                        if (++a->PreviewDeferrals >= 600)
                            Finish(KMeansRunStatus::GeometryProcessingFailed,
                                "K-Means terminal preview residency remained busy for 600 retries.");
                        return;
                    }
                    a->Phase = KMeansGpuPhase::Assign; // Skip a busy interval preview without idling a frame.
                    if (std::exchange(a->RestPreview, false))
                    {
                        a->Resting = true; // a busy rest preview: rest without it
                        return;
                    }
                }
            }
            a->PreviewDeferrals = 0;
            const bool terminalPreview =
                a->Terminal && (a->Phase == KMeansGpuPhase::Preview || a->Phase == KMeansGpuPhase::Presentation);
            const auto bytes =
                terminalPreview ? std::uint64_t(a->Clusters) * 12 : std::uint64_t(sizeof(KMeansGpuDiagnostics));
            a->Recorded = false;
            a->Page = a->Context.SpatialIndices->QueueGpuCompute(
                bytes,
                [this, alive = Alive, a](RHI::ICommandContext& cmd, const SpatialGpuIndexView&)
                {
                    return *alive ? Record(a, cmd) : RHI::BufferHandle{};
                },
                SpatialGpuLatency::Immediate);
            if (!a->Page)
            {
                Finish(KMeansRunStatus::GeometryProcessingFailed, "K-Means page submission rejected.");
                return;
            }
            ++a->Result.GpuSubmissions;
        }
    };
    ClusteringGpuState::ClusteringGpuState(RHI::IDevice& d, RHI::BufferManager&, RHI::ITransferQueue&)
        : m_Impl(std::make_unique<Impl>(d))
    {
    }
    ClusteringGpuState::~ClusteringGpuState() = default;
    ClusteringGpuSubmission ClusteringGpuState::Start(KMeansSnapshot& s,
        const EditorProcessingContext& c,
        StepRunsService& steps,
        std::function<bool()> current,
        std::function<EditorCommandStatus(const Geometry::KMeans::KMeansResult&)> publish)
    {
        return m_Impl->Start(s, c, steps, std::move(current), std::move(publish));
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
    std::string ClusteringGpuState::ReadinessDiagnostic(
        const RunKMeans& request, const EditorProcessingContext& context) const
    {
        return m_Impl->ReadinessDiagnostic(request, context);
    }
}

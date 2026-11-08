module;
#include <functional>
#include <algorithm>
#include <atomic>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <utility>
#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>
#include "Modules/KeypointAnalysis/Runtime.KeypointPaging.TestSupport.hpp"
module Extrinsic.Runtime.PointAnalysisOperations;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.Private.WorkspaceLease;
import Extrinsic.Graphics.PointScalarAnalysis;
import Extrinsic.Graphics.PointKeypoints;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.CommandContext;
import Extrinsic.Runtime.GpuPropertyBinding;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.SelectionController;
import Geometry.Properties;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Core.Error;
import Geometry.PointCloud.Features;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.StepRuns;
#include "Editor/internal/Runtime.EditorProcessingAccess.hpp"
#include "Editor/internal/Runtime.EditorPendingGpuOutput.hpp"
#include "Editor/internal/Runtime.EditorGeometryHelpers.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.PointFields.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.JobFailure.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.OneShotCpuRun.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.GpuScalar.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.RadiusRows.hpp"

namespace Extrinsic::Runtime
{
    template std::shared_ptr<Graphics::PointKeypointWorkspace>
    SpatialIndexCache::LeaseGpuWorkspace<Graphics::PointKeypointWorkspace>();

    namespace
    {
        namespace GS = ECS::Components::GeometrySources;
        namespace Features = Geometry::PointCloud::Features;
        using D = GeometryElementDomain;
        using GeometryProcessingDetail::PointPropertyWatch;
        using GeometryProcessingDetail::ObserveGeometryProperty;
        using GeometryProcessingDetail::GeometryPropertiesCurrent;
        struct KeypointWork : GeometryProcessingDetail::PointInputCapture
        {
            KeypointAnalysisConfig Config{};
            entt::entity Entity{};
            PointPropertyWatch MaskWatch{}, ScoreWatch{};
            GeometryScalarPropertySnapshot BeforeMask{}, BeforeScore{};
            std::vector<std::uint32_t> AfterMask{};
            GeometryProcessingDetail::PointRadiusRows Rows{};
            std::vector<float> AfterScore{};
            std::shared_ptr<const SpatialIndexSnapshot> Index{};
            SpatialIndexHandle GpuIndex{};
            bool Abandoned{}, Admitted{};
            std::weak_ptr<EditorPointScalarTransaction> Transaction{};
            std::shared_ptr<Graphics::PointKeypointWorkspace> Workspace{};
            std::optional<Graphics::GpuPropertyView> Input{}, ScoreBase{}, MaskBase{};
            std::optional<EditorPointScalarBack> Back{};
            Graphics::PointKeypointPage Page{};
            KeypointPagingLimits Limits{};
            Graphics::PointKeypointHeader Header{};
            std::optional<EditorKeypointAnalysisResult> MainFailure{};
            EditorKeypointAnalysisResult Result{};
            // ARCH-023: the resident run's failure (the transaction's producer) and the result as admitted (a restart
            // begins from it). Its Init (owner) retains the index view and captures what the worker's pages record
            // with (the views without their leases); the worker writes only Page, Header, Failure and its counters,
            // which the owner reads once the step ended.
            std::string Failure{};
            EditorKeypointAnalysisResult Initial{};
            // ARCH-023, Vulkan LBVH as a step run: the radius-row session prepared at the start (paged by the worker
            // step, no frame) and the run's discard, which a waiting page observes.
            std::shared_ptr<SpatialQuerySession> Session{};
            std::atomic<bool> Abort{false};
            std::shared_ptr<SpatialGpuIndexLease> IndexView{};
            struct WorkerView
            {
                std::shared_ptr<Graphics::PointKeypointWorkspace> Workspace{};
                std::shared_ptr<SpatialGpuIndexLease> Index{};
                Graphics::GpuPropertyView Input{}, Typed{}, Companion{};
                std::optional<RHI::BufferHandle> ScoreBase{}, MaskBase{};
                std::uint32_t Batches{};
                std::uint64_t ReadbackBytes{};
            } Worker{};
        };
        bool CurrentInput(const EditorProcessingContext& context, const KeypointWork& w)
        {
            const std::array outputs{w.MaskWatch, w.ScoreWatch};
            return GeometryProcessingDetail::EditorProcessingContextWorldCurrent(context) &&
                GeometryPropertiesCurrent(context, w.Entity, w.Inputs) &&
                GeometryPropertiesCurrent(context, w.Entity, outputs);
        }
        enum class CapturePurpose
        {
            Execute,
            Readiness
        };
        std::shared_ptr<KeypointWork> Capture(const EditorProcessingContext& context,
            KeypointAnalysisConfig c,
            std::string& diagnostic,
            CapturePurpose purpose = CapturePurpose::Execute)
        {
            auto fail = [&](std::string why) -> std::shared_ptr<KeypointWork>
            {
                diagnostic = std::move(why);
                return {};
            };
            const auto validation = ValidateKeypointAnalysisConfigSection(
                SerializeKeypointAnalysisConfig(c), {}, kKeypointAnalysisConfigSectionName);
            if (!validation.Usable())
                return fail(validation.Diagnostics.front().Message);
            if (!GeometryProcessingDetail::EditorProcessingContextWorldCurrent(context) || !context.Scene)
                return fail("Scene is unavailable.");
            const auto entity = EditorFeatureDetail::ResolveStableEntity(context.Scene->Raw(), c.StableEntityId);
            if (!entity)
                return fail("Keypoint target entity is stale or missing.");
            const auto a = BuildGeometryAvailability(context.Scene->Raw(), *entity);
            auto w = std::make_shared<KeypointWork>();
            const bool captured = purpose == CapturePurpose::Execute
                ? GeometryProcessingDetail::CapturePointInput(a, c.Positions, true, *w, diagnostic)
                : GeometryProcessingDetail::PreparePointInput(context, *entity, a, c.Positions, *w, diagnostic);
            if (!captured)
                return {};
            if (c.Mask.Domain == D::Unknown)
                c.Mask.Domain = c.Positions.Domain;
            if (c.Score.Domain == D::Unknown)
                c.Score.Domain = c.Positions.Domain;
            const std::array outputs{c.Mask, c.Score};
            if (purpose == CapturePurpose::Readiness)
            {
                const auto pending =
                    GeometryProcessingDetail::PendingGpuOutputReadiness(context, c.StableEntityId, outputs);
                if (!pending.Enabled)
                    return fail(pending.DisabledReason);
            }
            if (!GeometryProcessingDetail::ValidatePointOutputs(a, c.Positions, outputs, "Keypoint", diagnostic))
                return {};
            const auto* props = ResolveGeometryPropertySet(a, c.Positions.Domain);
            w->Config = c;
            w->Entity = *entity;
            w->Result.RequestedBackend = c.Backend;
            w->Result.ImplementationId = c.Backend == KeypointAnalysisBackend::VulkanCompute
                ? "vulkan.keypoints.resident.paged.v1"
                : "geometry.keypoints.centroid_pca";
            w->Result.Mask = c.Mask;
            w->Result.Score = c.Score;
            w->Result.SlotCount = w->SlotCount;
            w->Result.LiveCount = w->LiveCount;
            w->MaskWatch = ObserveGeometryProperty(a, c.Mask.Domain, c.Mask.Name);
            w->ScoreWatch = ObserveGeometryProperty(a, c.Score.Domain, c.Score.Name);
            if (!w->Result.LiveCount)
                return fail("Keypoint analysis requires live input samples.");
            if (w->Result.LiveCount < 2 || c.MinimumNeighbors >= w->Result.LiveCount)
                return fail(
                    "Keypoint analysis requires positive spacing and more live samples than minimum neighbors.");
            if (c.Backend != KeypointAnalysisBackend::CpuKDTree)
            {
                if (!context.SpatialIndices || !w->ValidLbvh || w->Result.LiveCount > (1u << 24) ||
                    std::max(c.SalientRadius, c.NonMaxRadius) > Geometry::PointLBVH::CoordinateLimit)
                    return fail(
                        "LBVH needs the spatial cache, at most 2^24 samples and coordinates/radii within 1e18.");
                if ((c.Backend == KeypointAnalysisBackend::VulkanLBVH ||
                        c.Backend == KeypointAnalysisBackend::VulkanCompute) &&
                    (!context.JobCommands.Available() || !context.SpatialIndices->GpuQueriesAvailable() ||
                        w->Result.LiveCount > (1u << 20)))
                    return fail("Vulkan keypoints require GPU queries/jobs and at most 2^20 samples.");
                // ARCH-023: Vulkan LBVH neighbourhoods are a worker step run; a composition without the session's
                // step runs cannot run them (there is no frame-paced substitute).
                if (c.Backend == KeypointAnalysisBackend::VulkanLBVH && !context.StepRuns)
                    return fail("Vulkan LBVH keypoints require the session's step runs.");
                if (c.Backend == KeypointAnalysisBackend::VulkanCompute &&
                    (!context.Device || !context.Device->SupportsShaderFloat64()))
                    return fail("Vulkan keypoint computation requires shader double-precision support.");
            }
            if (purpose == CapturePurpose::Execute)
            {
                w->BeforeMask = CaptureGeometryScalarProperty(*props, c.Mask);
                w->BeforeScore = CaptureGeometryScalarProperty(*props, c.Score);
                w->AfterMask.resize(props->Size());
                w->AfterScore.resize(props->Size());
            }
            return w;
        }
        Features::KeypointParams Parameters(const KeypointAnalysisConfig& c)
        {
            return {.SalientRadius = c.SalientRadius,
                .NonMaxRadius = c.NonMaxRadius,
                .Gamma21 = c.Gamma21,
                .Gamma32 = c.Gamma32,
                .MinNeighbors = c.MinimumNeighbors};
        }
        void Prepare(KeypointWork& w)
        {
            const auto started = std::chrono::steady_clock::now();
            w.Result.Status = EditorCommandStatus::GeometryProcessingFailed;
            const auto scale = Features::ResolveKeypointScale(w.Points, Parameters(w.Config));
            if (!scale)
            {
                w.Result.Message = "Keypoint scale requires positive spacing and representable radii.";
                return;
            }
            if (w.Config.Backend != KeypointAnalysisBackend::CpuKDTree &&
                std::max(scale->SalientRadius, scale->NonMaxRadius) > Geometry::PointLBVH::CoordinateLimit)
            {
                w.Result.Message = "Resolved keypoint radius exceeds the LBVH range.";
                return;
            }
            w.Result.MeanSpacing = scale->MeanSpacing;
            w.Result.SalientRadius = scale->SalientRadius;
            w.Result.NonMaxRadius = scale->NonMaxRadius;
            w.Result.CpuComputeMilliseconds =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
            w.Result.Status = EditorCommandStatus::Pending;
        }
        bool AppendRow(KeypointWork& w, std::vector<std::uint32_t>& row)
        {
            const bool appended = GeometryProcessingDetail::AppendPointRadiusRow(w.Rows, row, w.Result.Message);
            w.Result.MaximumNeighbors = std::max(w.Result.MaximumNeighbors, w.Rows.MaximumNeighbors);
            return appended;
        }
        void Compute(KeypointWork& w)
        {
            const auto started = std::chrono::steady_clock::now();
            auto& r = w.Result;
            const auto& c = w.Config;
            r.ActualBackend = ToString(c.Backend);
            if (c.Backend == KeypointAnalysisBackend::CpuLBVH)
                Prepare(w);
            if (r.Status == EditorCommandStatus::GeometryProcessingFailed)
                return;
            r.Status = EditorCommandStatus::GeometryProcessingFailed;
            std::optional<Features::KeypointAnalysis> analysis;
            if (c.Backend == KeypointAnalysisBackend::CpuKDTree)
                analysis = Features::AnalyzeKeypoints(w.Points, Parameters(c));
            else
            {
                if (c.Backend == KeypointAnalysisBackend::CpuLBVH)
                {
                    std::vector<std::uint32_t> row;
                    const auto radius = std::max(r.SalientRadius, r.NonMaxRadius);
                    for (std::uint32_t i = 0; i < w.Points.size(); ++i)
                    {
                        const auto neighbors =
                            w.Index->Index.Radius(w.Points[i], radius, std::uint32_t(w.Points.size() - 1), i);
                        if (neighbors.Overflowed() || neighbors.TotalCount != neighbors.Neighbors.size())
                        {
                            r.Message = "Incomplete CPU keypoint radius support.";
                            return;
                        }
                        row.clear();
                        for (const auto& n : neighbors.Neighbors)
                            row.push_back(n.Index);
                        if (!AppendRow(w, row))
                            return;
                    }
                }
                analysis = Features::AnalyzeKeypointsFromNeighbors(w.Points,
                    Parameters(c),
                    {.MeanSpacing = r.MeanSpacing, .SalientRadius = r.SalientRadius, .NonMaxRadius = r.NonMaxRadius},
                    {w.Rows.Offsets, w.Rows.Indices});
            }
            if (!analysis)
            {
                r.Message = "Keypoint analysis rejected invalid support or unrepresentable covariance.";
                return;
            }
            for (std::size_t i = 0; i < w.Slots.size(); ++i)
            {
                w.AfterMask[w.Slots[i]] = analysis->Mask[i];
                w.AfterScore[w.Slots[i]] = analysis->Saliency[i];
            }
            r.KeypointCount = analysis->Keypoints.Indices.size();
            r.WrittenCount = w.Slots.size();
            r.MeanSpacing = analysis->Scale.MeanSpacing;
            r.SalientRadius = analysis->Scale.SalientRadius;
            r.NonMaxRadius = analysis->Scale.NonMaxRadius;
            r.Status = EditorCommandStatus::Applied;
            r.CpuComputeMilliseconds =
                (c.Backend == KeypointAnalysisBackend::VulkanLBVH ? r.CpuComputeMilliseconds : 0) +
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
            r.Message = "Keypoint mask and saliency computed with " + r.ActualBackend +
                " neighborhoods; scale, covariance and suppression run on CPU.";
        }
        // ARCH-023, the step run's worker step for Vulkan LBVH, before Compute: the scale, then every radius row
        // through the prepared session (the shared radius-page decoder). False with the result's reason when
        // the scale or a page failed, or the run's discard aborted the rows.
        bool QueryRowsOnWorker(KeypointWork& w)
        {
            Prepare(w);
            if (w.Result.Status == EditorCommandStatus::GeometryProcessingFailed)
                return false;
            std::string diagnostic;
            const auto state = GeometryProcessingDetail::ComputePointRadiusRows(*w.Session,
                w.Slots,
                std::max(w.Result.SalientRadius, w.Result.NonMaxRadius),
                0,
                w.Abort,
                w.Rows,
                diagnostic);
            w.Result.MaximumNeighbors = w.Rows.MaximumNeighbors;
            w.Result.GpuQueryBatches = w.Session->Pages();
            w.Result.GpuNeighborhoodMilliseconds = w.Rows.Milliseconds;
            if (w.Rows.Queried)
                w.Result.ActualBackend = "vulkan_lbvh";
            if (state == GeometryProcessingDetail::RowsState::Ready)
                return true;
            w.Result.Status = state == GeometryProcessingDetail::RowsState::Pending
                ? EditorCommandStatus::StaleEntity
                : EditorCommandStatus::GeometryProcessingFailed;
            w.Result.Message = state == GeometryProcessingDetail::RowsState::Pending
                ? "Keypoint analysis was discarded or its input changed; nothing was applied."
                : std::move(diagnostic);
            return false;
        }
        EditorKeypointAnalysisResult Publish(
            const EditorProcessingContext& context, const std::shared_ptr<KeypointWork>& w)
        {
            auto& r = w->Result;
            if (w->Abandoned || !CurrentInput(context, *w))
            {
                r.Status = EditorCommandStatus::StaleEntity;
                r.Message = "Keypoint input or output changed before publication.";
                return r;
            }
            if (r.Status != EditorCommandStatus::Applied)
                return r;
            const std::array outputs{GeometryProcessingDetail::PointScalarOutput{.Output = w->Config.Mask,
                                         .Watch = w->MaskWatch,
                                         .Before = &w->BeforeMask,
                                         .AfterUInt = w->AfterMask},
                GeometryProcessingDetail::PointScalarOutput{.Output = w->Config.Score,
                    .Watch = w->ScoreWatch,
                    .Before = &w->BeforeScore,
                    .After = w->AfterScore}};
            const auto status =
                GeometryProcessingDetail::PublishPointScalarField(context, w->Entity, *w, outputs, "Detect keypoints");
            r.Status = status == EditorCommandHistoryStatus::InvalidCommand
                ? EditorCommandStatus::InvalidProcessingParameters
                : EditorFeatureDetail::ToEditorCommandStatus(status);
            if (!r.Succeeded())
                r.Message = status == EditorCommandHistoryStatus::InvalidCommand
                    ? "Output values are not exactly representable in the selected scalar storage."
                    : "Keypoint publication rejected by history checks.";
            return r;
        }
        // ARCH-023: the neighbourhood backends as a one-shot worker step run (the shared helper, OneShotCpuRun.hpp)
        // with the caller's execution: a batch runs at once and accepts at its end, an interactive start honours the
        // section's start and auto_accept; Commit publishes once through the scalar history. Vulkan LBVH pages its
        // radius rows on the device inside that step (QueryRowsOnWorker, the session prepared before the start),
        // between its CPU scale and covariance; a discard aborts a waiting page at once. Without the step-run service
        // the CPU backends keep the job below (a batch only); Vulkan LBVH is refused at capture.
        EditorKeypointAnalysisResult StartKeypointRun(const EditorProcessingContext& context,
            const std::shared_ptr<KeypointWork>& w,
            const EditorJobIdentity& identity,
            const GeometryProcessingDetail::MeshSupport::QueuedJobDelivery<EditorKeypointAnalysisResult>& delivery,
            const StepRunExecution& execution)
        {
            namespace MS = GeometryProcessingDetail::MeshSupport;
            return MS::StartOneShotCpuRun<KeypointWork, EditorKeypointAnalysisResult>(
                context,
                {.Label = "Keypoint analysis",
                    .Owner = "Runtime.KeypointAnalysis",
                    .Identity = identity,
                    .Execution = execution,
                    .Abort =
                        [w]
                    {
                        w->Abort.store(true, std::memory_order_release); // a waiting device page gives up
                    },
                    .MetricNames = {"keypoints", "", "", ""},
                    .Metric =
                        [w](StepRunMetric& metric)
                    {
                        metric.Values[0] = double(w->Result.KeypointCount);
                    }},
                w,
                // From the result as captured (the helper restores it): the rows of this generation only.
                [](KeypointWork& work)
                {
                    work.Rows = {};
                    // A failed page keeps its reason (failed); an aborted one belongs to a run already discarded.
                    if (!work.Session || QueryRowsOnWorker(work))
                        Compute(work);
                },
                [context](const KeypointWork& work)
                {
                    return CurrentInput(context, work);
                },
                [context](const std::shared_ptr<KeypointWork>& work)
                {
                    return Publish(context, work);
                },
                delivery);
        }
    }
    EditorPointScalarTransactionHandle StartEditorKeypointAnalysisTransaction(const EditorProcessingCommands& commands,
        const KeypointAnalysisConfig& config,
        EditorKeypointAnalysisResult& result,
        std::function<void(EditorKeypointAnalysisResult)> sink,
        bool automatic)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        std::string why;
        auto w = Capture(context, config, why);
        result = {.Status = EditorCommandStatus::InvalidProcessingParameters,
            .RequestedBackend = config.Backend,
            .Message = why};
        if (!w)
            return {};
        result = w->Result;
        const auto refuse = [&](std::string message) -> EditorPointScalarTransactionHandle
        {
            result.Status = EditorCommandStatus::GeometryProcessingFailed;
            result.Message = std::move(message);
            return {};
        };
        if (config.Backend != KeypointAnalysisBackend::VulkanCompute)
            return refuse("Resident keypoints require the Vulkan compute backend.");
        const EditorJobIdentity identity{.EntityId = config.StableEntityId,
            .Scope = ToEditorJobScope(w->Config.Mask.Domain),
            .OutputSemantic = GeometryPresentationSlotSemantic::ScalarField,
            .OutputName = w->Config.Mask.Name,
            .RequestedDomain = EditorJobDomainOfBackend(ToString(config.Backend))};
        // A duplicate is Pending like every queued editor job; the active run keeps its callback.
        if (auto busy =
                GeometryProcessingDetail::MeshSupport::ActiveOutputJobRefusal(context, identity, "Keypoint analysis"))
        {
            result.Status = EditorCommandStatus::Pending;
            result.Message = std::move(busy->Message);
            return {};
        }
        auto scoreOutput = w->Config.Score, maskOutput = w->Config.Mask;
        scoreOutput.ValueKind = Geometry::PropertyValueKind::Float;
        maskOutput.ValueKind = Geometry::PropertyValueKind::UInt32;
        auto* residency = context.SpatialIndices->PropertyResidency();
        if (!residency)
            return refuse("Keypoint residency unavailable.");
        const auto uploads = residency->Stats().UploadBytes;
        w->Input = ResolveGpuPropertyInput(*residency, *context.Scene, context.World, w->Entity, w->Config.Positions);
        if (!w->Input)
            return refuse("Keypoint resident input acquisition refused.");
        w->Result.GpuInputUploadBytes = residency->Stats().UploadBytes - uploads;
        w->Result.GpuInputCacheHits = w->Result.GpuInputUploadBytes ? 0 : 1;
        if (w->ScoreWatch.Revision && w->Config.Score.ValueKind == scoreOutput.ValueKind)
            w->ScoreBase =
                ResolveGpuPropertyInput(*residency, *context.Scene, context.World, w->Entity, w->Config.Score);
        if (w->MaskWatch.Revision && w->Config.Mask.ValueKind == maskOutput.ValueKind)
            w->MaskBase = ResolveGpuPropertyInput(*residency, *context.Scene, context.World, w->Entity, w->Config.Mask);
        if ((w->ScoreWatch.Revision && w->Config.Score.ValueKind == scoreOutput.ValueKind && !w->ScoreBase) ||
            (w->MaskWatch.Revision && w->Config.Mask.ValueKind == maskOutput.ValueKind && !w->MaskBase))
            return refuse("Keypoint output base acquisition refused.");
        w->Result.CpuStageUploadBytes = residency->Stats().UploadBytes - uploads - w->Result.GpuInputUploadBytes;
        const auto indexState = GeometryProcessingDetail::AcquirePointIndex(*context.SpatialIndices,
            context.World,
            w->Entity,
            w->Config.Positions,
            w->Slots,
            w->Points,
            w->GpuIndex,
            w->Index,
            w->Result.IndexReused,
            why,
            false); // the resident kernels read the device hierarchy only
        if (indexState == GeometryProcessingDetail::PointIndexState::Unavailable ||
            indexState == GeometryProcessingDetail::PointIndexState::Mismatched)
            return refuse(why);
        w->Result.Status = EditorCommandStatus::Pending;
        w->Result.Message = automatic || config.Execution.Start == StepRunStart::Run
            ? "Keypoint traversal queued; score preview follows suppression."
            : "Keypoint analysis is ready; run or step it.";
        EditorPointScalarTransactionSnapshot initial;
        auto transaction = BeginEditorPointScalarPublication(commands,
            {.EntityId = SelectionController::ToStableEntityId(w->Entity),
                .Count = std::uint32_t(w->SlotCount),
                .Output = scoreOutput,
                .Label = "Detect keypoints",
                .Current =
                    [context, w]
                {
                    return !w->Abandoned && CurrentInput(context, *w);
                },
                .Companion = maskOutput,
                .PublishPair =
                    [context, w](std::span<const std::byte> score, std::span<const std::byte> mask)
                {
                    std::memcpy(w->AfterScore.data(), score.data(), score.size());
                    std::memcpy(w->AfterMask.data(), mask.data(), mask.size());
                    w->Result.Status = EditorCommandStatus::Applied;
                    w->Result.WrittenCount = w->Slots.size();
                    return Publish(context, w).Status;
                }},
            initial,
            [w, sink = GuardEditorProcessingResult(context, std::move(sink))](auto snapshot)
            {
                w->Input.reset();
                w->ScoreBase.reset();
                w->MaskBase.reset();
                w->Back.reset();
                w->Workspace.reset();
                w->IndexView.reset();
                if (w->Result.Status != EditorCommandStatus::GeometryProcessingFailed &&
                    w->Result.Status != EditorCommandStatus::StaleEntity)
                {
                    w->Result.Status = snapshot.Status;
                    w->Result.Message = snapshot.Message;
                }
                w->Result.CpuStageReadbackBytes += snapshot.CpuStageReadbackBytes;
                w->Result.StepRun = snapshot.StepRun;
                w->Abandoned = true;
                if (w->Admitted && sink)
                    sink(w->Result);
            });
        if (!transaction)
            return refuse(initial.Message);
        w->Transaction = transaction;
        w->Limits = KeypointPagingForTesting;
        w->Limits.Pairs = std::clamp(w->Limits.Pairs, 1u, 1u << 24u);
        w->Limits.Visits = std::clamp(w->Limits.Visits, 1u, std::min(1024u, std::max(1u, w->Limits.Pairs)));
        w->Limits.Rows =
            std::max(1u, std::min({w->Limits.Rows, std::max(1u, w->Limits.Pairs / w->Limits.Visits), 16384u}));
        w->Workspace = context.SpatialIndices->LeaseGpuWorkspace<Graphics::PointKeypointWorkspace>();
        if (!w->Workspace)
        {
            DiscardEditorPointScalar(commands, transaction);
            return refuse("Keypoint device workspace unavailable.");
        }
        w->Initial = w->Result;
        // One page per submission; the device header says whether the page resumes, the stage advances or the
        // traversal ended. Commands only (a worker records it): the retained index view and the captured views.
        const auto recordPage = [w](RHI::ICommandContext& cmd)
        {
            auto& worker = w->Worker;
            for (const auto buffer : worker.Index->Barriers())
                cmd.BufferBarrier(buffer,
                    RHI::MemoryAccess::ShaderWrite | RHI::MemoryAccess::TransferWrite,
                    RHI::MemoryAccess::ShaderRead);
            if (w->Page.Mode == 4 && w->Page.First == 0)
            {
                const auto initialize =
                    [&](const std::optional<RHI::BufferHandle>& base, const RHI::BufferHandle output)
                {
                    if (base)
                    {
                        cmd.BufferBarrier(*base,
                            RHI::MemoryAccess::TransferWrite | RHI::MemoryAccess::ShaderWrite,
                            RHI::MemoryAccess::TransferRead);
                        cmd.CopyBuffer(*base, output, 0, 0, w->SlotCount * 4);
                    }
                    else
                        cmd.FillBuffer(output, 0, w->SlotCount * 4, 0);
                };
                initialize(worker.ScoreBase, worker.Typed.Buffer);
                initialize(worker.MaskBase, worker.Companion.Buffer);
                for (auto b : {worker.Typed.Buffer, worker.Companion.Buffer})
                    cmd.BufferBarrier(b, RHI::MemoryAccess::TransferWrite, RHI::MemoryAccess::ShaderWrite);
            }
            const auto& c = w->Config;
            const auto& index = worker.Index->View();
            return worker.Workspace->RecordPage(cmd,
                index.NodesBDA,
                worker.Input,
                index.OriginalSlotsBDA,
                index.Count,
                {c.SalientRadius, c.NonMaxRadius, c.Gamma21, c.Gamma32, c.MinimumNeighbors},
                w->Page,
                worker.Typed,
                worker.Companion);
        };
        // A completed page: true once the traversal ended (or the device reported an error).
        const auto observePage = [w]
        {
            auto& p = w->Page;
            if (w->Header.Error)
                return true;
            if (w->Header.Reserved)
            {
                p.Resume = 1;
                return false;
            }
            p.Resume = 0;
            p.First += p.Rows;
            if (p.First >= w->LiveCount || p.Mode == 1)
            {
                p.First = 0;
                if (p.Mode == 4)
                    return true;
                p.Mode = p.Mode == 0 ? 5 : p.Mode == 5 ? 1 : p.Mode == 1 ? 2 : p.Mode + 1;
            }
            if (p.Mode == 4 || p.Mode == 5)
                p.Rows = std::uint32_t(w->LiveCount);
            else
                p.Rows = std::min(w->Limits.Rows, std::uint32_t(w->LiveCount) - p.First);
            return false;
        };
        const std::weak_ptr<EditorPointScalarTransaction> weak = transaction;
        const auto strip = [](Graphics::GpuPropertyView view)
        {
            view.Lease.reset(); // the owner keeps the lease
            return view;
        };
        // ARCH-023: the paged traversal is the transaction's one logical step (pages bound each submission), run on
        // the step run's worker; the step run decides when it starts, and Accept, Discard and Restart go through the
        // shared controls.
        EditorPointScalarProducer producer{.Prepare =
                                               [w, weak, context, strip]
            {
                const auto t = weak.lock();
                w->Back = t ? AcquireEditorPointScalarBack(t) : std::nullopt;
                if (!w->Back)
                {
                    w->Failure = "Keypoint output rings unavailable.";
                    return false;
                }
                std::string why;
                if (!w->IndexView)
                    w->IndexView = context.SpatialIndices->PrepareGpuIndex(w->GpuIndex, why);
                if (!w->IndexView)
                {
                    w->Failure = why;
                    return false;
                }
                const auto refusals = context.SpatialIndices->Stats().ScratchRefusals;
                if (!w->Workspace || !w->Workspace->Prepare(w->IndexView->View().Count))
                {
                    w->Failure = context.SpatialIndices->MethodScratchDiagnostic(
                        refusals, "Keypoint device workspace unavailable.");
                    return false;
                }
                auto& worker = w->Worker;
                worker = {.Workspace = w->Workspace,
                    .Index = w->IndexView,
                    .Input = strip(*w->Input),
                    .Typed = strip(w->Back->Typed),
                    .Companion = strip(w->Back->Companion),
                    .ScoreBase = w->ScoreBase ? std::optional{w->ScoreBase->Buffer} : std::nullopt,
                    .MaskBase = w->MaskBase ? std::optional{w->MaskBase->Buffer} : std::nullopt};
                w->Header = {};
                w->Page = {.Rows = std::min(w->Limits.Rows, std::uint32_t(w->LiveCount)), .Visits = w->Limits.Visits};
                return true;
            },
            .Step = [w, recordPage, observePage](const EditorPointScalarPage& page) -> std::string
            {
                for (;;)
                {
                    ++w->Worker.Batches;
                    if (!page(recordPage, std::as_writable_bytes(std::span{&w->Header, 1u})))
                        return w->Failure = "Keypoint GPU submission failed.";
                    w->Worker.ReadbackBytes += sizeof(w->Header);
                    if (observePage())
                        return {};
                }
            },
            .Outcome = [w, weak]() -> std::string
            {
                auto& r = w->Result;
                r.GpuQueryBatches += std::exchange(w->Worker.Batches, 0u);
                r.CpuStageReadbackBytes += std::exchange(w->Worker.ReadbackBytes, 0u);
                if (!w->Failure.empty())
                    return w->Failure;
                r.ActualBackend = "vulkan_compute";
                if (w->Header.Error)
                    return "Keypoints rejected invalid scale, covariance or spatial traversal.";
                r.MeanSpacing = w->Header.MeanSpacing;
                r.SalientRadius = w->Header.SalientRadius;
                r.NonMaxRadius = w->Header.NonMaxRadius;
                r.MaximumNeighbors = w->Header.MaximumNeighbors;
                r.KeypointCount = w->Header.KeypointCount;
                w->Back.reset();
                const auto t = weak.lock();
                return t && PublishEditorPointScalarBack(t, false) ? std::string{}
                                                                   : std::string{"Keypoint ring publication failed."};
            },
            .Reset =
                [w]
            {
                w->Back.reset();
                w->Failure.clear();
                w->Header = {};
                w->Worker.Batches = 0u;
                w->Worker.ReadbackBytes = 0u;
                w->Result = w->Initial;
            },
            .Uses =
                [w](std::vector<RHI::BufferHandle>& buffers)
            {
                for (const auto* view : {&w->Input, &w->ScoreBase, &w->MaskBase})
                    if (*view)
                        buffers.push_back((*view)->Buffer);
                if (w->Back)
                    for (const auto& view : {w->Back->Typed, w->Back->Presentation, w->Back->Companion})
                        if (view.Buffer.IsValid())
                            buffers.push_back(view.Buffer);
            },
            // No step runs any more: the worker's workspace and index references return with the run (the sink
            // dropped the owner's already).
            .Release =
                [w]
            {
                w->Worker = {};
            }};
        EditorJobIdentity runIdentity{.EntityId = config.StableEntityId,
            .Scope = ToEditorJobScope(w->Config.Mask.Domain),
            .OutputSemantic = GeometryPresentationSlotSemantic::ScalarField,
            .OutputName = w->Config.Mask.Name,
            .RequestedDomain = EditorJobDomainOfBackend(ToString(config.Backend))};
        // A batch (automatic) runs at once and accepts at its end; an interactive start keeps the config's choice.
        const StepRunExecution execution = automatic ? kOneShotBatchExecution : OneShotExecution(config.Execution);
        w->Admitted = true;
        std::string rejected;
        if (!SubmitEditorPointScalarRun(transaction,
                std::move(runIdentity),
                execution,
                std::move(producer),
                "Resident paged keypoints",
                rejected))
        {
            w->Admitted = false;
            return refuse(std::move(rejected));
        }
        result = w->Result;
        result.StepRun = SnapshotEditorPointScalar(commands, transaction).StepRun;
        w->Initial.StepRun = result.StepRun;
        return transaction;
    }
    ActionReadiness PreviewEditorKeypointAnalysisCommand(
        const EditorProcessingCommands& commands, const KeypointAnalysisConfig& config)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        std::string diagnostic;
        const auto work = Capture(context, config, diagnostic, CapturePurpose::Readiness);
        return {bool(work), std::move(diagnostic)};
    }
    // Apply's run (a batch) or a start's (`execution`, the section's): Vulkan compute as its resident transaction,
    // the other backends as a one-shot worker step run of the session's service; without that service (or
    // without a job lane) only a batch runs, as one queued job (or inline).
    EditorKeypointAnalysisResult RunKeypointAnalysis(const EditorProcessingCommands& commands,
        KeypointAnalysisConfig config,
        std::function<void(EditorKeypointAnalysisResult)> onComplete,
        const StepRunExecution& execution)
    {
        if (config.Backend == KeypointAnalysisBackend::VulkanCompute)
        {
            config.Execution = execution;
            EditorKeypointAnalysisResult result;
            (void)StartEditorKeypointAnalysisTransaction(commands, config, result, std::move(onComplete));
            return result;
        }
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        std::string diagnostic;
        auto w = Capture(context, config, diagnostic);
        const auto report = [&](EditorCommandStatus status, std::string message)
        {
            auto result = w ? w->Result
                            : EditorKeypointAnalysisResult{
                                  .RequestedBackend = config.Backend, .Mask = config.Mask, .Score = config.Score};
            result.Status = status;
            result.Message = std::move(message);
            return result;
        };
        if (!w)
            return report(EditorCommandStatus::InvalidProcessingParameters, diagnostic);
        const std::array outputs{w->Config.Mask, w->Config.Score};
        const auto pending =
            GeometryProcessingDetail::PendingGpuOutputReadiness(context, config.StableEntityId, outputs);
        if (!pending.Enabled)
            return report(EditorCommandStatus::InvalidProcessingParameters, pending.DisabledReason);
        if (w->Config.Backend != KeypointAnalysisBackend::CpuKDTree)
        {
            const auto indexState = GeometryProcessingDetail::AcquirePointIndex(*context.SpatialIndices,
                context.World,
                w->Entity,
                w->Config.Positions,
                w->Slots,
                w->Points,
                w->GpuIndex,
                w->Index,
                w->Result.IndexReused,
                diagnostic,
                w->Config.Backend == KeypointAnalysisBackend::CpuLBVH); // Vulkan LBVH rows build no CPU hierarchy
            if (indexState == GeometryProcessingDetail::PointIndexState::Unavailable)
                return report(EditorCommandStatus::InvalidProcessingParameters, diagnostic);
            if (indexState == GeometryProcessingDetail::PointIndexState::Mismatched)
                return report(EditorCommandStatus::StaleEntity, "Keypoint index does not match the selected samples.");
        }
        const bool batch = execution.Start == StepRunStart::Run && execution.AutoAccept != StepRunAutoAccept::Never;
        if (!batch && (!context.StepRuns || !context.JobCommands.Available()))
            return report(EditorCommandStatus::InvalidProcessingParameters,
                "Keypoint analysis needs the step-run service and a job lane for its run controls; none is composed.");
        if (!context.JobCommands.Available())
        {
            Compute(*w);
            return Publish(context, w);
        }
        EditorJobIdentity identity{.EntityId = config.StableEntityId,
            .Scope = ToEditorJobScope(w->Config.Mask.Domain),
            .OutputSemantic = GeometryPresentationSlotSemantic::ScalarField,
            .OutputName = w->Config.Mask.Name,
            .RequestedDomain = EditorJobDomainOfBackend(ToString(config.Backend))};
        namespace MS = GeometryProcessingDetail::MeshSupport;
        if (auto busy = MS::ActiveOutputJobRefusal(context, identity, "Keypoint analysis"))
            return report(EditorCommandStatus::Pending, std::move(busy->Message)); // the active job owns the callback
        if (context.StepRuns && w->Config.Backend == KeypointAnalysisBackend::VulkanLBVH)
        {
            // The radius-row session, once, on the main thread: resident index, buffers, pipelines, the captured
            // points as queries excluding their own rows; the radii come with the step.
            w->Session = context.SpatialIndices->PrepareQuerySession(w->GpuIndex,
                {.Radius = true,
                    .Capacity =
                        GeometryProcessingDetail::PointRadiusCapacity(w->Config.GpuRadiusCapacity, w->Points.size(), 0),
                    .PageQueries = w->Config.GpuQueryBatchSize,
                    .Queries = w->Points,
                    .Excluded = w->Slots},
                diagnostic);
            if (!w->Session)
                return report(EditorCommandStatus::GeometryProcessingFailed,
                    "Vulkan keypoint radius support could not be prepared: " + diagnostic);
        }
        // Once submitted, Result belongs to the running job. A submission failure reports from the delivery's
        // immutable snapshot.
        const MS::QueuedJobDelivery<EditorKeypointAnalysisResult> delivery{context,
            std::move(onComplete),
            report(EditorCommandStatus::Pending, "Keypoint analysis queued."),
            "Keypoint analysis"};
        if (context.StepRuns)
            return StartKeypointRun(context, w, identity, delivery, execution);
        const auto validate = [context, w]
        {
            return MS::ValidateQueuedJob(w->Abandoned, CurrentInput(context, *w));
        };
        JobDesc desc{.DebugName = "Keypoint covariance and suppression",
            .Scope = context.World,
            .Work =
                [w](const JobCancellation&)
            {
                Compute(*w);
                return JobResultEnvelope::Make(true);
            },
            .ValidateBeforeApply = validate,
            .PublishCompletion =
                [context, w, delivery](KernelEventBus&, const JobResultEnvelope&)
            {
                return delivery.Publish(Publish(context, w));
            },
            .FinalizeUnpublishedOnMainThread =
                [w, delivery]
            {
                w->Abandoned = true;
                delivery.Finalize(w->MainFailure);
            }};
        const auto token = context.JobCommands.Submit(std::move(desc), identity);
        if (!token.IsValid())
        {
            w->Abandoned = true;
            return delivery.Rejected();
        }
        return delivery.Pending();
    }
    EditorKeypointAnalysisResult ApplyEditorKeypointAnalysisCommand(const EditorProcessingCommands& commands,
        const KeypointAnalysisConfig& config,
        std::function<void(EditorKeypointAnalysisResult)> onComplete)
    {
        return RunKeypointAnalysis(commands, config, std::move(onComplete), kOneShotBatchExecution);
    }
    EditorKeypointAnalysisResult StartEditorKeypointAnalysisRun(const EditorProcessingCommands& commands,
        const KeypointAnalysisConfig& config,
        std::function<void(EditorKeypointAnalysisResult)> onComplete)
    {
        return RunKeypointAnalysis(commands, config, std::move(onComplete), config.Execution);
    }
    EditorKeypointAnalysisResult ApplyEditorConfiguredKeypointAnalysis(
        const EditorProcessingCommands& commands, std::function<void(EditorKeypointAnalysisResult)> onComplete)
    {
        const auto config = GetEditorKeypointAnalysisConfig(commands);
        if (!config)
            return {.Status = EditorCommandStatus::InvalidProcessingParameters,
                .Message = "Keypoint config is unavailable."};
        return ApplyEditorKeypointAnalysisCommand(commands, *config, std::move(onComplete));
    }
    EditorKeypointAnalysisResult StartEditorConfiguredKeypointAnalysis(
        const EditorProcessingCommands& commands, std::function<void(EditorKeypointAnalysisResult)> onComplete)
    {
        const auto config = GetEditorKeypointAnalysisConfig(commands);
        if (!config)
            return {.Status = EditorCommandStatus::InvalidProcessingParameters,
                .Message = "Keypoint config is unavailable."};
        return StartEditorKeypointAnalysisRun(commands, *config, std::move(onComplete));
    }
} // namespace Extrinsic::Runtime

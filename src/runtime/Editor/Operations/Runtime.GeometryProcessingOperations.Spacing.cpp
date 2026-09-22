module;
#include <string_view>
#include <functional>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include <utility>
#include <glm/vec3.hpp>
#include <entt/entity/registry.hpp>
module Extrinsic.Runtime.PointFieldOperations;
import Extrinsic.Core.Error;
import Geometry.PointCloud.Utils;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.KernelEvents;
import Geometry.Properties;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
#include "Editor/internal/Runtime.EditorProcessingAccess.hpp"
#include "Editor/internal/Runtime.EditorGeometryHelpers.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.PointFields.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.RadiusRows.hpp"

namespace Extrinsic::Runtime
{
    namespace
    {
        namespace PC = Geometry::PointCloud;
        using namespace GeometryProcessingDetail;
        struct SpacingWork : PointScalarCapture
        {
            PointSpacingConfig Config{};
            entt::entity Entity{};
            std::shared_ptr<const SpatialIndexSnapshot> Index{};
            SpatialIndexHandle GpuIndex{};
            PointKnnRows Neighbors{};
            bool Abandoned{};
            EditorPointSpacingResult Result{};
        };
        bool CurrentInput(const EditorProcessingContext& context, const SpacingWork& w)
        {
            return PointScalarFieldCurrent(context, w.Entity, w);
        }
        enum class CapturePurpose { Execute, Readiness };
        std::shared_ptr<SpacingWork> Capture(const EditorProcessingContext& context,
            PointSpacingConfig c, std::string& diagnostic, CapturePurpose purpose = CapturePurpose::Execute)
        {
            auto fail = [&](std::string why) -> std::shared_ptr<SpacingWork> { diagnostic = std::move(why); return {}; };
            const auto validation = ValidatePointSpacingConfigSection(
                SerializePointSpacingConfig(c), {}, kPointSpacingConfigSectionName);
            if (!validation.Usable()) return fail(validation.Diagnostics.front().Message);
            if (!context.Scene) return fail("Scene is unavailable.");
            const auto entity = EditorFeatureDetail::ResolveStableEntity(context.Scene->Raw(), c.StableEntityId);
            if (!entity) return fail("Radii target entity is stale or missing.");
            const auto a = BuildGeometryAvailability(context.Scene->Raw(), *entity);
            auto w = std::make_shared<SpacingWork>();
            if (!CapturePointScalarField(context, *entity, a, c.Positions, c.Radii, "Radii",
                                        purpose == CapturePurpose::Execute, *w, diagnostic)) return {};
            w->Config = c; w->Entity = *entity;
            w->Result.RequestedBackend = c.Backend;
            w->Result.Radii = c.Radii;
            w->Result.SlotCount = w->SlotCount; w->Result.LiveCount = w->LiveCount;
            if (w->Result.LiveCount < 2) return fail("Point spacing requires at least two live samples.");
            if (c.Backend != PointSpacingBackend::CpuOctree)
            {
                if (!context.SpatialIndices || !w->ValidLbvh || w->Result.LiveCount > (1u << 24))
                    return fail("LBVH requires the spatial cache, at most 2^24 samples and coordinates within 1e18.");
                if (c.Backend == PointSpacingBackend::VulkanLBVH)
                {
                    if (!context.JobCommands.Available() || !context.SpatialIndices->GpuQueriesAvailable())
                        return fail("Vulkan radii neighborhoods require the framed spatial cache and job service.");
                    if (w->Result.LiveCount > (1u << 20) ||
                        c.KNeighbors > 63)
                        return fail("Vulkan radii queries support at most 2^20 live samples and k<=63 (64 candidates including self).");
                }
            }
            return w;
        }
        void Compute(SpacingWork& w)
        {
            const auto started = std::chrono::steady_clock::now();
            const auto& c = w.Config;
            auto& r = w.Result;
            r.Status = EditorCommandStatus::GeometryProcessingFailed;
            r.ActualBackend = ToString(c.Backend);
            const PC::RadiusEstimationParams params{.KNeighbors=c.KNeighbors, .ScaleFactor=c.ScaleFactor};
            std::optional<PC::RadiusEstimationResult> analysis;
            if (c.Backend == PointSpacingBackend::CpuOctree)
                analysis = PC::EstimateRadii(w.Points, params);
            else
            {
                const auto width=std::min<std::size_t>(w.Points.size(),std::max<std::size_t>(c.KNeighbors,1)+1);
                if (c.Backend == PointSpacingBackend::CpuLBVH &&
                    !AppendPointKnnRows(*w.Index, w.Points, std::uint32_t(width), w.Neighbors.Indices, r.Message))
                    return;
                analysis = PC::EstimateRadiiFromNeighbors(w.Points,w.Neighbors.Indices,params);
            }
            if (!analysis || analysis->Radii.size()!=w.Slots.size())
            {r.Message="Point spacing failed: invalid neighborhoods or unrepresentable float distances/radii.";return;}
            r.MeanRadius=analysis->AverageRadius;r.MinRadius=analysis->MinRadius;r.MaxRadius=analysis->MaxRadius;
            r.Centroid=analysis->Statistics.Centroid;
            r.AverageSpacing=analysis->Statistics.AverageSpacing;
            r.MinSpacing=analysis->Statistics.MinSpacing;
            r.MaxSpacing=analysis->Statistics.MaxSpacing;
            r.BoundingBoxDiagonal=analysis->Statistics.BoundingBoxDiagonal;
            for (std::size_t i=0;i<w.Slots.size();++i) w.AfterValues[w.Slots[i]]=analysis->Radii[i];
            r.WrittenCount=w.Slots.size();r.Status=EditorCommandStatus::Applied;
            r.CpuComputeMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
            r.Message="Radii computed using "+r.ActualBackend+" neighborhoods and CPU spacing/radius evaluation.";
        }
        bool AdvanceGpu(const EditorProcessingContext& context, SpacingWork& w)
        {
            if (w.Abandoned || !CurrentInput(context, w))
            {
                w.Result.Status = EditorCommandStatus::StaleEntity;
                w.Result.Message = "Radii inputs changed or the job was cancelled.";
                w.Neighbors.Batch.reset();
                return true;
            }
            const auto width = std::uint32_t(std::min<std::size_t>(
                w.Points.size(), std::max<std::size_t>(w.Config.KNeighbors, 1) + 1));
            const auto state = AdvancePointKnnRows(*context.SpatialIndices, w.GpuIndex,
                w.Points, w.Slots, width, w.Config.GpuQueryBatchSize, w.Neighbors, w.Result.Message);
            w.Result.GpuQueryBatches = w.Neighbors.QueryBatches;
            w.Result.GpuNeighborhoodMilliseconds = w.Neighbors.Milliseconds;
            if (state == KnnRowsState::Failed) w.Result.Status = EditorCommandStatus::GeometryProcessingFailed;
            return state != KnnRowsState::Pending;
        }
        EditorPointSpacingResult Publish(const EditorProcessingContext& context,
                                            const std::shared_ptr<SpacingWork>& w)
        {
            auto& r=w->Result;
            if (!CurrentInput(context,*w))
            {r.Status=EditorCommandStatus::StaleEntity; r.Message="Radii input or output changed before publication."; return r;}
            if (r.Status!=EditorCommandStatus::Applied) return r;
            const auto status = PublishPointScalarField(context, w->Entity, *w, "Estimate point spacing and radii");
            r.Status = status == EditorCommandHistoryStatus::InvalidCommand
                ? EditorCommandStatus::InvalidProcessingParameters
                : EditorFeatureDetail::ToEditorCommandStatus(status);
            if (!r.Succeeded()) r.Message = status == EditorCommandHistoryStatus::InvalidCommand
                ? "Output values are not exactly representable in the selected scalar storage."
                : "Radii publication rejected by history checks.";
            return r;
        }
    }
    ActionReadiness PreviewEditorPointSpacingCommand(
        const EditorProcessingCommands& commands,const PointSpacingConfig& config)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        std::string diagnostic;
        const auto work = Capture(context, config, diagnostic, CapturePurpose::Readiness);
        return {bool(work), std::move(diagnostic)};
    }
    GeometryPropertyCatalogSnapshot GetEditorPointSpacingInputCatalog(
        const EditorProcessingCommands& commands,std::uint32_t id)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        return GeometryProcessingDetail::BuildPointInputCatalog(context, id, 2);
    }
    EditorPointSpacingResult ApplyEditorPointSpacingCommand(
        const EditorProcessingCommands &commands, const PointSpacingConfig &config, std::function<void(EditorPointSpacingResult)> onComplete)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        std::string diagnostic;
        auto w = Capture(context, config, diagnostic);
        const auto report = [&config, &w](EditorCommandStatus status, std::string message) {
            auto result = w ? w->Result
                            : EditorPointSpacingResult{.RequestedBackend = config.Backend, .Radii = config.Radii};
            result.Status = status;
            result.Message = std::move(message);
            return result;
        };
        if (!w)
            return report(EditorCommandStatus::InvalidProcessingParameters, std::move(diagnostic));
        if (w->Config.Backend != PointSpacingBackend::CpuOctree)
        {
            const auto indexState = GeometryProcessingDetail::AcquirePointIndex(
                *context.SpatialIndices, context.World, w->Entity, w->Config.Positions,
                w->Slots, w->Points, w->GpuIndex, w->Index, w->Result.IndexReused, diagnostic);
            if (indexState == GeometryProcessingDetail::PointIndexState::Unavailable)
                return report(EditorCommandStatus::InvalidProcessingParameters, diagnostic);
            if (indexState == GeometryProcessingDetail::PointIndexState::Mismatched)
                return report(EditorCommandStatus::StaleEntity,
                              "Radii index snapshot does not match the selected samples.");
        }
        if (!context.JobCommands.Available())
        {
            Compute(*w);
            return Publish(context, w);
        }
        const EditorJobIdentity identity{.EntityId = config.StableEntityId,
                                         .Scope = ToEditorJobScope(w->Config.Radii.Domain),
                                         .OutputSemantic = GeometryPresentationSlotSemantic::ScalarField,
                                         .OutputName = w->Config.Radii.Name};
        if (auto active = GeometryProcessingDetail::MeshSupport::FindActiveEditorJob(context, identity);
            active && IsActiveEditorJobState(active->State))
            return report(EditorCommandStatus::Pending,
                          "A radii job for this output is already active.");
        auto sink = GuardEditorProcessingResult(context, std::move(onComplete));
        auto delivered = std::make_shared<bool>(false);
        auto pending = w->Result;
        pending.Status = EditorCommandStatus::Pending;
        pending.Message = "Radii estimation queued.";
        JobDesc desc{
            .DebugName = "Radii estimation",
            .Scope = context.World,
            .Kind = RuntimeTaskKinds::GeometryProcess,
            .Work = [w](const JobCancellation &) -> JobResultEnvelope {
                Compute(*w);
                return JobResultEnvelope::Make(true);
            },
            .ValidateBeforeApply =
                [context, w] {
                    return CurrentInput(context, *w) ? JobApplyValidation::Current
                                                                 : JobApplyValidation::StaleGeneration;
                },
            .PublishCompletion =
                [context, w, sink, delivered](KernelEventBus &, const JobResultEnvelope &) {
                    auto result = Publish(context, w);
                    *delivered = true;
                    if (sink)
                        sink(result);
                    return result.Succeeded();
                },
            .FinalizeUnpublishedOnMainThread =
                [sink, delivered, w, pending]() mutable {
                    w->Abandoned = true;
                    if (sink && !*delivered)
                    {
                        if (w->Result.Status == EditorCommandStatus::GeometryProcessingFailed)
                            pending = w->Result;
                        else
                        {
                            pending.Status = EditorCommandStatus::StaleEntity;
                            pending.Message = "Radii job was cancelled or its source became stale; previous output retained.";
                        }
                        sink(std::move(pending));
                    }
                }};
        if (w->Config.Backend == PointSpacingBackend::VulkanLBVH)
        {
            JobDesc gpu{
                .DebugName = "Radii neighborhoods (Vulkan)", .Scope = context.World,
                .Kind = RuntimeTaskKinds::GeometryProcess,
                .Work = [](const JobCancellation&) { return JobResultEnvelope::Make(true); },
                .IsReadyToApply = [context, w] { return AdvanceGpu(context, *w); },
                .PublishCompletion = [w](KernelEventBus&, const JobResultEnvelope&) { return w->Neighbors.Finished; },
                .FinalizeUnpublishedOnMainThread = [w] { w->Abandoned = true; }};
            const auto prerequisite = context.JobCommands.Submit(std::move(gpu), identity);
            if (!prerequisite.IsValid())
                return report(EditorCommandStatus::GeometryProcessingFailed, "GPU radii job submission was rejected.");
            desc.DependsOn.push_back({prerequisite, "Complete Vulkan radii neighborhoods before CPU spacing/radius evaluation"});
        }
        const auto token = context.JobCommands.Submit(std::move(desc), identity);
        if (!token.IsValid())
        {
            w->Abandoned = true;
            pending.Status = EditorCommandStatus::GeometryProcessingFailed;
            pending.Message = "Radii job submission was rejected.";
        }
        return pending;
    }
    EditorPointSpacingResult ApplyEditorConfiguredPointSpacing(
        const EditorProcessingCommands &commands, std::function<void(EditorPointSpacingResult)> onComplete)
    {
        const auto config = GetEditorPointSpacingConfig(commands);
        if (!config)
            return {.Status = EditorCommandStatus::InvalidProcessingParameters,
                    .Message = "Radii estimation config is unavailable."};
        return ApplyEditorPointSpacingCommand(commands, *config, std::move(onComplete));
    }
} // namespace Extrinsic::Runtime

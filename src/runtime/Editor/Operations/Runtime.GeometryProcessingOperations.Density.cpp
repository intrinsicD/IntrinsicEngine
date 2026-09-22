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
        struct DensityWork : PointScalarCapture
        {
            KernelDensityConfig Config{};
            entt::entity Entity{};
            std::shared_ptr<const SpatialIndexSnapshot> Index{};
            SpatialIndexHandle GpuIndex{};
            PointKnnRows Neighbors{};
            bool Abandoned{};
            EditorKernelDensityResult Result{};
        };
        bool CurrentInput(const EditorProcessingContext& context, const DensityWork& w)
        {
            return PointScalarFieldCurrent(context, w.Entity, w);
        }
        enum class CapturePurpose { Execute, Readiness };
        std::shared_ptr<DensityWork> Capture(const EditorProcessingContext& context,
            KernelDensityConfig c, std::string& diagnostic, CapturePurpose purpose = CapturePurpose::Execute)
        {
            auto fail = [&](std::string why) -> std::shared_ptr<DensityWork> { diagnostic = std::move(why); return {}; };
            const auto validation = ValidateKernelDensityConfigSection(
                SerializeKernelDensityConfig(c), {}, kKernelDensityConfigSectionName);
            if (!validation.Usable()) return fail(validation.Diagnostics.front().Message);
            if (!context.Scene) return fail("Scene is unavailable.");
            const auto entity = EditorFeatureDetail::ResolveStableEntity(context.Scene->Raw(), c.StableEntityId);
            if (!entity) return fail("Density target entity is stale or missing.");
            const auto a = BuildGeometryAvailability(context.Scene->Raw(), *entity);
            auto w = std::make_shared<DensityWork>();
            if (!CapturePointScalarField(context, *entity, a, c.Positions, c.Density, "Density",
                                        purpose == CapturePurpose::Execute, *w, diagnostic)) return {};
            w->Config = c; w->Entity = *entity;
            w->Result.RequestedBackend = c.Backend;
            w->Result.Density = c.Density;
            w->Result.SlotCount = w->SlotCount; w->Result.LiveCount = w->LiveCount;
            if (w->Result.LiveCount < 2) return fail("Kernel density requires at least two live samples.");
            if (c.Backend != KernelDensityBackend::CpuOctree)
            {
                if (!context.SpatialIndices || !w->ValidLbvh || w->Result.LiveCount > (1u << 24))
                    return fail("LBVH requires the spatial cache, at most 2^24 samples and coordinates within 1e18.");
                if (c.Backend == KernelDensityBackend::VulkanLBVH)
                {
                    if (!context.JobCommands.Available() || !context.SpatialIndices->GpuQueriesAvailable())
                        return fail("Vulkan density neighborhoods require the framed spatial cache and job service.");
                    if (w->Result.LiveCount > (1u << 20) ||
                        c.KNeighbors > 63)
                        return fail("Vulkan density queries support at most 2^20 live samples and k<=63 (64 candidates including self).");
                }
            }
            return w;
        }
        void Compute(DensityWork& w)
        {
            const auto started = std::chrono::steady_clock::now();
            const auto& c = w.Config;
            auto& r = w.Result;
            r.Status = EditorCommandStatus::GeometryProcessingFailed;
            r.ActualBackend = ToString(c.Backend);
            const PC::KDEParams params{.KNeighbors=c.KNeighbors, .Bandwidth=c.Bandwidth};
            std::optional<PC::KDEResult> analysis;
            if (c.Backend == KernelDensityBackend::CpuOctree)
                analysis = PC::EstimateKernelDensity(w.Points, params);
            else
            {
                const auto width=std::min<std::size_t>(w.Points.size(),std::max<std::size_t>(c.KNeighbors,2)+1);
                if (c.Backend == KernelDensityBackend::CpuLBVH &&
                    !AppendPointKnnRows(*w.Index, w.Points, std::uint32_t(width), w.Neighbors.Indices, r.Message))
                    return;
                analysis = PC::EstimateKernelDensityFromNeighbors(w.Points,w.Neighbors.Indices,params);
            }
            if (!analysis || analysis->Densities.size()!=w.Slots.size())
            {r.Message="Kernel density failed: invalid neighborhoods or unrepresentable float kernel values/bandwidth.";return;}
            r.UsedBandwidth=analysis->UsedBandwidth;r.MeanDensity=analysis->MeanDensity;
            r.MinDensity=analysis->MinDensity;r.MaxDensity=analysis->MaxDensity;
            for (std::size_t i=0;i<w.Slots.size();++i) w.AfterValues[w.Slots[i]]=analysis->Densities[i];
            r.WrittenCount=w.Slots.size();r.Status=EditorCommandStatus::Applied;
            r.CpuComputeMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
            r.Message="Density computed using "+r.ActualBackend+" neighborhoods and CPU bandwidth/Gaussian evaluation.";
        }
        bool AdvanceGpu(const EditorProcessingContext& context, DensityWork& w)
        {
            if (w.Abandoned || !CurrentInput(context, w))
            {
                w.Result.Status = EditorCommandStatus::StaleEntity;
                w.Result.Message = "Density inputs changed or the job was cancelled.";
                w.Neighbors.Batch.reset();
                return true;
            }
            const auto width = std::uint32_t(std::min<std::size_t>(
                w.Points.size(), std::max<std::size_t>(w.Config.KNeighbors, 2) + 1));
            const auto state = AdvancePointKnnRows(*context.SpatialIndices, w.GpuIndex,
                w.Points, w.Slots, width, w.Config.GpuQueryBatchSize, w.Neighbors, w.Result.Message);
            w.Result.GpuQueryBatches = w.Neighbors.QueryBatches;
            w.Result.GpuNeighborhoodMilliseconds = w.Neighbors.Milliseconds;
            if (state == KnnRowsState::Failed) w.Result.Status = EditorCommandStatus::GeometryProcessingFailed;
            return state != KnnRowsState::Pending;
        }
        EditorKernelDensityResult Publish(const EditorProcessingContext& context,
                                            const std::shared_ptr<DensityWork>& w)
        {
            auto& r=w->Result;
            if (!CurrentInput(context,*w))
            {r.Status=EditorCommandStatus::StaleEntity; r.Message="Density input or output changed before publication."; return r;}
            if (r.Status!=EditorCommandStatus::Applied) return r;
            const auto status = PublishPointScalarField(context, w->Entity, *w, "Estimate kernel density");
            r.Status = status == EditorCommandHistoryStatus::InvalidCommand
                ? EditorCommandStatus::InvalidProcessingParameters
                : EditorFeatureDetail::ToEditorCommandStatus(status);
            if (!r.Succeeded()) r.Message = status == EditorCommandHistoryStatus::InvalidCommand
                ? "Output values are not exactly representable in the selected scalar storage."
                : "Density publication rejected by history checks.";
            return r;
        }
    }
    ActionReadiness PreviewEditorKernelDensityCommand(
        const EditorProcessingCommands& commands,const KernelDensityConfig& config)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        std::string diagnostic;
        const auto work = Capture(context, config, diagnostic, CapturePurpose::Readiness);
        return {bool(work), std::move(diagnostic)};
    }
    GeometryPropertyCatalogSnapshot GetEditorKernelDensityInputCatalog(
        const EditorProcessingCommands& commands,std::uint32_t id)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        return GeometryProcessingDetail::BuildPointInputCatalog(context, id, 2);
    }
    EditorKernelDensityResult ApplyEditorKernelDensityCommand(
        const EditorProcessingCommands &commands, const KernelDensityConfig &config, std::function<void(EditorKernelDensityResult)> onComplete)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        std::string diagnostic;
        auto w = Capture(context, config, diagnostic);
        const auto report = [&config, &w](EditorCommandStatus status, std::string message) {
            auto result = w ? w->Result
                            : EditorKernelDensityResult{.RequestedBackend = config.Backend, .Density = config.Density};
            result.Status = status;
            result.Message = std::move(message);
            return result;
        };
        if (!w)
            return report(EditorCommandStatus::InvalidProcessingParameters, std::move(diagnostic));
        if (w->Config.Backend != KernelDensityBackend::CpuOctree)
        {
            const auto indexState = GeometryProcessingDetail::AcquirePointIndex(
                *context.SpatialIndices, context.World, w->Entity, w->Config.Positions,
                w->Slots, w->Points, w->GpuIndex, w->Index, w->Result.IndexReused, diagnostic);
            if (indexState == GeometryProcessingDetail::PointIndexState::Unavailable)
                return report(EditorCommandStatus::InvalidProcessingParameters, diagnostic);
            if (indexState == GeometryProcessingDetail::PointIndexState::Mismatched)
                return report(EditorCommandStatus::StaleEntity,
                              "Density index snapshot does not match the selected samples.");
        }
        if (!context.JobCommands.Available())
        {
            Compute(*w);
            return Publish(context, w);
        }
        const EditorJobIdentity identity{.EntityId = config.StableEntityId,
                                         .Scope = ToEditorJobScope(w->Config.Density.Domain),
                                         .OutputSemantic = GeometryPresentationSlotSemantic::ScalarField,
                                         .OutputName = w->Config.Density.Name};
        if (auto active = GeometryProcessingDetail::MeshSupport::FindActiveEditorJob(context, identity);
            active && IsActiveEditorJobState(active->State))
            return report(EditorCommandStatus::Pending,
                          "A density job for this output is already active.");
        auto sink = GuardEditorProcessingResult(context, std::move(onComplete));
        auto delivered = std::make_shared<bool>(false);
        auto pending = w->Result;
        pending.Status = EditorCommandStatus::Pending;
        pending.Message = "Density estimation queued.";
        JobDesc desc{
            .DebugName = "Density estimation",
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
                            pending.Message = "Density job was cancelled or its source became stale; previous output retained.";
                        }
                        sink(std::move(pending));
                    }
                }};
        if (w->Config.Backend == KernelDensityBackend::VulkanLBVH)
        {
            JobDesc gpu{
                .DebugName = "Density neighborhoods (Vulkan)", .Scope = context.World,
                .Kind = RuntimeTaskKinds::GeometryProcess,
                .Work = [](const JobCancellation&) { return JobResultEnvelope::Make(true); },
                .IsReadyToApply = [context, w] { return AdvanceGpu(context, *w); },
                .PublishCompletion = [w](KernelEventBus&, const JobResultEnvelope&) { return w->Neighbors.Finished; },
                .FinalizeUnpublishedOnMainThread = [w] { w->Abandoned = true; }};
            const auto prerequisite = context.JobCommands.Submit(std::move(gpu), identity);
            if (!prerequisite.IsValid())
                return report(EditorCommandStatus::GeometryProcessingFailed, "GPU density job submission was rejected.");
            desc.DependsOn.push_back({prerequisite, "Complete Vulkan density neighborhoods before CPU bandwidth/Gaussian evaluation"});
        }
        const auto token = context.JobCommands.Submit(std::move(desc), identity);
        if (!token.IsValid())
        {
            w->Abandoned = true;
            pending.Status = EditorCommandStatus::GeometryProcessingFailed;
            pending.Message = "Density job submission was rejected.";
        }
        return pending;
    }
    EditorKernelDensityResult ApplyEditorConfiguredKernelDensity(
        const EditorProcessingCommands &commands, std::function<void(EditorKernelDensityResult)> onComplete)
    {
        const auto config = GetEditorKernelDensityConfig(commands);
        if (!config)
            return {.Status = EditorCommandStatus::InvalidProcessingParameters,
                    .Message = "Density estimation config is unavailable."};
        return ApplyEditorKernelDensityCommand(commands, *config, std::move(onComplete));
    }
} // namespace Extrinsic::Runtime

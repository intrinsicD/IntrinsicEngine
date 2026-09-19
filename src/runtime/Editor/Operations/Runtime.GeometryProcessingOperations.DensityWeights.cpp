module;
#include <functional>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>
module Extrinsic.Runtime.PointAnalysisOperations;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Core.Error;
import Geometry.PointCloud.Utils;
import Geometry.PointCloud.Kernels;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.JobService;
#include "Editor/internal/Runtime.EditorProcessingAccess.hpp"
#include "Editor/internal/Runtime.EditorGeometryHelpers.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.PointFields.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.RadiusRows.hpp"
namespace Extrinsic::Runtime
{
    namespace
    {
        namespace Kernels=Geometry::PointCloud::Kernels;
        namespace Detail=GeometryProcessingDetail;
        struct DensityWeightWork : Detail::PointScalarCapture
        {
            DensityWeightConfig Config{};
            entt::entity Entity{};
            Detail::PointRadiusRows Rows{};
            std::shared_ptr<const SpatialIndexSnapshot> Index{};
            SpatialIndexHandle GpuIndex{};
            bool Abandoned{};
            std::optional<EditorDensityWeightResult> MainFailure{};
            EditorDensityWeightResult Result{};
        };
        bool Current(const EditorProcessingContext& context,const DensityWeightWork& w)
        {
            return Detail::PointScalarFieldCurrent(context, w.Entity, w);
        }
        enum class Purpose { Execute, Readiness };
        std::shared_ptr<DensityWeightWork> Capture(const EditorProcessingContext& context,
            DensityWeightConfig c,std::string& diagnostic,Purpose purpose=Purpose::Execute)
        {
            const auto fail=[&](std::string why)->std::shared_ptr<DensityWeightWork>{diagnostic=std::move(why);return {};};
            const auto validation=ValidateDensityWeightConfigSection(SerializeDensityWeightConfig(c),{},kDensityWeightConfigSectionName);
            if(!validation.Usable())return fail(validation.Diagnostics.front().Message);
            if(!context.Scene)return fail("Scene is unavailable.");
            const auto entity=EditorFeatureDetail::ResolveStableEntity(context.Scene->Raw(),c.StableEntityId);
            if(!entity)return fail("Density-weight target is stale or missing.");
            const auto a=BuildGeometryAvailability(context.Scene->Raw(),*entity);
            auto w = std::make_shared<DensityWeightWork>();
            if (!Detail::CapturePointScalarField(context, *entity, a, c.Positions, c.Weights, "Weight",
                                                 purpose == Purpose::Execute, *w, diagnostic)) return {};
            w->Config = c; w->Entity = *entity;
            w->Result.RequestedBackend = c.Backend; w->Result.Weights = c.Weights;
            w->Result.SlotCount = w->SlotCount; w->Result.LiveCount = w->LiveCount;
            if(!w->Result.LiveCount)return fail("Density weights require at least one live sample.");
            const auto radius=Kernels::ConservativeQueryRadius(c.SupportRadius);
            if(!radius)return fail("Invalid density support radius.");w->Result.QueryRadius=*radius;
            if(c.Backend!=DensityWeightBackend::CpuKDTree)
            {
                if(!context.SpatialIndices || !w->ValidLbvh || w->Result.LiveCount>(1u<<24) || *radius>Geometry::PointLBVH::CoordinateLimit)
                    return fail("LBVH requires the spatial cache, at most 2^24 points, and coordinates/expanded radius within 1e18.");
                if(c.Backend==DensityWeightBackend::VulkanLBVH)
                {
                    // The current shader clamp needs ordered AABB endpoints even
                    // when the device flushes subnormal values independently.
                    if(w->HasSubnormalCoordinates)return fail("Vulkan density weights require normal or zero coordinate components; subnormal coordinates are unsupported.");
                    if(!context.JobCommands.Available() || !context.SpatialIndices->GpuQueriesAvailable() || w->Result.LiveCount>(1u<<20))
                        return fail("Vulkan density weights require framed GPU queries/jobs and at most 2^20 live samples.");
                }
            }
            return w;
        }
        void Compute(DensityWeightWork& w)
        {
            const auto started=std::chrono::steady_clock::now();auto& r=w.Result;const auto& c=w.Config;
            r.Status=EditorCommandStatus::GeometryProcessingFailed;r.ActualBackend=ToString(c.Backend);
            Kernels::DensityWeightResult result;
            if(c.Backend==DensityWeightBackend::CpuKDTree)
                result=Kernels::ComputeDensityWeights(w.Points,c.SupportRadius,c.Kernel,c.Mode);
            else
            {
                if(c.Backend==DensityWeightBackend::CpuLBVH)
                {
                    std::vector<std::uint32_t> row;
                    for(std::uint32_t i=0;i<w.Points.size();++i)
                    {
                        const auto support=w.Index->Index.Radius(w.Points[i],r.QueryRadius,
                            std::uint32_t(std::max<std::size_t>(1,w.Points.size()-1)),i);
                        if(support.Neighbors.size()!=support.TotalCount){r.Message="Incomplete CPU radius candidates.";return;}
                        row.clear();for(const auto& n:support.Neighbors)row.push_back(n.Index);
                        if(!Detail::AppendPointRadiusRow(w.Rows,row,r.Message))return;
                    }
                    r.MaximumNeighbors=w.Rows.MaximumNeighbors;
                }
                result=Kernels::ComputeDensityWeightsFromNeighbors(w.Points,{w.Rows.Offsets,w.Rows.Indices},c.SupportRadius,c.Kernel,c.Mode);
            }
            if(!result.Succeeded()){r.Message="Density weights failed: "+std::string(Kernels::DebugName(result.Status));return;}
            for(std::size_t i=0;i<w.Slots.size();++i)w.AfterValues[w.Slots[i]]=result.Weights[i];
            const auto [minimum,maximum]=std::minmax_element(result.Weights.begin(),result.Weights.end());
            r.MinWeight=*minimum;r.MaxWeight=*maximum;r.Diagnostics=result.Diagnostics;r.WrittenCount=w.Slots.size();
            r.Status=EditorCommandStatus::Applied;
            r.CpuComputeMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
            r.Message="Density weights computed with "+r.ActualBackend+" radius candidates and CPU double-precision kernel reduction.";
        }
        bool AdvanceGpu(const EditorProcessingContext& context,DensityWeightWork& w)
        {
            if(w.Abandoned || !Current(context,w))
            {w.Result.Status=EditorCommandStatus::StaleEntity;w.Result.Message="Density input changed or the job was cancelled.";w.MainFailure=w.Result;w.Rows.Batch.reset();return true;}
            std::string diagnostic;
            const auto state=Detail::AdvancePointRadiusRows(*context.SpatialIndices,w.GpuIndex,w.Points,w.Slots,
                w.Result.QueryRadius,w.Config.GpuQueryBatchSize,w.Config.GpuRadiusCapacity,0,w.Rows,diagnostic);
            w.Result.MaximumNeighbors=w.Rows.MaximumNeighbors;w.Result.GpuQueryBatches=w.Rows.QueryBatches;
            w.Result.GpuNeighborhoodMilliseconds=w.Rows.Milliseconds;
            if(w.Rows.Queried)w.Result.ActualBackend="vulkan_lbvh";
            if(state==Detail::RadiusRowsState::Failed)
            {w.Result.Status=EditorCommandStatus::GeometryProcessingFailed;w.Result.Message=std::move(diagnostic);w.MainFailure=w.Result;}
            return state!=Detail::RadiusRowsState::Pending;
        }
        EditorDensityWeightResult Publish(const EditorProcessingContext& context,const std::shared_ptr<DensityWeightWork>& w)
        {
            auto& r=w->Result;
            if(w->Abandoned || !Current(context,*w))
            {r.Status=EditorCommandStatus::StaleEntity;r.Message="Density input or output changed before publication.";return r;}
            if(r.Status!=EditorCommandStatus::Applied)return r;
            const auto status = Detail::PublishPointScalarField(context, w->Entity, *w,
                                                               "Compute compact density weights");
            r.Status=EditorFeatureDetail::ToEditorCommandStatus(status);if(!r.Succeeded())r.Message="Density publication rejected by history checks.";return r;
        }
    }
    ActionReadiness PreviewEditorDensityWeightCommand(const EditorProcessingCommands& commands,const DensityWeightConfig& config)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        std::string diagnostic;
        const auto work = Capture(context, config, diagnostic, Purpose::Readiness);
        return {bool(work), std::move(diagnostic)};
    }
    EditorDensityWeightResult ApplyEditorDensityWeightCommand(const EditorProcessingCommands& commands,const DensityWeightConfig& config, std::function<void(EditorDensityWeightResult)> onComplete)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        std::string diagnostic;auto w=Capture(context,config,diagnostic);
        const auto report=[&](EditorCommandStatus status,std::string message)
        {auto r=w?w->Result:EditorDensityWeightResult{.RequestedBackend=config.Backend,.Weights=config.Weights};r.Status=status;r.Message=std::move(message);return r;};
        if(!w)return report(EditorCommandStatus::InvalidProcessingParameters,diagnostic);
        if(w->Config.Backend!=DensityWeightBackend::CpuKDTree)
        {
            const auto indexState = GeometryProcessingDetail::AcquirePointIndex(
                *context.SpatialIndices, context.World, w->Entity, w->Config.Positions,
                w->Slots, w->Points, w->GpuIndex, w->Index, w->Result.IndexReused, diagnostic);
            if (indexState == GeometryProcessingDetail::PointIndexState::Unavailable)
                return report(EditorCommandStatus::InvalidProcessingParameters, diagnostic);
            if (indexState == GeometryProcessingDetail::PointIndexState::Mismatched)
                return report(EditorCommandStatus::StaleEntity,
                              "Density index does not match selected samples.");
        }
        if(!context.JobCommands.Available()){Compute(*w);return Publish(context,w);}
        const EditorJobIdentity identity{.EntityId=config.StableEntityId,.Scope=ToEditorJobScope(w->Config.Weights.Domain),
            .OutputSemantic=GeometryPresentationSlotSemantic::ScalarField,.OutputName=w->Config.Weights.Name};
        if (auto active = GeometryProcessingDetail::MeshSupport::FindActiveEditorJob(context, identity);
            active && IsActiveEditorJobState(active->State))
            return report(EditorCommandStatus::Pending,"A density-weight job for this output is already active.");
        auto sink=GuardEditorProcessingResult(context, std::move(onComplete));auto delivered=std::make_shared<bool>(false);
        const auto pending=report(EditorCommandStatus::Pending,"Density weights queued.");
        const auto rejected=[pending](std::string message){auto r=pending;r.Status=EditorCommandStatus::GeometryProcessingFailed;r.Message=std::move(message);return r;};
        JobDesc desc{.DebugName="Compact density weights",.Scope=context.World,.Kind=RuntimeTaskKinds::GeometryProcess,
            .Work=[w](const JobCancellation&){Compute(*w);return JobResultEnvelope::Make(true);},
            .ValidateBeforeApply=[context,w]{return !w->Abandoned && Current(context,*w)?JobApplyValidation::Current:JobApplyValidation::StaleGeneration;},
            .PublishCompletion=[context,w,sink,delivered](KernelEventBus&,const JobResultEnvelope&)
            {auto r=Publish(context,w);*delivered=true;if(sink)sink(r);return r.Succeeded();},
            .FinalizeUnpublishedOnMainThread=[w,sink,delivered,pending]() mutable
            {
                w->Abandoned=true;if(*delivered)return;*delivered=true;auto r=pending;
                if(w->MainFailure)r=*w->MainFailure;
                else {r.Status=EditorCommandStatus::StaleEntity;r.Message="Density job cancelled or stale; previous output retained.";}
                if(sink)sink(std::move(r));
            }};
        if(w->Config.Backend==DensityWeightBackend::VulkanLBVH)
        {
            JobDesc gpu{.DebugName="Density radius support (Vulkan)",.Scope=context.World,.Kind=RuntimeTaskKinds::GeometryProcess,
                .Work=[](const JobCancellation&){return JobResultEnvelope::Make(true);},
                .IsReadyToApply=[context,w]{return AdvanceGpu(context,*w);},
                .PublishCompletion=[w](KernelEventBus&,const JobResultEnvelope&){return w->Rows.Finished;},
                .FinalizeUnpublishedOnMainThread=[w]{w->Abandoned=true;}};
            const auto support=context.JobCommands.Submit(std::move(gpu),identity);
            if(!support.IsValid())return rejected("Density GPU submission rejected.");
            desc.DependsOn.push_back({support,"Complete radius candidates before density reduction"});
        }
        const auto token=context.JobCommands.Submit(std::move(desc),identity);
        if(!token.IsValid()){w->Abandoned=true;return rejected("Density reduction submission rejected.");}
        return pending;
    }
    EditorDensityWeightResult ApplyEditorConfiguredDensityWeight(const EditorProcessingCommands& commands, std::function<void(EditorDensityWeightResult)> onComplete)
    {
        const auto config=GetEditorDensityWeightConfig(commands);
        if(!config)return {.Status=EditorCommandStatus::InvalidProcessingParameters,.Message="Density-weight config is unavailable."};
        return ApplyEditorDensityWeightCommand(commands, *config, std::move(onComplete));
    }
}

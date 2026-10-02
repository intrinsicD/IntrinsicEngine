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
import Extrinsic.Graphics.PointScalarAnalysis;
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
#include "Editor/Operations/Runtime.GeometryProcessingOperations.JobFailure.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.RadiusRows.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.GpuScalar.hpp"
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
            EditorDensityWeightResult Result{};
        };
        bool Current(const EditorProcessingContext& context,const DensityWeightWork& w)
        {
            return Detail::PointScalarFieldCurrent(context, w.Entity, w);
        }
        Graphics::PointScalarGpuParams Params(const DensityWeightConfig& c,float radius)
        { return {.Method=2,.Capacity=c.GpuRadiusCapacity,.Kernel=std::uint32_t(c.Kernel),.Inverse=std::uint32_t(c.Mode),.QueryRadius=radius,.SupportRadius=c.SupportRadius}; }
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
                    if(!Detail::AdmitPointScalarGpu(context,*w,Params(c,*radius),diagnostic))return {};
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
                            std::uint32_t(std::max<std::size_t>(1,w.Points.size()-1)),i,true);
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

        EditorDensityWeightResult Publish(const EditorProcessingContext& context,const std::shared_ptr<DensityWeightWork>& w)
        {
            auto& r=w->Result;
            if(w->Abandoned || !Current(context,*w))
            {r.Status=EditorCommandStatus::StaleEntity;r.Message="Density input or output changed before publication.";return r;}
            if(r.Status!=EditorCommandStatus::Applied)return r;
            const auto status = Detail::PublishPointScalarField(context, w->Entity, *w,
                                                               "Compute compact density weights");
            r.Status = status == EditorCommandHistoryStatus::InvalidCommand
                ? EditorCommandStatus::InvalidProcessingParameters
                : EditorFeatureDetail::ToEditorCommandStatus(status);
            if(!r.Succeeded())
                r.Message = status == EditorCommandHistoryStatus::InvalidCommand
                    ? "Output values are not exactly representable in the selected scalar storage."
                    : "Density publication rejected by history checks.";
            return r;
        }
        void FoldGpu(EditorDensityWeightResult& r,const EditorPointScalarTransactionSnapshot& s)
        {
            r.Status=s.Status;r.Message=s.Message;r.LiveCount=s.LiveCount;r.ActualBackend=s.GpuQueryBatches?"vulkan_lbvh":"";
            r.GpuInputUploadBytes=s.GpuInputUploadBytes;r.GpuInputCacheHits=s.GpuInputCacheHits;r.CpuStageReadbackBytes=s.CpuStageReadbackBytes;
            r.GpuQueryBatches=s.GpuQueryBatches;r.IndexReused=s.IndexReused;r.MinWeight=s.Statistics.Minimum;r.MaxWeight=s.Statistics.Maximum;r.MaximumNeighbors=s.Statistics.MaximumNeighbors;
            r.Diagnostics.QueryCount=r.LiveCount;r.Diagnostics.NeighborContributionCount=s.Statistics.Contributions;
            r.Diagnostics.UsedSuppliedNeighborhoods=true;
            if(s.Phase==EditorGpuTransactionPhase::Applied)r.WrittenCount=r.LiveCount;
        }
        EditorPointScalarTransactionHandle StartGpu(const EditorProcessingContext& ctx,const std::shared_ptr<DensityWeightWork>& w,
            EditorDensityWeightResult& result,std::function<void(EditorDensityWeightResult)> sink,bool automatic,Graphics::GpuPropertyResidency* test=nullptr, const EditorPointScalarTransactionSnapshot& diagnostics = {})
        {
            EditorPointScalarTransactionSnapshot state;
            auto run=Detail::StartPointScalarGpu(ctx,w,w->Entity,w->Config.StableEntityId,w->Config.Positions,Params(w->Config,w->Result.QueryRadius),"Estimate density weights","Density weights",state,
                [w,sink=std::move(sink)](EditorPointScalarTransactionSnapshot s){FoldGpu(w->Result,s);if(sink)sink(w->Result);},automatic,test,diagnostics);
            FoldGpu(w->Result,state);result=w->Result;return run;
        }
    }
    void UpdateEditorPointScalarResult(EditorDensityWeightResult& result, const EditorPointScalarTransactionSnapshot& snapshot)
    { FoldGpu(result,snapshot); }
    EditorPointScalarTransactionHandle StartEditorDensityWeightTransaction(const EditorProcessingCommands& commands,const DensityWeightConfig& config,
        EditorDensityWeightResult& result,std::function<void(EditorDensityWeightResult)> sink)
    {
        result.RequestedBackend=config.Backend;result.Weights=config.Weights;
        const auto& ctx=EditorProcessingCommandsAccess::Resolve(commands);std::string why;auto w=Capture(ctx,config,why);
        if(!w||config.Backend!=DensityWeightBackend::VulkanLBVH){result.Status=EditorCommandStatus::InvalidProcessingParameters;result.Message=w?"GPU transaction requires vulkan_lbvh.":why;return {};}
        return StartGpu(ctx,w,result,std::move(sink),false);
    }
    EditorPointScalarTransactionHandle MakeEditorDensityWeightTransactionForTest(const EditorProcessingCommands& commands,const DensityWeightConfig& config,
        std::vector<float> values,Graphics::GpuPropertyResidency& residency, const EditorPointScalarTransactionSnapshot& diagnostics)
    {
        const auto& ctx=EditorProcessingCommandsAccess::Resolve(commands);auto cpu=config;cpu.Backend=DensityWeightBackend::CpuKDTree;std::string why;
        auto w=Capture(ctx,cpu,why);if(!w||values.size()!=w->SlotCount)return {};w->Config.Backend=config.Backend;w->Result.RequestedBackend=config.Backend;w->AfterValues=std::move(values);
        EditorDensityWeightResult result;return StartGpu(ctx,w,result,{},false,&residency,diagnostics);
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
        if(w->Config.Backend==DensityWeightBackend::VulkanLBVH){EditorDensityWeightResult result;(void)StartGpu(context,w,result,std::move(onComplete),true);return result;}
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
            .OutputSemantic=GeometryPresentationSlotSemantic::ScalarField,.OutputName=w->Config.Weights.Name,
            .RequestedDomain=EditorJobDomainOfBackend(ToString(config.Backend))};
        namespace MS = GeometryProcessingDetail::MeshSupport;
        if (auto busy = MS::ActiveOutputJobRefusal(context, identity, "Density weights"))
            return report(EditorCommandStatus::Pending, std::move(busy->Message));
        const MS::QueuedJobDelivery<EditorDensityWeightResult> delivery{
            context, std::move(onComplete), report(EditorCommandStatus::Pending, "Density weights queued."), "Density weights"};
        JobDesc desc{.DebugName="Compact density weights",.Scope=context.World,.Kind=RuntimeTaskKinds::GeometryProcess,
            .Work=[w](const JobCancellation&){Compute(*w);return JobResultEnvelope::Make(true);},
            .ValidateBeforeApply=[context,w]{return MS::ValidateQueuedJob(w->Abandoned,Current(context,*w));},
            .PublishCompletion=[context,w,delivery](KernelEventBus&,const JobResultEnvelope&){return delivery.Publish(Publish(context,w));},
            .FinalizeUnpublishedOnMainThread=[w,delivery]{w->Abandoned=true;delivery.Finalize();}};
        if(!context.JobCommands.Submit(std::move(desc),identity).IsValid()){w->Abandoned=true;return delivery.Rejected();}
        return delivery.Pending();
    }
    EditorDensityWeightResult ApplyEditorConfiguredDensityWeight(const EditorProcessingCommands& commands, std::function<void(EditorDensityWeightResult)> onComplete)
    {
        const auto config=GetEditorDensityWeightConfig(commands);
        if(!config)return {.Status=EditorCommandStatus::InvalidProcessingParameters,.Message="Density-weight config is unavailable."};
        return ApplyEditorDensityWeightCommand(commands, *config, std::move(onComplete));
    }
}

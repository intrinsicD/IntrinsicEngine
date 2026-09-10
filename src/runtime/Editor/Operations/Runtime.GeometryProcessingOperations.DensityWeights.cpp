module;
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>
module Extrinsic.Runtime.GeometryProcessingOperations;
import Geometry.HalfedgeMesh;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.KernelEvents;
#include "Editor/Operations/Runtime.GeometryProcessingOperations.Internal.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.RadiusRows.hpp"
namespace Extrinsic::Runtime
{
    namespace
    {
        namespace GS=ECS::Components::GeometrySources;
        namespace Kernels=Geometry::PointCloud::Kernels;
        namespace Detail=GeometryProcessingDetail;
        using D=GeometryElementDomain;
        struct DensityWeightWork
        {
            DensityWeightConfig Config{};
            entt::entity Entity{};
            std::vector<Detail::PointPropertyWatch> Inputs{};
            Detail::PointPropertyWatch WeightWatch{};
            std::vector<glm::vec3> Points{};
            std::vector<std::uint32_t> Slots{};
            std::vector<float> Before{},After{};
            Detail::PointRadiusRows Rows{};
            std::shared_ptr<const SpatialIndexSnapshot> Index{};
            SpatialIndexHandle GpuIndex{};
            bool Abandoned{};
            std::optional<EditorDensityWeightResult> MainFailure{};
            EditorDensityWeightResult Result{};
        };
        bool Current(const EditorGeometryProcessingContext& context,const DensityWeightWork& w)
        {
            const std::array outputs{w.WeightWatch};
            return Detail::GeometryPropertiesCurrent(context,w.Entity,w.Inputs) &&
                   Detail::GeometryPropertiesCurrent(context,w.Entity,outputs);
        }
        bool NormalOrZero(float value)
        {
            const auto magnitude=std::bit_cast<std::uint32_t>(value)&0x7fffffffu;
            return magnitude==0 || magnitude>=0x00800000u;
        }
        enum class Purpose { Execute, Readiness };
        std::shared_ptr<DensityWeightWork> Capture(const EditorGeometryProcessingContext& context,
            DensityWeightConfig c,std::string& diagnostic,Purpose purpose=Purpose::Execute)
        {
            const auto fail=[&](std::string why)->std::shared_ptr<DensityWeightWork>{diagnostic=std::move(why);return {};};
            const auto validation=ValidateDensityWeightConfigSection(SerializeDensityWeightConfig(c),{},kDensityWeightConfigSectionName);
            if(!validation.Usable())return fail(validation.Diagnostics.front().Message);
            if(!context.Scene)return fail("Scene is unavailable.");
            const auto entity=Detail::ResolveEditorStableEntity(context.Scene->Raw(),c.StableEntityId);
            if(!entity)return fail("Density-weight target is stale or missing.");
            const auto a=BuildGeometryAvailability(context.Scene->Raw(),*entity);
            if(c.Positions.Domain==D::Unknown)c.Positions.Domain=Detail::PrimaryPointDomain(a);
            if(c.Weights.Domain==D::Unknown)c.Weights.Domain=c.Positions.Domain;
            const auto* props=ResolveGeometryPropertySet(a,c.Positions.Domain);
            if(!props || !ResolveGeometryProperty(a,c.Positions,props->Size(),false).Resolved())
                return fail("Choose a count-matched vec3 position property on a resolved domain.");
            if(c.Weights.Domain!=c.Positions.Domain || c.Weights.Name==c.Positions.Name)
                return fail("Weight output must be a distinct property on the position domain.");
            for(const auto* reserved:{"v:deleted","e:deleted","h:deleted","f:deleted","v:halfedge","e:v0","e:v1",
                "h:to_vertex","h:next","h:prev","h:opposite","h:face","f:halfedge","h:connectivity"})
                if(c.Weights.Name==reserved)return fail("Weight output cannot replace topology/deletion properties.");
            if(props->Exists(c.Weights.Name) && !ResolveGeometryProperty(a,c.Weights,props->Size(),false).Resolved())
                return fail("Weight output must be absent or a count-matched float property.");
            if(props->Size()>std::numeric_limits<std::uint32_t>::max())return fail("Input exceeds the supported slot range.");
            auto w=std::make_shared<DensityWeightWork>();w->Config=c;w->Entity=*entity;
            w->Result.RequestedBackend=c.Backend;w->Result.Weights=c.Weights;w->Result.SlotCount=props->Size();
            w->Inputs.push_back(Detail::ObserveGeometryProperty(a,c.Positions.Domain,c.Positions.Name));
            w->WeightWatch=Detail::ObserveGeometryProperty(a,c.Weights.Domain,c.Weights.Name);
            auto deletionDomain=c.Positions.Domain;const char* deletionName="v:deleted";std::size_t divisor=1;
            if(deletionDomain==D::MeshFace)deletionName="f:deleted";
            if(deletionDomain==D::MeshEdge || deletionDomain==D::GraphEdge)deletionName="e:deleted";
            if(deletionDomain==D::MeshHalfedge || deletionDomain==D::GraphHalfedge)
            {deletionDomain=deletionDomain==D::MeshHalfedge?D::MeshEdge:D::GraphEdge;deletionName="e:deleted";divisor=2;}
            const auto* deletionProps=ResolveGeometryPropertySet(a,deletionDomain);
            if(!deletionProps || props->Size()%divisor || deletionProps->Size()!=props->Size()/divisor)
                return fail("Invalid deletion domain/cardinality.");
            const auto deleted=deletionProps->Get<bool>(deletionName);
            if(deletionProps->Exists(deletionName) && (!deleted || deleted.Size()!=deletionProps->Size()))
                return fail("Deletion mask must be count-matched bool.");
            w->Inputs.push_back(Detail::ObserveGeometryProperty(a,deletionDomain,deletionName));
            const auto points=props->Get<glm::vec3>(c.Positions.Name);bool validLbvh=true,validGpu=true;
            for(std::uint32_t i=0;i<props->Size();++i)
            {
                if(deleted && deleted[i/divisor])continue;
                const auto point=points[i];if(!Detail::FinitePosition(point))return fail("Live positions must be finite.");
                validLbvh &= Geometry::PointLBVH::ValidPoint(point);
                validGpu &= NormalOrZero(point.x) && NormalOrZero(point.y) && NormalOrZero(point.z);
                ++w->Result.LiveCount;
                if(purpose==Purpose::Execute){w->Points.push_back(point);w->Slots.push_back(i);}
            }
            if(!w->Result.LiveCount)return fail("Density weights require at least one live sample.");
            const auto radius=Kernels::ConservativeQueryRadius(c.SupportRadius);
            if(!radius)return fail("Invalid density support radius.");w->Result.QueryRadius=*radius;
            if(c.Backend!=DensityWeightBackend::CpuKDTree)
            {
                if(!context.SpatialIndices || !validLbvh || w->Result.LiveCount>(1u<<24) || *radius>Geometry::PointLBVH::CoordinateLimit)
                    return fail("LBVH requires the spatial cache, at most 2^24 points, and coordinates/expanded radius within 1e18.");
                if(c.Backend==DensityWeightBackend::VulkanLBVH)
                {
                    // The current shader clamp needs ordered AABB endpoints even
                    // when the device flushes subnormal values independently.
                    if(!validGpu)return fail("Vulkan density weights require normal or zero coordinate components; subnormal coordinates are unsupported.");
                    if(!context.JobCommands.Available() || !context.SpatialIndices->GpuQueriesAvailable() || w->Result.LiveCount>(1u<<20))
                        return fail("Vulkan density weights require framed GPU queries/jobs and at most 2^20 live samples.");
                }
            }
            if(purpose==Purpose::Execute)
            {
                if(w->WeightWatch.Revision)w->Before=props->Get<float>(c.Weights.Name).Vector();
                w->After=w->WeightWatch.Revision?w->Before:std::vector<float>(props->Size());
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
            for(std::size_t i=0;i<w.Slots.size();++i)w.After[w.Slots[i]]=result.Weights[i];
            const auto [minimum,maximum]=std::minmax_element(result.Weights.begin(),result.Weights.end());
            r.MinWeight=*minimum;r.MaxWeight=*maximum;r.Diagnostics=result.Diagnostics;r.WrittenCount=w.Slots.size();
            r.Status=EditorCommandStatus::Applied;
            r.CpuComputeMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
            r.Message="Density weights computed with "+r.ActualBackend+" radius candidates and CPU double-precision kernel reduction.";
        }
        bool AdvanceGpu(const EditorGeometryProcessingContext& context,DensityWeightWork& w)
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
        EditorDensityWeightResult Publish(const EditorGeometryProcessingContext& context,const std::shared_ptr<DensityWeightWork>& w)
        {
            auto& r=w->Result;
            if(w->Abandoned || !Current(context,*w))
            {r.Status=EditorCommandStatus::StaleEntity;r.Message="Density input or output changed before publication.";return r;}
            if(r.Status!=EditorCommandStatus::Applied)return r;
            auto before=std::make_shared<std::vector<float>>(w->Before),after=std::make_shared<std::vector<float>>(w->After);
            auto revision=std::make_shared<Detail::PointPropertyWatch>(w->WeightWatch);
            const auto mutate=[context,entity=w->Entity,inputs=w->Inputs,c=w->Config,revision](const std::vector<float>& values,bool exists)
            {
                const std::array outputs{*revision};
                if(!Detail::GeometryPropertiesCurrent(context,entity,inputs) || !Detail::GeometryPropertiesCurrent(context,entity,outputs))return EditorCommandHistoryStatus::StaleEntity;
                auto* props=Detail::MutableGeometryProperties(context.Scene->Raw(),entity,c.Weights.Domain);
                if(exists)props->GetOrAdd<float>(c.Weights.Name).Vector()=values;
                else if(auto property=props->Get<float>(c.Weights.Name))props->Remove(property);
                revision->Revision=props->FindPropertyRevision(c.Weights.Name);
                if(context.InvalidateWorkspaceSnapshotCache)context.InvalidateWorkspaceSnapshotCache();
                return EditorCommandHistoryStatus::Applied;
            };
            const bool existed=bool(w->WeightWatch.Revision);
            const auto status=context.CommandHistory?context.CommandHistory->Execute({.Label="Compute compact density weights",
                .Redo=[mutate,after]{return mutate(*after,true);},.Undo=[mutate,before,existed]{return mutate(*before,existed);}}).Status:mutate(*after,true);
            r.Status=Detail::ToEditorMethodCommandStatus(status);if(!r.Succeeded())r.Message="Density publication rejected by history checks.";return r;
        }
    }
    EditorDensityWeightReadiness PreviewEditorDensityWeightCommand(const EditorGeometryProcessingContext& context,const DensityWeightConfig& config)
    {
        EditorDensityWeightReadiness r;auto w=Capture(context,config,r.Diagnostic,Purpose::Readiness);
        r.Ready=bool(w);if(w)r.Resolved=w->Config;return r;
    }
    GeometryPropertyCatalogSnapshot GetEditorDensityWeightInputCatalog(const EditorGeometryProcessingContext& context,std::uint32_t id)
    {return GetEditorKeypointAnalysisInputCatalog(context,id);}
    EditorDensityWeightResult ApplyEditorDensityWeightCommand(const EditorGeometryProcessingContext& context,const DensityWeightConfig& config)
    {
        std::string diagnostic;auto w=Capture(context,config,diagnostic);
        const auto report=[&](EditorCommandStatus status,std::string message)
        {auto r=w?w->Result:EditorDensityWeightResult{.RequestedBackend=config.Backend,.Weights=config.Weights};r.Status=status;r.Message=std::move(message);return r;};
        if(!w)return report(EditorCommandStatus::InvalidProcessingParameters,diagnostic);
        if(w->Config.Backend!=DensityWeightBackend::CpuKDTree)
        {
            const auto acquired=context.SpatialIndices->Acquire(context.World,w->Entity,w->Config.Positions);
            if(!acquired.Ready())return report(EditorCommandStatus::InvalidProcessingParameters,acquired.Diagnostic);
            w->GpuIndex=acquired.Handle;w->Index=context.SpatialIndices->Snapshot(acquired.Handle);w->Result.IndexReused=acquired.Reused;
            if(!w->Index || w->Index->Slots!=w->Slots || w->Index->Index.Points().size()!=w->Points.size() ||
               !std::equal(w->Points.begin(),w->Points.end(),w->Index->Index.Points().begin()))
                return report(EditorCommandStatus::StaleEntity,"Density index does not match selected samples.");
        }
        if(!context.JobCommands.Available()){Compute(*w);return Publish(context,w);}
        const EditorJobIdentity identity{.EntityId=config.StableEntityId,.Scope=ToEditorJobScope(w->Config.Weights.Domain),
            .OutputSemantic=GeometryPresentationSlotSemantic::ScalarField,.OutputName=w->Config.Weights.Name};
        if(context.JobCommands.FindActive)
            if(auto active=context.JobCommands.FindActive(identity);active && IsActiveEditorJobState(active->State))
                return report(EditorCommandStatus::Pending,"A density-weight job for this output is already active.");
        auto sink=context.MethodResultSinks.DensityWeight;auto delivered=std::make_shared<bool>(false);
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
    EditorDensityWeightResult ApplyEditorConfiguredDensityWeight(const EditorGeometryProcessingContext& context)
    {
        const auto config=GetEditorDensityWeightConfig(context);
        if(!config)return {.Status=EditorCommandStatus::InvalidProcessingParameters,.Message="Density-weight config is unavailable."};
        return ApplyEditorDensityWeightCommand(context,*config);
    }
}

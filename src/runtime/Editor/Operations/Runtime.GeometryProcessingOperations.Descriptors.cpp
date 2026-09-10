module;
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include <utility>
#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>
module Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.SelectionController;
import Geometry.Properties;
import Geometry.HalfedgeMesh;
#include "Editor/Operations/Runtime.GeometryProcessingOperations.Internal.hpp"

namespace Extrinsic::Runtime
{
    namespace
    {
        namespace GS = ECS::Components::GeometrySources;
        namespace Features = Geometry::PointCloud::Features;
        using D = GeometryElementDomain;
        using GeometryProcessingDetail::PointPropertyWatch;
        using GeometryProcessingDetail::ObserveGeometryProperty;
        using GeometryProcessingDetail::MutableGeometryProperties;
        using GeometryProcessingDetail::PrimaryPointDomain;
        using GeometryProcessingDetail::FinitePosition;
        using GeometryProcessingDetail::GeometryPropertiesCurrent;
        struct DescriptorWork
        {
            DescriptorAnalysisConfig Config{};
            entt::entity Entity{};
            std::vector<PointPropertyWatch> Inputs{};
            std::array<PointPropertyWatch,33> OutputWatches{};
            std::vector<glm::vec3> Points{}, Normals{};
            std::vector<std::uint32_t> Slots{}, NeighborIds{}, Offsets{0};
            std::array<std::vector<float>,33> BeforeOutputs{}, AfterOutputs{};
            std::shared_ptr<const SpatialIndexSnapshot> Index{};
            SpatialIndexHandle GpuIndex{};
            std::shared_ptr<SpatialNearestBatch> Batch{};
            std::size_t NextQuery{};
            bool GpuFinished{}, Abandoned{};
            std::chrono::steady_clock::time_point GpuStarted{};
            std::optional<EditorDescriptorAnalysisResult> MainFailure{};
            EditorDescriptorAnalysisResult Result{};
        };
        bool CurrentInput(const EditorGeometryProcessingContext& context, const DescriptorWork& w)
        {
            return GeometryPropertiesCurrent(context, w.Entity, w.Inputs) && GeometryPropertiesCurrent(context, w.Entity, w.OutputWatches);
        }
        enum class CapturePurpose { Execute, Readiness, Catalog };
        std::shared_ptr<DescriptorWork> Capture(const EditorGeometryProcessingContext& context,
            DescriptorAnalysisConfig c, std::string& diagnostic, CapturePurpose purpose = CapturePurpose::Execute)
        {
            auto fail = [&](std::string why) -> std::shared_ptr<DescriptorWork> { diagnostic = std::move(why); return {}; };
            const auto validation = ValidateDescriptorAnalysisConfigSection(
                SerializeDescriptorAnalysisConfig(c), {}, kDescriptorAnalysisConfigSectionName);
            if (!validation.Usable()) return fail(validation.Diagnostics.front().Message);
            if (!context.Scene) return fail("Scene is unavailable.");
            const auto entity = GeometryProcessingDetail::ResolveEditorStableEntity(context.Scene->Raw(), c.StableEntityId);
            if (!entity) return fail("Descriptor target entity is stale or missing.");
            const auto a = BuildGeometryAvailability(context.Scene->Raw(), *entity);
            if (c.Positions.Domain == D::Unknown) c.Positions.Domain = PrimaryPointDomain(a);
            if(c.Normals.Domain==D::Unknown)c.Normals.Domain=c.Positions.Domain;
            for(auto& output:c.Outputs)if(output.Domain==D::Unknown)output.Domain=c.Positions.Domain;
            const auto* props = ResolveGeometryPropertySet(a, c.Positions.Domain);
            if (!props || !ResolveGeometryProperty(a, c.Positions, props->Size(), false).Resolved())
                return fail("Choose a count-matched vec3 position property on a resolved element domain.");
            for (const auto& output : c.Outputs)
            {
                if (output.Domain != c.Positions.Domain || (output.Name == c.Positions.Name || output.Name == c.Normals.Name))
                    return fail("Descriptor outputs must be distinct properties on the input domain.");
                for (const auto* reserved : {"v:deleted", "e:deleted", "h:deleted", "f:deleted", "v:halfedge",
                     "e:v0", "e:v1", "h:to_vertex", "h:next", "h:prev", "h:opposite", "h:face", "f:halfedge", "h:connectivity"})
                    if (output.Name == reserved) return fail("Descriptor outputs cannot replace topology/deletion properties.");
                if (props->Exists(output.Name) && !ResolveGeometryProperty(a, output, props->Size(), false).Resolved())
                    return fail("Descriptor outputs must be absent or count-matched float histogram properties.");
            }
            if(c.Normals.Domain!=c.Positions.Domain || !ResolveGeometryProperty(a,c.Normals,props->Size(),false).Resolved())
                return fail("Choose count-matched vec3 normals on the position domain.");
            if (props->Size() > std::numeric_limits<std::uint32_t>::max()) return fail("Input exceeds the supported slot range.");
            auto w = std::make_shared<DescriptorWork>();
            w->Config = c; w->Entity = *entity;
            w->Result.RequestedBackend = c.Backend;
            w->Result.Outputs = c.Outputs; w->Result.SlotCount = props->Size();
            w->Inputs.push_back(ObserveGeometryProperty(a, c.Positions.Domain, c.Positions.Name));
            w->Inputs.push_back(ObserveGeometryProperty(a,c.Normals.Domain,c.Normals.Name));
            for(unsigned i=0;i<33;++i)w->OutputWatches[i]=ObserveGeometryProperty(a,c.Outputs[i].Domain,c.Outputs[i].Name);
            auto deletionDomain = c.Positions.Domain;
            const char* deletionName = "v:deleted";
            std::size_t divisor = 1;
            if (deletionDomain == D::MeshFace) deletionName = "f:deleted";
            if (deletionDomain == D::MeshEdge || deletionDomain == D::GraphEdge) deletionName = "e:deleted";
            if (deletionDomain == D::MeshHalfedge || deletionDomain == D::GraphHalfedge)
            {
                deletionDomain = deletionDomain == D::MeshHalfedge ? D::MeshEdge : D::GraphEdge;
                deletionName = "e:deleted"; divisor = 2;
            }
            const auto* deletionProps = ResolveGeometryPropertySet(a, deletionDomain);
            if (!deletionProps || props->Size() % divisor || deletionProps->Size() != props->Size() / divisor)
                return fail("Invalid deletion domain/cardinality.");
            const auto deleted = deletionProps->Get<bool>(deletionName);
            if (deletionProps->Exists(deletionName) && (!deleted || deleted.Size() != deletionProps->Size()))
                return fail("Deletion mask must be a count-matched bool property.");
            w->Inputs.push_back(ObserveGeometryProperty(a, deletionDomain, deletionName));
            const auto points = props->Get<glm::vec3>(c.Positions.Name);
            const auto normals = props->Get<glm::vec3>(c.Normals.Name);
            bool validLbvh = true;
            for (std::uint32_t i = 0; i < props->Size(); ++i)
            {
                if (deleted && deleted[i / divisor]) continue;
                if(!FinitePosition(points[i]) || !FinitePosition(normals[i]) || glm::dot(glm::dvec3(normals[i]),glm::dvec3(normals[i]))<=0)
                    return fail("Live positions must be finite and normals finite and nonzero.");
                validLbvh &= Geometry::PointLBVH::ValidPoint(points[i]);
                ++w->Result.LiveCount;
                if (purpose == CapturePurpose::Execute) { w->Points.push_back(points[i]); w->Normals.push_back(normals[i]); w->Slots.push_back(i); }
            }
            if (!w->Result.LiveCount) return fail("Descriptor analysis requires live input samples.");
            if (purpose == CapturePurpose::Catalog) return w;
            if(w->Result.LiveCount<2)
                return fail("Descriptor analysis requires at least two live samples and positive spacing.");
            if(c.Backend!=DescriptorAnalysisBackend::CpuKDTree)
            {
                if(!context.SpatialIndices || !validLbvh || w->Result.LiveCount>(1u<<24) ||
                   c.FeatureRadius>Geometry::PointLBVH::CoordinateLimit)
                    return fail("LBVH needs the spatial cache, at most 2^24 samples and coordinates/radii within 1e18.");
                if(c.Backend==DescriptorAnalysisBackend::VulkanLBVH &&
                   (!context.JobCommands.Available() || !context.SpatialIndices->GpuQueriesAvailable() || w->Result.LiveCount>(1u<<20)))
                    return fail("Vulkan descriptors require framed GPU queries/jobs and at most 2^20 samples.");
            }
            if (purpose == CapturePurpose::Execute)
            {
                for(unsigned i=0;i<33;++i)
                {
                    if(w->OutputWatches[i].Revision)w->BeforeOutputs[i]=props->Get<float>(c.Outputs[i].Name).Vector();
                    w->AfterOutputs[i]=w->OutputWatches[i].Revision?w->BeforeOutputs[i]:std::vector<float>(props->Size());
                }
            }
            return w;
        }
        Features::DescriptorParams Parameters(const DescriptorAnalysisConfig& c)
        {
            return {.FeatureRadius=c.FeatureRadius,.MaxNeighbors=c.MaxNeighbors};
        }
        void Prepare(DescriptorWork& w)
        {
            const auto started=std::chrono::steady_clock::now();
            w.Result.Status=EditorCommandStatus::GeometryProcessingFailed;
            const auto scale=Features::ResolveDescriptorScale(w.Points,Parameters(w.Config));
            if(!scale) {w.Result.Message="Descriptor scale requires positive spacing and representable radius.";return;}
            if(w.Config.Backend!=DescriptorAnalysisBackend::CpuKDTree &&
               scale->FeatureRadius>Geometry::PointLBVH::CoordinateLimit)
            {w.Result.Message="Resolved descriptor radius exceeds the LBVH range.";return;}
            w.Result.Scale=*scale;
            w.Result.CpuComputeMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
            w.Result.Status=EditorCommandStatus::Pending;
        }
        bool AppendRow(DescriptorWork& w, std::vector<std::uint32_t>& row)
        {
            if(row.size()>std::numeric_limits<std::uint32_t>::max()-w.NeighborIds.size())
            {w.Result.Message="Complete descriptor support exceeds the packed neighborhood range.";return false;}
            std::sort(row.begin(),row.end());
            w.Result.MaximumNeighbors=std::max(w.Result.MaximumNeighbors,row.size());
            w.NeighborIds.insert(w.NeighborIds.end(),row.begin(),row.end());
            w.Offsets.push_back(std::uint32_t(w.NeighborIds.size()));
            return true;
        }
        void Compute(DescriptorWork& w)
        {
            const auto started=std::chrono::steady_clock::now();
            auto& r=w.Result;const auto& c=w.Config;
            r.ActualBackend=ToString(c.Backend);
            if(c.Backend==DescriptorAnalysisBackend::CpuLBVH) Prepare(w);
            if(r.Status==EditorCommandStatus::GeometryProcessingFailed)return;
            r.Status=EditorCommandStatus::GeometryProcessingFailed;
            std::optional<Features::DescriptorSet> analysis;
            if(c.Backend==DescriptorAnalysisBackend::CpuKDTree)
                analysis=Features::ComputeDescriptors(w.Points,w.Normals,{},Parameters(c));
            else
            {
                if(c.Backend==DescriptorAnalysisBackend::CpuLBVH)
                {
                    std::vector<std::uint32_t> row;
                    const auto radius=r.Scale.FeatureRadius;
                    for(std::uint32_t i=0;i<w.Points.size();++i)
                    {
                        const auto capacity=c.MaxNeighbors?std::min(c.MaxNeighbors,std::uint32_t(w.Points.size()-1)):std::uint32_t(w.Points.size()-1);
                        const auto neighbors=w.Index->Index.Radius(w.Points[i],radius,capacity,i);
                        r.MaximumNeighbors=std::max(r.MaximumNeighbors,std::size_t(neighbors.TotalCount));
                        const auto required=c.MaxNeighbors?std::min(c.MaxNeighbors,neighbors.TotalCount):neighbors.TotalCount;
                        if(neighbors.Neighbors.size()<required)
                        {r.Message="Incomplete CPU descriptor radius support.";return;}
                        row.clear();for(const auto& n:neighbors.Neighbors)row.push_back(n.Index);
                        if(!AppendRow(w,row))return;
                    }
                }
                analysis=Features::ComputeDescriptorsFromNeighbors(w.Points,w.Normals,{},Parameters(c),r.Scale,{w.Offsets,w.NeighborIds});
            }
            if(!analysis) {r.Message="Descriptor analysis rejected invalid support or invalid normals.";return;}
            for(std::size_t i=0;i<w.Slots.size();++i)for(unsigned b=0;b<33;++b)
                w.AfterOutputs[b][w.Slots[i]]=analysis->Row(std::uint32_t(i))[b];
            r.WrittenCount=w.Slots.size();r.Scale=analysis->Scale;
            r.Status=EditorCommandStatus::Applied;
            r.CpuComputeMilliseconds=(c.Backend==DescriptorAnalysisBackend::VulkanLBVH?r.CpuComputeMilliseconds:0)+
                std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
            r.Message="FPFH histogram columns computed with "+r.ActualBackend+" neighborhoods; scale, SPFH and FPFH run on CPU.";
        }
        bool AdvanceGpu(const EditorGeometryProcessingContext& context, DescriptorWork& w)
        {
            auto fail=[&](std::string why,EditorCommandStatus status=EditorCommandStatus::GeometryProcessingFailed)
            {w.Result.Status=status;w.Result.Message=std::move(why);w.MainFailure=w.Result;w.Batch.reset();return true;};
            if(w.Abandoned || !CurrentInput(context,w))return fail("Descriptor input changed or the job was cancelled.",EditorCommandStatus::StaleEntity);
            if(w.GpuFinished)return true;
            if(w.GpuStarted==std::chrono::steady_clock::time_point{})w.GpuStarted=std::chrono::steady_clock::now();
            if(w.Batch)
            {
                if(w.Batch->State==SpatialQueryState::Failed)return fail(w.Batch->Diagnostic);
                if(w.Batch->State!=SpatialQueryState::Ready)return false;
                w.Result.ActualBackend="vulkan_lbvh";
                std::vector<std::uint32_t> row;
                for(std::size_t i=0;i<w.Batch->Counts.size();++i)
                {
                    w.Result.MaximumNeighbors=std::max(w.Result.MaximumNeighbors,std::size_t(w.Batch->Counts[i]));
                    const auto required=w.Config.MaxNeighbors?std::min(w.Config.MaxNeighbors,w.Batch->Counts[i]):w.Batch->Counts[i];
                    if(required>w.Batch->Capacity)
                        return fail("Vulkan descriptor radius support overflowed capacity; increase capacity, set a supported neighbor cap or reduce radius. Previous outputs retained.");
                    row.clear();
                    for(std::uint32_t j=0;j<required;++j)
                    {
                        const auto id=w.Batch->Neighbors[i*w.Batch->Capacity+j].Index;
                        const auto found=std::lower_bound(w.Slots.begin(),w.Slots.end(),id);
                        if(found==w.Slots.end() || *found!=id || id==w.Slots[w.NextQuery+i])
                            return fail("Invalid Vulkan descriptor source row.");
                        row.push_back(std::uint32_t(found-w.Slots.begin()));
                    }
                    if(!AppendRow(w,row))return fail(w.Result.Message);
                }
                w.NextQuery+=w.Batch->Counts.size();
                if(w.NextQuery==w.Points.size())
                {
                    w.Batch.reset();w.GpuFinished=true;
                    w.Result.GpuNeighborhoodMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-w.GpuStarted).count();
                    return true;
                }
            }
            const auto count=std::min<std::size_t>(w.Config.GpuQueryBatchSize,w.Points.size()-w.NextQuery);
            if(w.Batch && w.Batch->Counts.size()!=count)w.Batch.reset();
            auto capacity=std::uint32_t(std::min<std::size_t>(w.Config.GpuRadiusCapacity,w.Points.size()-1));
            if(w.Config.MaxNeighbors)capacity=std::min(capacity,w.Config.MaxNeighbors);
            w.Batch=context.SpatialIndices->QueueGpuRadius(w.GpuIndex,std::span(w.Points).subspan(w.NextQuery,count),
                w.Result.Scale.FeatureRadius,
                capacity,
                std::span<const std::uint32_t>(w.Slots).subspan(w.NextQuery,count),std::move(w.Batch));
            ++w.Result.GpuQueryBatches;
            if(w.Batch->State==SpatialQueryState::Failed)return fail(w.Batch->Diagnostic);
            return false;
        }
        EditorDescriptorAnalysisResult Publish(const EditorGeometryProcessingContext& context,
                                            const std::shared_ptr<DescriptorWork>& w)
        {
            auto& r=w->Result;
            if (w->Abandoned || !CurrentInput(context,*w))
            {r.Status=EditorCommandStatus::StaleEntity; r.Message="Descriptor input or output changed before publication."; return r;}
            if (r.Status!=EditorCommandStatus::Applied) return r;
            struct State
            {
                std::array<bool,33> Exists{};
                std::array<std::vector<float>,33> Columns{};
            };
            auto before=std::make_shared<State>();before->Columns=w->BeforeOutputs;
            auto after=std::make_shared<State>();after->Columns=w->AfterOutputs;after->Exists.fill(true);
            for(unsigned i=0;i<33;++i)before->Exists[i]=bool(w->OutputWatches[i].Revision);
            auto revisions=std::make_shared<std::array<PointPropertyWatch,33>>(w->OutputWatches);
            const auto mutate=[context,entity=w->Entity,inputs=w->Inputs,c=w->Config,revisions](const State& target)
            {
                if(!GeometryPropertiesCurrent(context,entity,inputs) || !GeometryPropertiesCurrent(context,entity,*revisions))
                    return EditorCommandHistoryStatus::StaleEntity;
                auto* props=MutableGeometryProperties(context.Scene->Raw(),entity,c.Positions.Domain);
                for(unsigned i=0;i<33;++i)
                {
                    if(target.Exists[i])props->GetOrAdd<float>(c.Outputs[i].Name).Vector()=target.Columns[i];
                    else if(auto property=props->Get<float>(c.Outputs[i].Name))props->Remove(property);
                    (*revisions)[i].Revision=props->FindPropertyRevision(c.Outputs[i].Name);
                }
                if(context.InvalidateWorkspaceSnapshotCache)context.InvalidateWorkspaceSnapshotCache();
                return EditorCommandHistoryStatus::Applied;
            };
            const auto status=context.CommandHistory ? context.CommandHistory->Execute({.Label="Compute FPFH descriptors",
                .Redo=[mutate,after]{return mutate(*after);},.Undo=[mutate,before]{return mutate(*before);}}).Status : mutate(*after);
            r.Status=GeometryProcessingDetail::ToEditorMethodCommandStatus(status);
            if (!r.Succeeded()) r.Message="Descriptor publication rejected by history checks.";
            return r;
        }
    }
    EditorDescriptorAnalysisReadiness PreviewEditorDescriptorAnalysisCommand(
        const EditorGeometryProcessingContext& context,const DescriptorAnalysisConfig& config)
    {
        EditorDescriptorAnalysisReadiness r;
        auto w=Capture(context,config,r.Diagnostic,CapturePurpose::Readiness);
        r.Ready=bool(w);if(w)r.Resolved=w->Config;return r;
    }
    GeometryPropertyCatalogSnapshot GetEditorDescriptorAnalysisInputCatalog(
        const EditorGeometryProcessingContext& context,std::uint32_t id)
    {
        // Both slots accept the same finite vec3 property catalog; the combined
        // preflight additionally checks normal length, domains and all outputs.
        return GetEditorKeypointAnalysisInputCatalog(context,id);
    }

    EditorDescriptorAnalysisResult ApplyEditorDescriptorAnalysisCommand(
        const EditorGeometryProcessingContext& context,const DescriptorAnalysisConfig& config)
    {
        std::string diagnostic;
        auto w=Capture(context,config,diagnostic);
        const auto report=[&](EditorCommandStatus status,std::string message)
        {
            auto result=w?w->Result:EditorDescriptorAnalysisResult{.RequestedBackend=config.Backend,.Outputs=config.Outputs};
            result.Status=status;result.Message=std::move(message);return result;
        };
        if(!w)return report(EditorCommandStatus::InvalidProcessingParameters,diagnostic);
        if(w->Config.Backend!=DescriptorAnalysisBackend::CpuKDTree)
        {
            const auto acquired=context.SpatialIndices->Acquire(context.World,w->Entity,w->Config.Positions);
            if(!acquired.Ready())return report(EditorCommandStatus::InvalidProcessingParameters,acquired.Diagnostic);
            w->GpuIndex=acquired.Handle;w->Index=context.SpatialIndices->Snapshot(acquired.Handle);w->Result.IndexReused=acquired.Reused;
            if(!w->Index || w->Index->Slots!=w->Slots || w->Index->Index.Points().size()!=w->Points.size() ||
                !std::equal(w->Points.begin(),w->Points.end(),w->Index->Index.Points().begin()))
                return report(EditorCommandStatus::StaleEntity,"Descriptor index does not match the selected samples.");
        }
        if(!context.JobCommands.Available()){Compute(*w);return Publish(context,w);}
        const EditorJobIdentity identity{.EntityId=config.StableEntityId,.Scope=ToEditorJobScope(w->Config.Outputs[0].Domain),
            .OutputSemantic=GeometryPresentationSlotSemantic::ScalarField,.OutputName=w->Config.Outputs[0].Name};
        if(context.JobCommands.FindActive)
            if(auto active=context.JobCommands.FindActive(identity);active && IsActiveEditorJobState(active->State))
                return report(EditorCommandStatus::Pending,"A descriptor job for this output is already active.");
        auto sink=context.MethodResultSinks.DescriptorAnalysis;auto delivered=std::make_shared<bool>(false);
        auto pending=report(EditorCommandStatus::Pending,"Descriptor analysis queued.");
        // Once submitted, Result belongs to the running stage. Submission failures
        // report from this immutable snapshot while earlier stages wind down.
        const auto rejected=[pending](std::string message)
        {auto result=pending;result.Status=EditorCommandStatus::GeometryProcessingFailed;result.Message=std::move(message);return result;};
        JobDesc desc{
            .DebugName="FPFH histograms",.Scope=context.World,.Kind=RuntimeTaskKinds::GeometryProcess,
            .Work=[w](const JobCancellation&){Compute(*w);return JobResultEnvelope::Make(true);},
            .ValidateBeforeApply=[context,w]{return !w->Abandoned && CurrentInput(context,*w)?JobApplyValidation::Current:JobApplyValidation::StaleGeneration;},
            .PublishCompletion=[context,w,sink,delivered](KernelEventBus&,const JobResultEnvelope&)
            {auto result=Publish(context,w);*delivered=true;if(sink)sink(result);return result.Succeeded();},
            .FinalizeUnpublishedOnMainThread=[w,sink,delivered,pending]() mutable
            {
                w->Abandoned=true;if(*delivered)return;*delivered=true;
                if(w->MainFailure)pending=*w->MainFailure;
                else {pending.Status=EditorCommandStatus::StaleEntity;pending.Message="Descriptor job cancelled or stale; previous outputs retained.";}
                if(sink)sink(std::move(pending));
            }};
        if(w->Config.Backend==DescriptorAnalysisBackend::VulkanLBVH)
        {
            JobDesc prepare{
                .DebugName="Descriptor scale",.Scope=context.World,.Kind=RuntimeTaskKinds::GeometryProcess,
                .Work=[w](const JobCancellation&){Prepare(*w);return JobResultEnvelope::Make(true);},
                .ValidateBeforeApply=[context,w]{return !w->Abandoned && CurrentInput(context,*w)?JobApplyValidation::Current:JobApplyValidation::StaleGeneration;},
                .PublishCompletion=[w](KernelEventBus&,const JobResultEnvelope&)
                {if(w->Result.Status==EditorCommandStatus::GeometryProcessingFailed){w->MainFailure=w->Result;return false;}return true;},
                .FinalizeUnpublishedOnMainThread=[w]{w->Abandoned=true;}};
            const auto scale=context.JobCommands.Submit(std::move(prepare),identity);
            if(!scale.IsValid())return rejected("Descriptor scale submission rejected.");
            JobDesc gpu{
                .DebugName="Descriptor radius support (Vulkan)",.Scope=context.World,.Kind=RuntimeTaskKinds::GeometryProcess,
                .Work=[](const JobCancellation&){return JobResultEnvelope::Make(true);},
                .IsReadyToApply=[context,w]{return AdvanceGpu(context,*w);},
                .PublishCompletion=[w](KernelEventBus&,const JobResultEnvelope&){return w->GpuFinished;},
                .FinalizeUnpublishedOnMainThread=[w]{w->Abandoned=true;}};
            gpu.DependsOn.push_back({scale,"Resolve descriptor radius before complete Vulkan support"});
            const auto support=context.JobCommands.Submit(std::move(gpu),identity);
            if(!support.IsValid()){w->Abandoned=true;return rejected("Descriptor GPU submission rejected.");}
            desc.DependsOn.push_back({support,"Required radius prefix before SPFH and FPFH histograms"});
        }
        const auto token=context.JobCommands.Submit(std::move(desc),identity);
        if(!token.IsValid()){w->Abandoned=true;return rejected("Descriptor job submission rejected.");}
        return pending;
    }
    EditorDescriptorAnalysisResult ApplyEditorConfiguredDescriptorAnalysis(const EditorGeometryProcessingContext& context)
    {
        const auto config=GetEditorDescriptorAnalysisConfig(context);
        if(!config)return {.Status=EditorCommandStatus::InvalidProcessingParameters,.Message="Descriptor config is unavailable."};
        return ApplyEditorDescriptorAnalysisCommand(context,*config);
    }
} // namespace Extrinsic::Runtime

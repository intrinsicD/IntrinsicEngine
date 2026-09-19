module;
#include <string_view>
#include <functional>
#include <algorithm>
#include <array>
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
module Extrinsic.Runtime.PointAnalysisOperations;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.JobService;
import Extrinsic.Core.Error;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Geometry.Properties;
import Geometry.PointCloud.Features;
#include "Editor/internal/Runtime.EditorProcessingAccess.hpp"
#include "Editor/internal/Runtime.EditorGeometryHelpers.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.PointFields.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.RadiusRows.hpp"

namespace Extrinsic::Runtime
{
    namespace
    {
        namespace Features = Geometry::PointCloud::Features;
        using D = GeometryElementDomain;
        using GeometryProcessingDetail::PointPropertyWatch;
        using GeometryProcessingDetail::ObserveGeometryProperty;
        using GeometryProcessingDetail::MutableGeometryProperties;
        using GeometryProcessingDetail::PrimaryPointDomain;
        using GeometryProcessingDetail::GeometryPropertiesCurrent;
        struct DescriptorWork : GeometryProcessingDetail::PointNormalCapture
        {
            DescriptorAnalysisConfig Config{};
            entt::entity Entity{};
            std::array<PointPropertyWatch,33> OutputWatches{};
            GeometryProcessingDetail::PointRadiusRows Rows{};
            std::array<std::vector<float>,33> BeforeOutputs{}, AfterOutputs{};
            std::shared_ptr<const SpatialIndexSnapshot> Index{};
            SpatialIndexHandle GpuIndex{};
            bool Abandoned{};
            std::optional<EditorDescriptorAnalysisResult> MainFailure{};
            EditorDescriptorAnalysisResult Result{};
        };
        bool CurrentInput(const EditorProcessingContext& context, const DescriptorWork& w)
        {
            return GeometryPropertiesCurrent(context, w.Entity, w.Inputs) && GeometryPropertiesCurrent(context, w.Entity, w.OutputWatches);
        }
        enum class CapturePurpose { Execute, Readiness };
        std::shared_ptr<DescriptorWork> Capture(const EditorProcessingContext& context,
            DescriptorAnalysisConfig c, std::string& diagnostic, CapturePurpose purpose = CapturePurpose::Execute)
        {
            auto fail = [&](std::string why) -> std::shared_ptr<DescriptorWork> { diagnostic = std::move(why); return {}; };
            const auto validation = ValidateDescriptorAnalysisConfigSection(
                SerializeDescriptorAnalysisConfig(c), {}, kDescriptorAnalysisConfigSectionName);
            if (!validation.Usable()) return fail(validation.Diagnostics.front().Message);
            if (!context.Scene) return fail("Scene is unavailable.");
            const auto entity = EditorFeatureDetail::ResolveStableEntity(context.Scene->Raw(), c.StableEntityId);
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
            auto w = std::make_shared<DescriptorWork>();
            if (!GeometryProcessingDetail::CapturePointNormalInput(
                    context, *entity, a, c.Positions, c.Normals,
                    purpose == CapturePurpose::Execute, *w, diagnostic)) return {};
            if (w->HasZeroNormals)
                return fail("Live positions must be finite and normals finite and nonzero.");
            w->Config = c; w->Entity = *entity;
            w->Result.RequestedBackend = c.Backend;
            w->Result.Outputs = c.Outputs;
            for(unsigned i=0;i<33;++i)w->OutputWatches[i]=ObserveGeometryProperty(a,c.Outputs[i].Domain,c.Outputs[i].Name);
            w->Result.SlotCount = w->SlotCount;
            w->Result.LiveCount = w->LiveCount;
            if (!w->Result.LiveCount) return fail("Descriptor analysis requires live input samples.");
            if(w->Result.LiveCount<2)
                return fail("Descriptor analysis requires at least two live samples and positive spacing.");
            if(c.Backend!=DescriptorAnalysisBackend::CpuKDTree)
            {
                if(!context.SpatialIndices || !w->ValidLbvh || w->Result.LiveCount>(1u<<24) ||
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
            w.Result.MeanSpacing=scale->MeanSpacing;
            w.Result.FeatureRadius=scale->FeatureRadius;
            w.Result.CpuComputeMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
            w.Result.Status=EditorCommandStatus::Pending;
        }
        bool AppendRow(DescriptorWork& w,std::vector<std::uint32_t>& row)
        {
            const bool appended=GeometryProcessingDetail::AppendPointRadiusRow(w.Rows,row,w.Result.Message);
            w.Result.MaximumNeighbors=std::max(w.Result.MaximumNeighbors,w.Rows.MaximumNeighbors);
            return appended;
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
                    const auto radius=r.FeatureRadius;
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
                analysis=Features::ComputeDescriptorsFromNeighbors(w.Points,w.Normals,{},Parameters(c),{.MeanSpacing=r.MeanSpacing, .FeatureRadius=r.FeatureRadius},
                    {w.Rows.Offsets,w.Rows.Indices});
            }
            if(!analysis) {r.Message="Descriptor analysis rejected invalid support or invalid normals.";return;}
            for(std::size_t i=0;i<w.Slots.size();++i)for(unsigned b=0;b<33;++b)
                w.AfterOutputs[b][w.Slots[i]]=analysis->Row(std::uint32_t(i))[b];
            r.WrittenCount=w.Slots.size();
            r.MeanSpacing=analysis->Scale.MeanSpacing;
            r.FeatureRadius=analysis->Scale.FeatureRadius;
            r.Status=EditorCommandStatus::Applied;
            r.CpuComputeMilliseconds=(c.Backend==DescriptorAnalysisBackend::VulkanLBVH?r.CpuComputeMilliseconds:0)+
                std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
            r.Message="FPFH histogram columns computed with "+r.ActualBackend+" neighborhoods; scale, SPFH and FPFH run on CPU.";
        }
        bool AdvanceGpu(const EditorProcessingContext& context,DescriptorWork& w)
        {
            if(w.Abandoned || !CurrentInput(context,w))
            {
                w.Result.Status=EditorCommandStatus::StaleEntity;
                w.Result.Message="Descriptor input changed or the job was cancelled.";
                w.MainFailure=w.Result;w.Rows.Batch.reset();return true;
            }
            std::string diagnostic;
            const auto state=GeometryProcessingDetail::AdvancePointRadiusRows(*context.SpatialIndices,w.GpuIndex,
                w.Points,w.Slots,w.Result.FeatureRadius,w.Config.GpuQueryBatchSize,w.Config.GpuRadiusCapacity,w.Config.MaxNeighbors,w.Rows,diagnostic);
            w.Result.MaximumNeighbors=w.Rows.MaximumNeighbors;w.Result.GpuQueryBatches=w.Rows.QueryBatches;
            w.Result.GpuNeighborhoodMilliseconds=w.Rows.Milliseconds;
            if(w.Rows.Queried)w.Result.ActualBackend="vulkan_lbvh";
            if(state==GeometryProcessingDetail::RadiusRowsState::Failed)
            {w.Result.Status=EditorCommandStatus::GeometryProcessingFailed;w.Result.Message=std::move(diagnostic);w.MainFailure=w.Result;}
            return state!=GeometryProcessingDetail::RadiusRowsState::Pending;
        }
        EditorDescriptorAnalysisResult Publish(const EditorProcessingContext& context,
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
            r.Status=EditorFeatureDetail::ToEditorCommandStatus(status);
            if (!r.Succeeded()) r.Message="Descriptor publication rejected by history checks.";
            return r;
        }
    }
    ActionReadiness PreviewEditorDescriptorAnalysisCommand(
        const EditorProcessingCommands& commands,const DescriptorAnalysisConfig& config)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        std::string diagnostic;
        const auto work = Capture(context, config, diagnostic, CapturePurpose::Readiness);
        return {bool(work), std::move(diagnostic)};
    }
    EditorDescriptorAnalysisResult ApplyEditorDescriptorAnalysisCommand(
        const EditorProcessingCommands& commands,const DescriptorAnalysisConfig& config,
        std::function<void(EditorDescriptorAnalysisResult)> onComplete)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
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
            const auto indexState = GeometryProcessingDetail::AcquirePointIndex(
                *context.SpatialIndices, context.World, w->Entity, w->Config.Positions,
                w->Slots, w->Points, w->GpuIndex, w->Index, w->Result.IndexReused, diagnostic);
            if (indexState == GeometryProcessingDetail::PointIndexState::Unavailable)
                return report(EditorCommandStatus::InvalidProcessingParameters, diagnostic);
            if (indexState == GeometryProcessingDetail::PointIndexState::Mismatched)
                return report(EditorCommandStatus::StaleEntity,
                              "Descriptor index does not match the selected samples.");
        }
        if(!context.JobCommands.Available()){Compute(*w);return Publish(context,w);}
        const EditorJobIdentity identity{.EntityId=config.StableEntityId,.Scope=ToEditorJobScope(w->Config.Outputs[0].Domain),
            .OutputSemantic=GeometryPresentationSlotSemantic::ScalarField,.OutputName=w->Config.Outputs[0].Name};
        if (auto active = GeometryProcessingDetail::MeshSupport::FindActiveEditorJob(context, identity);
            active && IsActiveEditorJobState(active->State))
            return report(EditorCommandStatus::Pending,"A descriptor job for this output is already active.");
        auto sink=GuardEditorProcessingResult(context, std::move(onComplete));auto delivered=std::make_shared<bool>(false);
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
                .PublishCompletion=[w](KernelEventBus&,const JobResultEnvelope&){return w->Rows.Finished;},
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
    EditorDescriptorAnalysisResult ApplyEditorConfiguredDescriptorAnalysis(
        const EditorProcessingCommands& commands,
        std::function<void(EditorDescriptorAnalysisResult)> onComplete)
    {
        const auto config=GetEditorDescriptorAnalysisConfig(commands);
        if(!config)return {.Status=EditorCommandStatus::InvalidProcessingParameters,.Message="Descriptor config is unavailable."};
        return ApplyEditorDescriptorAnalysisCommand(commands,*config,std::move(onComplete));
    }
} // namespace Extrinsic::Runtime

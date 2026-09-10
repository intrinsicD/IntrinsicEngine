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
#include "Editor/Operations/Runtime.GeometryProcessingOperations.RadiusRows.hpp"

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
        struct KeypointWork
        {
            KeypointAnalysisConfig Config{};
            entt::entity Entity{};
            std::vector<PointPropertyWatch> Inputs{};
            PointPropertyWatch MaskWatch{}, ScoreWatch{};
            std::vector<glm::vec3> Points{};
            std::vector<std::uint32_t> Slots{}, BeforeMask{}, AfterMask{};
            GeometryProcessingDetail::PointRadiusRows Rows{};
            std::vector<float> BeforeScore{}, AfterScore{};
            std::shared_ptr<const SpatialIndexSnapshot> Index{};
            SpatialIndexHandle GpuIndex{};
            bool Abandoned{};
            std::optional<EditorKeypointAnalysisResult> MainFailure{};
            EditorKeypointAnalysisResult Result{};
        };
        bool CurrentInput(const EditorGeometryProcessingContext& context, const KeypointWork& w)
        {
            const std::array outputs{w.MaskWatch, w.ScoreWatch};
            return GeometryPropertiesCurrent(context, w.Entity, w.Inputs) && GeometryPropertiesCurrent(context, w.Entity, outputs);
        }
        enum class CapturePurpose { Execute, Readiness, Catalog };
        std::shared_ptr<KeypointWork> Capture(const EditorGeometryProcessingContext& context,
            KeypointAnalysisConfig c, std::string& diagnostic, CapturePurpose purpose = CapturePurpose::Execute)
        {
            auto fail = [&](std::string why) -> std::shared_ptr<KeypointWork> { diagnostic = std::move(why); return {}; };
            const auto validation = ValidateKeypointAnalysisConfigSection(
                SerializeKeypointAnalysisConfig(c), {}, kKeypointAnalysisConfigSectionName);
            if (!validation.Usable()) return fail(validation.Diagnostics.front().Message);
            if (!context.Scene) return fail("Scene is unavailable.");
            const auto entity = GeometryProcessingDetail::ResolveEditorStableEntity(context.Scene->Raw(), c.StableEntityId);
            if (!entity) return fail("Keypoint target entity is stale or missing.");
            const auto a = BuildGeometryAvailability(context.Scene->Raw(), *entity);
            if (c.Positions.Domain == D::Unknown) c.Positions.Domain = PrimaryPointDomain(a);
            if (c.Mask.Domain == D::Unknown) c.Mask.Domain = c.Positions.Domain;
            if (c.Score.Domain == D::Unknown) c.Score.Domain = c.Positions.Domain;
            const auto* props = ResolveGeometryPropertySet(a, c.Positions.Domain);
            if (!props || !ResolveGeometryProperty(a, c.Positions, props->Size(), false).Resolved())
                return fail("Choose a count-matched vec3 position property on a resolved element domain.");
            for (const auto& output : {c.Mask, c.Score})
            {
                if (output.Domain != c.Positions.Domain || output.Name == c.Positions.Name)
                    return fail("Keypoint outputs must be distinct properties on the input domain.");
                for (const auto* reserved : {"v:deleted", "e:deleted", "h:deleted", "f:deleted", "v:halfedge",
                     "e:v0", "e:v1", "h:to_vertex", "h:next", "h:prev", "h:opposite", "h:face", "f:halfedge", "h:connectivity"})
                    if (output.Name == reserved) return fail("Keypoint outputs cannot replace topology/deletion properties.");
                if (props->Exists(output.Name) && !ResolveGeometryProperty(a, output, props->Size(), false).Resolved())
                    return fail("Keypoint outputs must be absent or count-matched uint32 mask / float score properties.");
            }
            if (props->Size() > std::numeric_limits<std::uint32_t>::max()) return fail("Input exceeds the supported slot range.");
            auto w = std::make_shared<KeypointWork>();
            w->Config = c; w->Entity = *entity;
            w->Result.RequestedBackend = c.Backend;
            w->Result.Mask = c.Mask; w->Result.Score = c.Score; w->Result.SlotCount = props->Size();
            w->Inputs.push_back(ObserveGeometryProperty(a, c.Positions.Domain, c.Positions.Name));
            w->MaskWatch = ObserveGeometryProperty(a, c.Mask.Domain, c.Mask.Name);
            w->ScoreWatch = ObserveGeometryProperty(a, c.Score.Domain, c.Score.Name);
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
            bool validLbvh = true;
            for (std::uint32_t i = 0; i < props->Size(); ++i)
            {
                if (deleted && deleted[i / divisor]) continue;
                if (!FinitePosition(points[i])) return fail("Live position samples must be finite.");
                validLbvh &= Geometry::PointLBVH::ValidPoint(points[i]);
                ++w->Result.LiveCount;
                if (purpose == CapturePurpose::Execute) { w->Points.push_back(points[i]); w->Slots.push_back(i); }
            }
            if (!w->Result.LiveCount) return fail("Keypoint analysis requires live input samples.");
            if (purpose == CapturePurpose::Catalog) return w;
            if(w->Result.LiveCount<2 || c.MinimumNeighbors>=w->Result.LiveCount)
                return fail("Keypoint analysis requires positive spacing and more live samples than minimum neighbors.");
            if(c.Backend!=KeypointAnalysisBackend::CpuKDTree)
            {
                if(!context.SpatialIndices || !validLbvh || w->Result.LiveCount>(1u<<24) ||
                   std::max(c.SalientRadius,c.NonMaxRadius)>Geometry::PointLBVH::CoordinateLimit)
                    return fail("LBVH needs the spatial cache, at most 2^24 samples and coordinates/radii within 1e18.");
                if(c.Backend==KeypointAnalysisBackend::VulkanLBVH &&
                   (!context.JobCommands.Available() || !context.SpatialIndices->GpuQueriesAvailable() || w->Result.LiveCount>(1u<<20)))
                    return fail("Vulkan keypoints require framed GPU queries/jobs and at most 2^20 samples.");
            }
            if (purpose == CapturePurpose::Execute)
            {
                if (w->MaskWatch.Revision) w->BeforeMask = props->Get<std::uint32_t>(c.Mask.Name).Vector();
                if (w->ScoreWatch.Revision) w->BeforeScore = props->Get<float>(c.Score.Name).Vector();
                w->AfterMask = w->MaskWatch.Revision ? w->BeforeMask : std::vector<std::uint32_t>(props->Size());
                w->AfterScore = w->ScoreWatch.Revision ? w->BeforeScore : std::vector<float>(props->Size());
            }
            return w;
        }
        Features::KeypointParams Parameters(const KeypointAnalysisConfig& c)
        {
            return {.SalientRadius=c.SalientRadius,.NonMaxRadius=c.NonMaxRadius,
                    .Gamma21=c.Gamma21,.Gamma32=c.Gamma32,.MinNeighbors=c.MinimumNeighbors};
        }
        void Prepare(KeypointWork& w)
        {
            const auto started=std::chrono::steady_clock::now();
            w.Result.Status=EditorCommandStatus::GeometryProcessingFailed;
            const auto scale=Features::ResolveKeypointScale(w.Points,Parameters(w.Config));
            if(!scale) {w.Result.Message="Keypoint scale requires positive spacing and representable radii.";return;}
            if(w.Config.Backend!=KeypointAnalysisBackend::CpuKDTree &&
               std::max(scale->SalientRadius,scale->NonMaxRadius)>Geometry::PointLBVH::CoordinateLimit)
            {w.Result.Message="Resolved keypoint radius exceeds the LBVH range.";return;}
            w.Result.Scale=*scale;
            w.Result.CpuComputeMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
            w.Result.Status=EditorCommandStatus::Pending;
        }
        bool AppendRow(KeypointWork& w,std::vector<std::uint32_t>& row)
        {
            const bool appended=GeometryProcessingDetail::AppendPointRadiusRow(w.Rows,row,w.Result.Message);
            w.Result.MaximumNeighbors=std::max(w.Result.MaximumNeighbors,w.Rows.MaximumNeighbors);
            return appended;
        }
        void Compute(KeypointWork& w)
        {
            const auto started=std::chrono::steady_clock::now();
            auto& r=w.Result;const auto& c=w.Config;
            r.ActualBackend=ToString(c.Backend);
            if(c.Backend==KeypointAnalysisBackend::CpuLBVH) Prepare(w);
            if(r.Status==EditorCommandStatus::GeometryProcessingFailed)return;
            r.Status=EditorCommandStatus::GeometryProcessingFailed;
            std::optional<Features::KeypointAnalysis> analysis;
            if(c.Backend==KeypointAnalysisBackend::CpuKDTree)
                analysis=Features::AnalyzeKeypoints(w.Points,Parameters(c));
            else
            {
                if(c.Backend==KeypointAnalysisBackend::CpuLBVH)
                {
                    std::vector<std::uint32_t> row;
                    const auto radius=std::max(r.Scale.SalientRadius,r.Scale.NonMaxRadius);
                    for(std::uint32_t i=0;i<w.Points.size();++i)
                    {
                        const auto neighbors=w.Index->Index.Radius(w.Points[i],radius,std::uint32_t(w.Points.size()-1),i);
                        if(neighbors.Overflowed() || neighbors.TotalCount!=neighbors.Neighbors.size())
                        {r.Message="Incomplete CPU keypoint radius support.";return;}
                        row.clear();for(const auto& n:neighbors.Neighbors)row.push_back(n.Index);
                        if(!AppendRow(w,row))return;
                    }
                }
                analysis=Features::AnalyzeKeypointsFromNeighbors(w.Points,Parameters(c),r.Scale,{w.Rows.Offsets,w.Rows.Indices});
            }
            if(!analysis) {r.Message="Keypoint analysis rejected invalid support or unrepresentable covariance.";return;}
            for(std::size_t i=0;i<w.Slots.size();++i)
            {w.AfterMask[w.Slots[i]]=analysis->Mask[i];w.AfterScore[w.Slots[i]]=analysis->Saliency[i];}
            r.KeypointCount=analysis->Keypoints.Indices.size();r.WrittenCount=w.Slots.size();r.Scale=analysis->Scale;
            r.Status=EditorCommandStatus::Applied;
            r.CpuComputeMilliseconds=(c.Backend==KeypointAnalysisBackend::VulkanLBVH?r.CpuComputeMilliseconds:0)+
                std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
            r.Message="Keypoint mask and saliency computed with "+r.ActualBackend+" neighborhoods; scale, covariance and suppression run on CPU.";
        }
        bool AdvanceGpu(const EditorGeometryProcessingContext& context,KeypointWork& w)
        {
            if(w.Abandoned || !CurrentInput(context,w))
            {
                w.Result.Status=EditorCommandStatus::StaleEntity;
                w.Result.Message="Keypoint input changed or the job was cancelled.";
                w.MainFailure=w.Result;w.Rows.Batch.reset();return true;
            }
            std::string diagnostic;
            const auto state=GeometryProcessingDetail::AdvancePointRadiusRows(*context.SpatialIndices,w.GpuIndex,
                w.Points,w.Slots,std::max(w.Result.Scale.SalientRadius,w.Result.Scale.NonMaxRadius),w.Config.GpuQueryBatchSize,w.Config.GpuRadiusCapacity,0,w.Rows,diagnostic);
            w.Result.MaximumNeighbors=w.Rows.MaximumNeighbors;w.Result.GpuQueryBatches=w.Rows.QueryBatches;
            w.Result.GpuNeighborhoodMilliseconds=w.Rows.Milliseconds;
            if(w.Rows.Queried)w.Result.ActualBackend="vulkan_lbvh";
            if(state==GeometryProcessingDetail::RadiusRowsState::Failed)
            {w.Result.Status=EditorCommandStatus::GeometryProcessingFailed;w.Result.Message=std::move(diagnostic);w.MainFailure=w.Result;}
            return state!=GeometryProcessingDetail::RadiusRowsState::Pending;
        }
        EditorKeypointAnalysisResult Publish(const EditorGeometryProcessingContext& context,
                                            const std::shared_ptr<KeypointWork>& w)
        {
            auto& r=w->Result;
            if (w->Abandoned || !CurrentInput(context,*w))
            {r.Status=EditorCommandStatus::StaleEntity; r.Message="Keypoint input or output changed before publication."; return r;}
            if (r.Status!=EditorCommandStatus::Applied) return r;
            struct State
            {
                bool MaskExists{}, ScoreExists{};
                std::vector<std::uint32_t> Mask{};
                std::vector<float> Score{};
            };
            auto before=std::make_shared<State>(State{bool(w->MaskWatch.Revision),bool(w->ScoreWatch.Revision),w->BeforeMask,w->BeforeScore});
            auto after=std::make_shared<State>(State{true,true,w->AfterMask,w->AfterScore});
            auto revisions=std::make_shared<std::array<PointPropertyWatch,2>>(std::array{w->MaskWatch,w->ScoreWatch});
            const auto mutate=[context,entity=w->Entity,inputs=w->Inputs,c=w->Config,revisions](const State& target)
            {
                if (!GeometryPropertiesCurrent(context,entity,inputs) || !GeometryPropertiesCurrent(context,entity,*revisions))
                    return EditorCommandHistoryStatus::StaleEntity;
                auto* props=MutableGeometryProperties(context.Scene->Raw(),entity,c.Positions.Domain);
                if (target.MaskExists) props->GetOrAdd<std::uint32_t>(c.Mask.Name).Vector()=target.Mask;
                else if (auto p=props->Get<std::uint32_t>(c.Mask.Name)) props->Remove(p);
                if (target.ScoreExists) props->GetOrAdd<float>(c.Score.Name).Vector()=target.Score;
                else if (auto p=props->Get<float>(c.Score.Name)) props->Remove(p);
                const auto a=BuildGeometryAvailability(context.Scene->Raw(),entity);
                *revisions={ObserveGeometryProperty(a,c.Mask.Domain,c.Mask.Name),ObserveGeometryProperty(a,c.Score.Domain,c.Score.Name)};
                ECS::Components::DirtyTags::MarkGpuDirty(context.Scene->Raw(),entity);
                ECS::Components::DirtyTags::MarkVertexAttributesDirty(context.Scene->Raw(),entity);
                if (context.InvalidateWorkspaceSnapshotCache) context.InvalidateWorkspaceSnapshotCache();
                return EditorCommandHistoryStatus::Applied;
            };
            const auto status=context.CommandHistory ? context.CommandHistory->Execute({.Label="Detect keypoints",
                .Redo=[mutate,after]{return mutate(*after);},.Undo=[mutate,before]{return mutate(*before);}}).Status : mutate(*after);
            r.Status=GeometryProcessingDetail::ToEditorMethodCommandStatus(status);
            if (!r.Succeeded()) r.Message="Keypoint publication rejected by history checks.";
            return r;
        }
    }
    EditorKeypointAnalysisReadiness PreviewEditorKeypointAnalysisCommand(
        const EditorGeometryProcessingContext& context,const KeypointAnalysisConfig& config)
    {
        EditorKeypointAnalysisReadiness r;
        auto w=Capture(context,config,r.Diagnostic,CapturePurpose::Readiness);
        r.Ready=bool(w);if(w)r.Resolved=w->Config;return r;
    }
    GeometryPropertyCatalogSnapshot GetEditorKeypointAnalysisInputCatalog(
        const EditorGeometryProcessingContext& context,std::uint32_t id)
    {
        if(!context.Scene)return {};
        const auto entity=GeometryProcessingDetail::ResolveEditorStableEntity(context.Scene->Raw(),id);
        if(!entity)return {};
        const auto a=BuildGeometryAvailability(context.Scene->Raw(),*entity);
        std::uint64_t generation=1469598103934665603ull;
        for(unsigned d=1;d<=unsigned(D::PointCloudPoint);++d)
        {
            const auto* props=ResolveGeometryPropertySet(a,D(d));
            generation=(generation^(props?props->Revision():0))*1099511628211ull;
        }
        auto catalog=BuildGeometryPropertyCatalogSnapshot(a,id,generation);
        std::erase_if(catalog.Entries,[&](auto& entry){
            if(entry.Ref.ValueKind!=Geometry::PropertyValueKind::Vec3)return true;
            const auto* props=ResolveGeometryPropertySet(a,entry.Ref.Domain);
            entry.PropertyGeneration=props->FindPropertyRevision(entry.Ref.Name).value_or(0);
            KeypointAnalysisConfig c;c.StableEntityId=id;c.Positions=entry.Ref;c.Mask.Domain=c.Score.Domain=entry.Ref.Domain;
            c.Mask.Name=entry.Ref.Name+".keypoint_mask";c.Score.Name=entry.Ref.Name+".keypoint_score";
            while(props->Exists(c.Mask.Name))c.Mask.Name+="_";
            while(props->Exists(c.Score.Name))c.Score.Name+="_";
            std::string diagnostic;return !Capture(context,c,diagnostic,CapturePurpose::Catalog);
        });
        return catalog;
    }
    EditorKeypointAnalysisResult ApplyEditorKeypointAnalysisCommand(
        const EditorGeometryProcessingContext& context,const KeypointAnalysisConfig& config)
    {
        std::string diagnostic;
        auto w=Capture(context,config,diagnostic);
        const auto report=[&](EditorCommandStatus status,std::string message)
        {
            auto result=w?w->Result:EditorKeypointAnalysisResult{.RequestedBackend=config.Backend,.Mask=config.Mask,.Score=config.Score};
            result.Status=status;result.Message=std::move(message);return result;
        };
        if(!w)return report(EditorCommandStatus::InvalidProcessingParameters,diagnostic);
        if(w->Config.Backend!=KeypointAnalysisBackend::CpuKDTree)
        {
            const auto acquired=context.SpatialIndices->Acquire(context.World,w->Entity,w->Config.Positions);
            if(!acquired.Ready())return report(EditorCommandStatus::InvalidProcessingParameters,acquired.Diagnostic);
            w->GpuIndex=acquired.Handle;w->Index=context.SpatialIndices->Snapshot(acquired.Handle);w->Result.IndexReused=acquired.Reused;
            if(!w->Index || w->Index->Slots!=w->Slots || w->Index->Index.Points().size()!=w->Points.size() ||
                !std::equal(w->Points.begin(),w->Points.end(),w->Index->Index.Points().begin()))
                return report(EditorCommandStatus::StaleEntity,"Keypoint index does not match the selected samples.");
        }
        if(!context.JobCommands.Available()){Compute(*w);return Publish(context,w);}
        const EditorJobIdentity identity{.EntityId=config.StableEntityId,.Scope=ToEditorJobScope(w->Config.Mask.Domain),
            .OutputSemantic=GeometryPresentationSlotSemantic::ScalarField,.OutputName=w->Config.Mask.Name};
        if(context.JobCommands.FindActive)
            if(auto active=context.JobCommands.FindActive(identity);active && IsActiveEditorJobState(active->State))
                return report(EditorCommandStatus::Pending,"A keypoint job for this output is already active.");
        auto sink=context.MethodResultSinks.KeypointAnalysis;auto delivered=std::make_shared<bool>(false);
        auto pending=report(EditorCommandStatus::Pending,"Keypoint analysis queued.");
        // Once submitted, Result belongs to the running stage. Submission failures
        // report from this immutable snapshot while earlier stages wind down.
        const auto rejected=[pending](std::string message)
        {auto result=pending;result.Status=EditorCommandStatus::GeometryProcessingFailed;result.Message=std::move(message);return result;};
        JobDesc desc{
            .DebugName="Keypoint covariance and suppression",.Scope=context.World,.Kind=RuntimeTaskKinds::GeometryProcess,
            .Work=[w](const JobCancellation&){Compute(*w);return JobResultEnvelope::Make(true);},
            .ValidateBeforeApply=[context,w]{return !w->Abandoned && CurrentInput(context,*w)?JobApplyValidation::Current:JobApplyValidation::StaleGeneration;},
            .PublishCompletion=[context,w,sink,delivered](KernelEventBus&,const JobResultEnvelope&)
            {auto result=Publish(context,w);*delivered=true;if(sink)sink(result);return result.Succeeded();},
            .FinalizeUnpublishedOnMainThread=[w,sink,delivered,pending]() mutable
            {
                w->Abandoned=true;if(*delivered)return;*delivered=true;
                if(w->MainFailure)pending=*w->MainFailure;
                else {pending.Status=EditorCommandStatus::StaleEntity;pending.Message="Keypoint job cancelled or stale; previous outputs retained.";}
                if(sink)sink(std::move(pending));
            }};
        if(w->Config.Backend==KeypointAnalysisBackend::VulkanLBVH)
        {
            JobDesc prepare{
                .DebugName="Keypoint scale",.Scope=context.World,.Kind=RuntimeTaskKinds::GeometryProcess,
                .Work=[w](const JobCancellation&){Prepare(*w);return JobResultEnvelope::Make(true);},
                .ValidateBeforeApply=[context,w]{return !w->Abandoned && CurrentInput(context,*w)?JobApplyValidation::Current:JobApplyValidation::StaleGeneration;},
                .PublishCompletion=[w](KernelEventBus&,const JobResultEnvelope&)
                {if(w->Result.Status==EditorCommandStatus::GeometryProcessingFailed){w->MainFailure=w->Result;return false;}return true;},
                .FinalizeUnpublishedOnMainThread=[w]{w->Abandoned=true;}};
            const auto scale=context.JobCommands.Submit(std::move(prepare),identity);
            if(!scale.IsValid())return rejected("Keypoint scale submission rejected.");
            JobDesc gpu{
                .DebugName="Keypoint radius support (Vulkan)",.Scope=context.World,.Kind=RuntimeTaskKinds::GeometryProcess,
                .Work=[](const JobCancellation&){return JobResultEnvelope::Make(true);},
                .IsReadyToApply=[context,w]{return AdvanceGpu(context,*w);},
                .PublishCompletion=[w](KernelEventBus&,const JobResultEnvelope&){return w->Rows.Finished;},
                .FinalizeUnpublishedOnMainThread=[w]{w->Abandoned=true;}};
            gpu.DependsOn.push_back({scale,"Resolve keypoint radii before complete Vulkan support"});
            const auto support=context.JobCommands.Submit(std::move(gpu),identity);
            if(!support.IsValid()){w->Abandoned=true;return rejected("Keypoint GPU submission rejected.");}
            desc.DependsOn.push_back({support,"Complete radius support before keypoint covariance and suppression"});
        }
        const auto token=context.JobCommands.Submit(std::move(desc),identity);
        if(!token.IsValid()){w->Abandoned=true;return rejected("Keypoint job submission rejected.");}
        return pending;
    }
    EditorKeypointAnalysisResult ApplyEditorConfiguredKeypointAnalysis(const EditorGeometryProcessingContext& context)
    {
        const auto config=GetEditorKeypointAnalysisConfig(context);
        if(!config)return {.Status=EditorCommandStatus::InvalidProcessingParameters,.Message="Keypoint config is unavailable."};
        return ApplyEditorKeypointAnalysisCommand(context,*config);
    }
} // namespace Extrinsic::Runtime

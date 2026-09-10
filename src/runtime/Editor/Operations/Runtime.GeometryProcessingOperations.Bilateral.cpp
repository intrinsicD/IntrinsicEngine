module;
#include <algorithm>
#include <array>
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
import Geometry.Properties;
import Geometry.HalfedgeMesh;
#include "Editor/Operations/Runtime.GeometryProcessingOperations.Internal.hpp"

namespace Extrinsic::Runtime
{
    namespace
    {
        namespace GS = ECS::Components::GeometrySources;
        namespace PC = Geometry::PointCloud;
        using D = GeometryElementDomain;
        struct Watch
        {
            D Domain{};
            std::string Name{};
            std::size_t Count{};
            std::optional<Geometry::PropertyRevision> Revision{};
            bool operator==(const Watch&) const = default;
        };
        Watch Observe(const GeometryEntityAvailability& a, D domain, std::string name)
        {
            const auto* props = ResolveGeometryPropertySet(a, domain);
            return {domain, name, props ? props->Size() : 0,
                    props ? props->FindPropertyRevision(name) : std::nullopt};
        }
        Geometry::PropertySet *MutableProperties(entt::registry &raw, entt::entity entity, D domain)
        {
            auto view = GS::BuildMutableView(raw, entity);
            switch (domain)
            {
            case D::MeshVertex:
            case D::GraphNode:
            case D::PointCloudPoint:
                return view.VertexSource ? &view.VertexSource->Properties : nullptr;
            case D::MeshEdge:
            case D::GraphEdge:
                return view.EdgeSource ? &view.EdgeSource->Properties : nullptr;
            case D::MeshHalfedge:
            case D::GraphHalfedge:
                return view.HalfedgeSource ? &view.HalfedgeSource->Properties : nullptr;
            case D::MeshFace:
                return view.FaceSource ? &view.FaceSource->Properties : nullptr;
            default:
                return nullptr;
            }
        }
        D DefaultDomain(const GeometryEntityAvailability &a)
        {
            for (auto d : {D::MeshVertex, D::GraphNode, D::PointCloudPoint})
                if (SupportsGeometryElementDomain(a, d))
                    return d;
            return D::Unknown;
        }
        bool Finite(glm::vec3 p)
        {
            return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
        }
        struct BilateralWork
        {
            BilateralFilterConfig Config{};
            entt::entity Entity{};
            std::vector<Watch> Inputs{};
            Watch OutputWatch{};
            std::vector<glm::vec3> Points{}, Normals{};
            std::vector<std::uint32_t> Slots{}, NeighborIds{};
            std::vector<glm::vec3> BeforeOutput{}, AfterOutput{};
            PC::BilateralFilterParams Params{};
            std::optional<EditorBilateralFilterResult> MainFailure{};
            std::shared_ptr<const SpatialIndexSnapshot> Index{};
            SpatialIndexHandle GpuIndex{};
            std::shared_ptr<SpatialNearestBatch> Batch{};
            std::size_t NextQuery{};
            bool GpuFinished{}, Abandoned{};
            std::chrono::steady_clock::time_point GpuStarted{};
            EditorBilateralFilterResult Result{};
        };
        bool CurrentSource(const EditorGeometryProcessingContext& context, entt::entity entity,
                           std::span<const Watch> inputs)
        {
            if (!context.Scene || !context.Scene->Raw().valid(entity)) return false;
            const auto a = BuildGeometryAvailability(context.Scene->Raw(), entity);
            for (const auto& w : inputs)
                if (Observe(a, w.Domain, w.Name) != w) return false;
            return true;
        }
        bool CurrentInput(const EditorGeometryProcessingContext& context, const BilateralWork& w)
        {
            const std::array outputs{w.OutputWatch};
            return CurrentSource(context, w.Entity, w.Inputs) && CurrentSource(context, w.Entity, outputs);
        }
        enum class CapturePurpose { Execute, Readiness, Catalog };
        std::shared_ptr<BilateralWork> Capture(const EditorGeometryProcessingContext& context,
            BilateralFilterConfig c, std::string& diagnostic, CapturePurpose purpose = CapturePurpose::Execute)
        {
            auto fail = [&](std::string why) -> std::shared_ptr<BilateralWork> { diagnostic = std::move(why); return {}; };
            const auto validation = ValidateBilateralFilterConfigSection(
                SerializeBilateralFilterConfig(c), {}, kBilateralFilterConfigSectionName);
            if (!validation.Usable()) return fail(validation.Diagnostics.front().Message);
            if (!context.Scene) return fail("Scene is unavailable.");
            const auto entity = GeometryProcessingDetail::ResolveEditorStableEntity(context.Scene->Raw(), c.StableEntityId);
            if (!entity) return fail("Bilateral target entity is stale or missing.");
            const auto a = BuildGeometryAvailability(context.Scene->Raw(), *entity);
            if (c.Positions.Domain == D::Unknown) c.Positions.Domain = DefaultDomain(a);
            if (c.Output.Domain == D::Unknown) c.Output.Domain = c.Positions.Domain;
            if (c.Normals.Domain == D::Unknown) c.Normals.Domain = c.Positions.Domain;
            const auto* props = ResolveGeometryPropertySet(a, c.Positions.Domain);
            if (!props || !ResolveGeometryProperty(a, c.Positions, props->Size(), false).Resolved())
                return fail("Choose a count-matched vec3 position property on a resolved element domain.");
            for (const auto& output : {c.Output})
            {
                if (output.Domain != c.Positions.Domain || (output.Name == c.Normals.Name && c.Normals.Name != c.Positions.Name))
                    return fail("Filtered positions must share the input domain and cannot overwrite a distinct normal input.");
                for (const auto* reserved : {"v:deleted", "e:deleted", "h:deleted", "f:deleted", "v:halfedge",
                     "e:v0", "e:v1", "h:to_vertex", "h:next", "h:prev", "h:opposite", "h:face", "f:halfedge", "h:connectivity"})
                    if (output.Name == reserved) return fail("Filtered positions cannot replace topology/deletion properties.");
                if (props->Exists(output.Name) && !ResolveGeometryProperty(a, output, props->Size(), false).Resolved())
                    return fail("Filtered positions must be absent or count-matched vec3 position properties.");
            }
            if (c.Normals.Domain != c.Positions.Domain ||
                !ResolveGeometryProperty(a,c.Normals,props->Size(),false).Resolved())
                return fail("Choose count-matched vec3 normals on the position domain.");
            if (props->Size() > std::numeric_limits<std::uint32_t>::max()) return fail("Input exceeds the supported slot range.");
            auto w = std::make_shared<BilateralWork>();
            w->Config = c; w->Entity = *entity;
            w->Result.RequestedBackend = c.Backend;
            w->Result.Output = c.Output; w->Result.SlotCount = props->Size();
            w->Inputs.push_back(Observe(a, c.Positions.Domain, c.Positions.Name));
            w->Inputs.push_back(Observe(a, c.Normals.Domain, c.Normals.Name));
            w->OutputWatch = Observe(a, c.Output.Domain, c.Output.Name);
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
            w->Inputs.push_back(Observe(a, deletionDomain, deletionName));
            const auto points = props->Get<glm::vec3>(c.Positions.Name);
            const auto normals = props->Get<glm::vec3>(c.Normals.Name);
            bool validLbvh = true;
            for (std::uint32_t i = 0; i < props->Size(); ++i)
            {
                if (deleted && deleted[i / divisor]) continue;
                if (!Finite(points[i]) || !Finite(normals[i])) return fail("Live positions and normals must be finite.");
                validLbvh &= Geometry::PointLBVH::ValidPoint(points[i]);
                ++w->Result.LiveCount;
                if (purpose == CapturePurpose::Execute) { w->Points.push_back(points[i]); w->Normals.push_back(normals[i]); w->Slots.push_back(i); }
            }
            if (w->Result.LiveCount < 2) return fail("Bilateral filtering requires at least two live samples.");
            if (purpose == CapturePurpose::Catalog) return w;
            if (c.Backend != BilateralFilterBackend::CpuOctree)
            {
                if (!context.SpatialIndices || !validLbvh || w->Result.LiveCount > (1u << 24))
                    return fail("LBVH requires the spatial cache, at most 2^24 samples and coordinates within 1e18.");
                if (c.Backend == BilateralFilterBackend::VulkanLBVH)
                {
                    if (!context.JobCommands.Available() || !context.SpatialIndices->GpuQueriesAvailable())
                        return fail("Vulkan bilateral filter neighborhoods require the framed spatial cache and job service.");
                    if (w->Result.LiveCount > (1u << 20) ||
                        c.KNeighbors > 63)
                        return fail("Vulkan bilateral filter queries support at most 2^20 live samples and k<=63 (64 candidates including self).");
                }
            }
            if (purpose == CapturePurpose::Execute)
            {
                if (w->OutputWatch.Revision) w->BeforeOutput = props->Get<glm::vec3>(c.Output.Name).Vector();
                w->AfterOutput = w->OutputWatch.Revision ? w->BeforeOutput : points.Vector();
            }
            return w;
        }
        bool Prepare(BilateralWork& w)
        {
            const auto& c=w.Config;
            w.Params={.KNeighbors=c.KNeighbors,.SpatialSigma=c.SpatialSigma,.NormalSigma=c.NormalSigma,.Iterations=1};
            w.Result.ActualBackend=ToString(c.Backend);
            if(c.Iterations && w.Params.SpatialSigma<=0)
            {
                const auto stats=PC::ComputeStatistics(w.Points,{.SpacingSampleCount=std::min(w.Points.size(),std::size_t{500})});
                if(!stats){w.Result.Status=EditorCommandStatus::GeometryProcessingFailed;w.Result.Message="Automatic bilateral bandwidth failed.";return false;}
                w.Params.SpatialSigma=stats->AverageSpacing>0?2.f*stats->AverageSpacing:.01f;
            }
            w.Result.SpatialSigmaUsed=w.Params.SpatialSigma;
            w.Result.Status=EditorCommandStatus::Applied;
            return true;
        }
        void ComputeStep(BilateralWork& w)
        {
            const auto started=std::chrono::steady_clock::now();
            auto& r=w.Result;
            std::optional<PC::BilateralFilterOutput> filtered;
            if(w.Config.Backend==BilateralFilterBackend::CpuOctree)
                filtered=PC::BilateralFilter(w.Points,w.Normals,w.Params);
            else
            {
                if(w.Config.Backend==BilateralFilterBackend::CpuLBVH)
                {
                    Geometry::PointLBVH::Index rebuilt;
                    const auto* index=w.Index?&w.Index->Index:nullptr;
                    if(r.CompletedIterations)
                    {
                        if(!rebuilt.Build(w.Points)){r.Status=EditorCommandStatus::GeometryProcessingFailed;r.Message="Moving points exceed LBVH limits.";return;}
                        index=&rebuilt;++r.WorkspaceBuilds;
                    }
                    const auto width=std::min(w.Points.size()-1,std::size_t(w.Config.KNeighbors))+1;
                    w.NeighborIds.clear();w.NeighborIds.reserve(w.Points.size()*width);
                    if(!index){r.Status=EditorCommandStatus::GeometryProcessingFailed;r.Message="Bilateral CPU index is unavailable.";return;}
                    for(auto point:w.Points)
                    {
                        const auto row=index->KNearest(point,std::uint32_t(width));
                        if(row.size()!=width){r.Status=EditorCommandStatus::GeometryProcessingFailed;r.Message="Incomplete CPU bilateral neighborhood.";return;}
                        for(const auto& n:row)w.NeighborIds.push_back(n.Index);
                    }
                }
                filtered=PC::BilateralFilterStepFromNeighbors(w.Points,w.Normals,w.NeighborIds,w.Params);
            }
            if(!filtered){r.Status=EditorCommandStatus::GeometryProcessingFailed;r.Message="Bilateral filtering failed: invalid neighborhoods or unrepresentable float updates.";return;}
            w.Points=std::move(filtered->Positions);r.Diagnostics=filtered->Diagnostics;
            ++r.CompletedIterations;r.Status=EditorCommandStatus::Applied;
            r.CpuComputeMilliseconds+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
        }
        void CompleteOutput(BilateralWork& w)
        {
            if(w.Result.Status!=EditorCommandStatus::Applied)return;
            for(std::size_t i=0;i<w.Slots.size();++i)w.AfterOutput[w.Slots[i]]=w.Points[i];
            w.Result.WrittenCount=w.Slots.size();
            w.Result.Message="Bilateral point filtering completed using "+w.Result.ActualBackend+" neighborhoods and CPU updates with fixed normals.";
        }
        void Compute(BilateralWork& w)
        {
            if(!Prepare(w))return;
            for(std::uint32_t i=0;i<w.Config.Iterations;++i)
            {ComputeStep(w);if(w.Result.Status!=EditorCommandStatus::Applied)return;}
            CompleteOutput(w);
        }
        bool AdvanceGpu(const EditorGeometryProcessingContext& context, BilateralWork& w)
        {
            auto fail=[&](std::string why, EditorCommandStatus status=EditorCommandStatus::GeometryProcessingFailed)
            {w.Result.Status=status; w.Result.Message=std::move(why); w.MainFailure=w.Result; w.Batch.reset(); return true;};
            if (w.Abandoned || !CurrentInput(context,w))
                return fail("Bilateral inputs changed or the job was cancelled.",EditorCommandStatus::StaleEntity);
            if (w.GpuFinished) return true;
            if (!w.GpuIndex.Value)
            {
                auto workspace=context.SpatialIndices->CreateWorkspace(w.Points);
                if(!workspace.Ready())return fail(workspace.Diagnostic);
                w.GpuIndex=workspace.Handle;w.Index=std::move(workspace.Snapshot);
                ++w.Result.WorkspaceBuilds;
            }
            if (w.GpuStarted==std::chrono::steady_clock::time_point{}) w.GpuStarted=std::chrono::steady_clock::now();
            if (w.Batch)
            {
                if (w.Batch->State==SpatialQueryState::Failed) return fail(w.Batch->Diagnostic);
                if (w.Batch->State!=SpatialQueryState::Ready) return false;
                for (std::size_t row=0;row<w.Batch->Counts.size();++row)
                {
                    const auto width=std::min<std::size_t>(w.Points.size()-1,w.Config.KNeighbors)+1;
                    if(w.Batch->Counts[row]!=width) return fail("Incomplete Vulkan kNN neighborhood.");
                    for(std::size_t j=0;j<width;++j)
                    {
                        const auto id=w.Batch->Neighbors[row*w.Batch->Capacity+j].Index;
                        if(w.Result.CompletedIterations)
                        {
                            if(id>=w.Points.size())return fail("Invalid private Vulkan neighbor row.");
                            w.NeighborIds.push_back(id);
                        }
                        else
                        {
                            const auto found=std::lower_bound(w.Slots.begin(),w.Slots.end(),id);
                            if(found==w.Slots.end() || *found!=id)return fail("Invalid Vulkan source row.");
                            w.NeighborIds.push_back(std::uint32_t(found-w.Slots.begin()));
                        }
                    }
                }
                w.NextQuery+=w.Batch->Counts.size();
                if (w.NextQuery==w.Points.size())
                {
                    w.Batch.reset(); w.GpuFinished=true;
                    w.Result.GpuNeighborhoodMilliseconds+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-w.GpuStarted).count();
                    return true;
                }
            }
            const auto count=std::min<std::size_t>(w.Config.GpuQueryBatchSize,w.Points.size()-w.NextQuery);
            if (w.Batch && w.Batch->Counts.size()!=count) w.Batch.reset();
            const auto queries=std::span(w.Points).subspan(w.NextQuery,count);
            w.Batch=context.SpatialIndices->QueueGpuKNearest(w.GpuIndex,queries,
                std::uint32_t(std::min<std::size_t>(w.Points.size()-1,w.Config.KNeighbors)+1),{},std::move(w.Batch));
            ++w.Result.GpuQueryBatches;
            if (w.Batch->State==SpatialQueryState::Failed) return fail(w.Batch->Diagnostic);
            return false;
        }
        EditorBilateralFilterResult Publish(const EditorGeometryProcessingContext& context,
                                            const std::shared_ptr<BilateralWork>& w)
        {
            auto& r=w->Result;
            if (!CurrentInput(context,*w))
            {r.Status=EditorCommandStatus::StaleEntity; r.Message="Bilateral input or output changed before publication."; return r;}
            if (r.Status!=EditorCommandStatus::Applied) return r;
            struct State { bool Exists{};std::vector<glm::vec3> Values{}; };
            auto before=std::make_shared<State>(State{bool(w->OutputWatch.Revision),w->BeforeOutput});
            auto after=std::make_shared<State>(State{true,w->AfterOutput});
            auto revisions=std::make_shared<std::array<Watch,1>>(std::array{w->OutputWatch});
            auto inputs=w->Inputs;
            std::erase_if(inputs,[&](const auto& input){return input.Domain==w->Config.Output.Domain && input.Name==w->Config.Output.Name;});
            const auto mutate=[context,entity=w->Entity,inputs=std::move(inputs),c=w->Config,revisions](const State& target)
            {
                if (!CurrentSource(context,entity,inputs) || !CurrentSource(context,entity,*revisions))
                    return EditorCommandHistoryStatus::StaleEntity;
                auto* props=MutableProperties(context.Scene->Raw(),entity,c.Positions.Domain);
                if (target.Exists) props->GetOrAdd<glm::vec3>(c.Output.Name).Vector()=target.Values;
                else if (auto p=props->Get<glm::vec3>(c.Output.Name)) props->Remove(p);
                const auto a=BuildGeometryAvailability(context.Scene->Raw(),entity);
                *revisions={Observe(a,c.Output.Domain,c.Output.Name)};
                ECS::Components::DirtyTags::MarkGpuDirty(context.Scene->Raw(),entity);
                ECS::Components::DirtyTags::MarkVertexPositionsDirty(context.Scene->Raw(),entity);
                if (context.InvalidateWorkspaceSnapshotCache) context.InvalidateWorkspaceSnapshotCache();
                return EditorCommandHistoryStatus::Applied;
            };
            const auto status=context.CommandHistory ? context.CommandHistory->Execute({.Label="Bilateral point filter",
                .Redo=[mutate,after]{return mutate(*after);},.Undo=[mutate,before]{return mutate(*before);}}).Status : mutate(*after);
            r.Status=GeometryProcessingDetail::ToEditorMethodCommandStatus(status);
            if (!r.Succeeded()) r.Message="Bilateral publication rejected by history checks.";
            return r;
        }
    }
    EditorBilateralFilterReadiness PreviewEditorBilateralFilterCommand(
        const EditorGeometryProcessingContext& context,const BilateralFilterConfig& config)
    {
        EditorBilateralFilterReadiness r;
        auto w=Capture(context,config,r.Diagnostic,CapturePurpose::Readiness);
        r.Ready=bool(w);if(w)r.Resolved=w->Config;return r;
    }
    GeometryPropertyCatalogSnapshot GetEditorBilateralFilterInputCatalog(
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
            BilateralFilterConfig c;c.StableEntityId=id;c.Positions=entry.Ref;c.Normals=entry.Ref;c.Output.Domain=entry.Ref.Domain;
            c.Output.Name=entry.Ref.Name+".filtered";
            while(props->Exists(c.Output.Name))c.Output.Name+="_";
            std::string diagnostic;return !Capture(context,c,diagnostic,CapturePurpose::Catalog);
        });
        return catalog;
    }
    EditorBilateralFilterResult ApplyEditorBilateralFilterCommand(
        const EditorGeometryProcessingContext &context, const BilateralFilterConfig &config)
    {
        std::string diagnostic;
        auto w = Capture(context, config, diagnostic);
        const auto report = [&config, &w](EditorCommandStatus status, std::string message) {
            auto result = w ? w->Result
                            : EditorBilateralFilterResult{.RequestedBackend = config.Backend, .Output = config.Output};
            result.Status = status;
            result.Message = std::move(message);
            return result;
        };
        if (!w)
            return report(EditorCommandStatus::InvalidProcessingParameters, std::move(diagnostic));
        if (w->Config.Iterations && w->Config.Backend != BilateralFilterBackend::CpuOctree)
        {
            auto acquired = context.SpatialIndices->Acquire(context.World, w->Entity, w->Config.Positions);
            if (!acquired.Ready())
                return report(EditorCommandStatus::InvalidProcessingParameters, acquired.Diagnostic);
            w->GpuIndex = acquired.Handle;
            w->Index = context.SpatialIndices->Snapshot(acquired.Handle);
            w->Result.IndexReused = acquired.Reused;
            if (!w->Index || w->Index->Slots != w->Slots ||
                w->Index->Index.Points().size() != w->Points.size() ||
                !std::equal(w->Points.begin(), w->Points.end(), w->Index->Index.Points().begin()))
                return report(EditorCommandStatus::StaleEntity,
                              "Bilateral index snapshot does not match the selected samples.");
        }
        if (!context.JobCommands.Available())
        {
            Compute(*w);
            return Publish(context, w);
        }
        const EditorJobIdentity identity{.EntityId = config.StableEntityId,
                                         .Scope = ToEditorJobScope(w->Config.Output.Domain),
                                         .OutputSemantic = GeometryPresentationSlotSemantic::Displacement,
                                         .OutputName = w->Config.Output.Name};
        if (context.JobCommands.FindActive)
            if (auto active = context.JobCommands.FindActive(identity);
                active && IsActiveEditorJobState(active->State))
                return report(EditorCommandStatus::Pending,
                              "A bilateral filter job for this output is already active.");
        auto sink = context.MethodResultSinks.BilateralFilter;
        auto delivered = std::make_shared<bool>(false);
        auto pending = w->Result;
        pending.Status = EditorCommandStatus::Pending;
        pending.Message = "Bilateral filtering queued.";
        auto finalize=[sink,delivered,w,pending]() mutable
        {
            w->Abandoned=true;
            if(sink && !*delivered)
            {
                *delivered=true;
                if(w->MainFailure)pending=*w->MainFailure;
                else {pending.Status=EditorCommandStatus::StaleEntity;pending.Message="Bilateral job cancelled or source stale; previous positions retained.";}
                sink(std::move(pending));
            }
        };
        auto validate=[context,w]{if(w->Abandoned)return JobApplyValidation::Cancelled;return CurrentInput(context,*w)?JobApplyValidation::Current:JobApplyValidation::StaleGeneration;};
        auto publish=[context,w,sink,delivered](KernelEventBus&,const JobResultEnvelope&)
        {
            if(w->Abandoned){w->Result.Status=EditorCommandStatus::StaleEntity;w->Result.Message="Bilateral sequence was abandoned; previous positions retained.";}
            else CompleteOutput(*w);
            auto result=Publish(context,w);*delivered=true;if(sink)sink(result);return result.Succeeded();
        };
        JobToken previous{};
        auto submit=[&](JobDesc desc)
        {
            if(previous.IsValid())desc.DependsOn.push_back({previous,"Complete the previous bilateral stage before this stage"});
            previous=context.JobCommands.Submit(std::move(desc),identity);
            return previous.IsValid();
        };
        bool submitted=true;
        if(w->Config.Backend!=BilateralFilterBackend::VulkanLBVH || !w->Config.Iterations)
        {
            submitted=submit({.DebugName="Bilateral point filter",.Scope=context.World,.Kind=RuntimeTaskKinds::GeometryProcess,
                .Work=[w](const JobCancellation&){Compute(*w);return JobResultEnvelope::Make(true);},
                .ValidateBeforeApply=validate,.PublishCompletion=publish,.FinalizeUnpublishedOnMainThread=finalize});
        }
        else
        {
            submitted=submit({.DebugName="Bilateral bandwidth",.Scope=context.World,.Kind=RuntimeTaskKinds::GeometryProcess,
                .Work=[w](const JobCancellation&){return JobResultEnvelope::Make(Prepare(*w));},
                .ValidateBeforeApply=validate,
                .PublishCompletion=[w](KernelEventBus&,const JobResultEnvelope&){if(w->Result.Status!=EditorCommandStatus::Applied){w->MainFailure=w->Result;return false;}return true;},
                .FinalizeUnpublishedOnMainThread=[w]{w->Abandoned=true;}});
            for(std::uint32_t i=0;submitted && i<w->Config.Iterations;++i)
            {
                submitted=submit({.DebugName="Bilateral neighborhoods (Vulkan)",.Scope=context.World,.Kind=RuntimeTaskKinds::GeometryProcess,
                    .Work=[](const JobCancellation&){return JobResultEnvelope::Make(true);},
                    .IsReadyToApply=[context,w]{return AdvanceGpu(context,*w);},.ValidateBeforeApply=validate,
                    .PublishCompletion=[w](KernelEventBus&,const JobResultEnvelope&){return w->GpuFinished;},
                    .FinalizeUnpublishedOnMainThread=[w]{w->Abandoned=true;}});
                if(!submitted)break;
                const bool last=i+1==w->Config.Iterations;
                JobDesc step{.DebugName="Bilateral position update",.Scope=context.World,.Kind=RuntimeTaskKinds::GeometryProcess,
                    .Work=[w](const JobCancellation&){ComputeStep(*w);return JobResultEnvelope::Make(true);},
                    .ValidateBeforeApply=validate};
                if(last){step.PublishCompletion=publish;step.FinalizeUnpublishedOnMainThread=finalize;}
                else
                {
                    step.PublishCompletion=[w](KernelEventBus&,const JobResultEnvelope&){
                        if(w->Result.Status!=EditorCommandStatus::Applied){w->MainFailure=w->Result;return false;}
                        w->GpuIndex={};w->Index.reset();w->Batch.reset();w->NextQuery=0;w->NeighborIds.clear();
                        w->GpuFinished=false;w->GpuStarted={};return true;};
                    step.FinalizeUnpublishedOnMainThread=[w]{w->Abandoned=true;};
                }
                submitted=submit(std::move(step));
            }
        }
        if(!submitted)
        {
            w->Abandoned=true;pending.Status=EditorCommandStatus::GeometryProcessingFailed;
            pending.Message="Bilateral job sequence submission was rejected.";
        }
        return pending;
    }
    EditorBilateralFilterResult ApplyEditorConfiguredBilateralFilter(
        const EditorGeometryProcessingContext &context)
    {
        const auto config = GetEditorBilateralFilterConfig(context);
        if (!config)
            return {.Status = EditorCommandStatus::InvalidProcessingParameters,
                    .Message = "Bilateral filtering config is unavailable."};
        return ApplyEditorBilateralFilterCommand(context, *config);
    }
} // namespace Extrinsic::Runtime

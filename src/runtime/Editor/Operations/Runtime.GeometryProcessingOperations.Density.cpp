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
        struct DensityWork
        {
            KernelDensityConfig Config{};
            entt::entity Entity{};
            std::vector<Watch> Inputs{};
            Watch DensityWatch{};
            std::vector<glm::vec3> Points{};
            std::vector<std::uint32_t> Slots{}, NeighborIds{};
            std::vector<float> BeforeDensity{}, AfterDensity{};
            std::shared_ptr<const SpatialIndexSnapshot> Index{};
            SpatialIndexHandle GpuIndex{};
            std::shared_ptr<SpatialNearestBatch> Batch{};
            std::size_t NextQuery{};
            bool GpuFinished{}, Abandoned{};
            std::chrono::steady_clock::time_point GpuStarted{};
            EditorKernelDensityResult Result{};
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
        bool CurrentInput(const EditorGeometryProcessingContext& context, const DensityWork& w)
        {
            const std::array outputs{w.DensityWatch};
            return CurrentSource(context, w.Entity, w.Inputs) && CurrentSource(context, w.Entity, outputs);
        }
        enum class CapturePurpose { Execute, Readiness, Catalog };
        std::shared_ptr<DensityWork> Capture(const EditorGeometryProcessingContext& context,
            KernelDensityConfig c, std::string& diagnostic, CapturePurpose purpose = CapturePurpose::Execute)
        {
            auto fail = [&](std::string why) -> std::shared_ptr<DensityWork> { diagnostic = std::move(why); return {}; };
            const auto validation = ValidateKernelDensityConfigSection(
                SerializeKernelDensityConfig(c), {}, kKernelDensityConfigSectionName);
            if (!validation.Usable()) return fail(validation.Diagnostics.front().Message);
            if (!context.Scene) return fail("Scene is unavailable.");
            const auto entity = GeometryProcessingDetail::ResolveEditorStableEntity(context.Scene->Raw(), c.StableEntityId);
            if (!entity) return fail("Density target entity is stale or missing.");
            const auto a = BuildGeometryAvailability(context.Scene->Raw(), *entity);
            if (c.Positions.Domain == D::Unknown) c.Positions.Domain = DefaultDomain(a);
            if (c.Density.Domain == D::Unknown) c.Density.Domain = c.Positions.Domain;
            const auto* props = ResolveGeometryPropertySet(a, c.Positions.Domain);
            if (!props || !ResolveGeometryProperty(a, c.Positions, props->Size(), false).Resolved())
                return fail("Choose a count-matched vec3 position property on a resolved element domain.");
            for (const auto& output : {c.Density})
            {
                if (output.Domain != c.Positions.Domain || output.Name == c.Positions.Name)
                    return fail("Density outputs must be distinct properties on the input domain.");
                for (const auto* reserved : {"v:deleted", "e:deleted", "h:deleted", "f:deleted", "v:halfedge",
                     "e:v0", "e:v1", "h:to_vertex", "h:next", "h:prev", "h:opposite", "h:face", "f:halfedge", "h:connectivity"})
                    if (output.Name == reserved) return fail("Density outputs cannot replace topology/deletion properties.");
                if (props->Exists(output.Name) && !ResolveGeometryProperty(a, output, props->Size(), false).Resolved())
                    return fail("Density outputs must be absent or count-matched float density properties.");
            }
            if (props->Size() > std::numeric_limits<std::uint32_t>::max()) return fail("Input exceeds the supported slot range.");
            auto w = std::make_shared<DensityWork>();
            w->Config = c; w->Entity = *entity;
            w->Result.RequestedBackend = c.Backend;
            w->Result.Density = c.Density; w->Result.SlotCount = props->Size();
            w->Inputs.push_back(Observe(a, c.Positions.Domain, c.Positions.Name));
            w->DensityWatch = Observe(a, c.Density.Domain, c.Density.Name);
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
            bool validLbvh = true;
            for (std::uint32_t i = 0; i < props->Size(); ++i)
            {
                if (deleted && deleted[i / divisor]) continue;
                if (!Finite(points[i])) return fail("Live position samples must be finite.");
                validLbvh &= Geometry::PointLBVH::ValidPoint(points[i]);
                ++w->Result.LiveCount;
                if (purpose == CapturePurpose::Execute) { w->Points.push_back(points[i]); w->Slots.push_back(i); }
            }
            if (w->Result.LiveCount < 2) return fail("Kernel density requires at least two live samples.");
            if (purpose == CapturePurpose::Catalog) return w;
            if (c.Backend != KernelDensityBackend::CpuOctree)
            {
                if (!context.SpatialIndices || !validLbvh || w->Result.LiveCount > (1u << 24))
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
            if (purpose == CapturePurpose::Execute)
            {
                if (w->DensityWatch.Revision) w->BeforeDensity = props->Get<float>(c.Density.Name).Vector();
                w->AfterDensity = w->DensityWatch.Revision ? w->BeforeDensity : std::vector<float>(props->Size());
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
                if (c.Backend == KernelDensityBackend::CpuLBVH)
                {
                    w.NeighborIds.reserve(w.Points.size()*width);
                    for (auto point : w.Points)
                    {
                        const auto row=w.Index->Index.KNearest(point,std::uint32_t(width));
                        if (row.size()!=width) {r.Message="Incomplete CPU kNN neighborhood.";return;}
                        for (const auto& n : row) w.NeighborIds.push_back(n.Index);
                    }
                }
                analysis = PC::EstimateKernelDensityFromNeighbors(w.Points,w.NeighborIds,params);
            }
            if (!analysis || analysis->Densities.size()!=w.Slots.size())
            {r.Message="Kernel density failed: invalid neighborhoods or unrepresentable float kernel values/bandwidth.";return;}
            r.UsedBandwidth=analysis->UsedBandwidth;r.MeanDensity=analysis->MeanDensity;
            r.MinDensity=analysis->MinDensity;r.MaxDensity=analysis->MaxDensity;
            for (std::size_t i=0;i<w.Slots.size();++i) w.AfterDensity[w.Slots[i]]=analysis->Densities[i];
            r.WrittenCount=w.Slots.size();r.Status=EditorCommandStatus::Applied;
            r.CpuComputeMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
            r.Message="Density computed using "+r.ActualBackend+" neighborhoods and CPU bandwidth/Gaussian evaluation.";
        }
        bool AdvanceGpu(const EditorGeometryProcessingContext& context, DensityWork& w)
        {
            auto fail=[&](std::string why, EditorCommandStatus status=EditorCommandStatus::GeometryProcessingFailed)
            {w.Result.Status=status; w.Result.Message=std::move(why); w.Batch.reset(); return true;};
            if (w.Abandoned || !CurrentInput(context,w))
                return fail("Density inputs changed or the job was cancelled.",EditorCommandStatus::StaleEntity);
            if (w.GpuFinished) return true;
            if (w.GpuStarted==std::chrono::steady_clock::time_point{}) w.GpuStarted=std::chrono::steady_clock::now();
            if (w.Batch)
            {
                if (w.Batch->State==SpatialQueryState::Failed) return fail(w.Batch->Diagnostic);
                if (w.Batch->State!=SpatialQueryState::Ready) return false;
                for (std::size_t row=0;row<w.Batch->Counts.size();++row)
                {
                    const auto width=std::min<std::size_t>(w.Points.size(),std::max<std::size_t>(w.Config.KNeighbors,2)+1);
                    if(w.Batch->Counts[row]!=width) return fail("Incomplete Vulkan kNN neighborhood.");
                    for(std::size_t j=0;j<width;++j)
                    {
                        const auto id=w.Batch->Neighbors[row*w.Batch->Capacity+j].Index;
                        const auto found=std::lower_bound(w.Slots.begin(),w.Slots.end(),id);
                        if(found==w.Slots.end() || *found!=id) return fail("Invalid Vulkan neighbor source row.");
                        w.NeighborIds.push_back(std::uint32_t(found-w.Slots.begin()));
                    }
                }
                w.NextQuery+=w.Batch->Counts.size();
                if (w.NextQuery==w.Points.size())
                {
                    w.Batch.reset(); w.GpuFinished=true;
                    w.Result.GpuNeighborhoodMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-w.GpuStarted).count();
                    return true;
                }
            }
            const auto count=std::min<std::size_t>(w.Config.GpuQueryBatchSize,w.Points.size()-w.NextQuery);
            if (w.Batch && w.Batch->Counts.size()!=count) w.Batch.reset();
            const auto queries=std::span(w.Points).subspan(w.NextQuery,count);
            w.Batch=context.SpatialIndices->QueueGpuKNearest(w.GpuIndex,queries,
                std::uint32_t(std::min<std::size_t>(w.Points.size(),std::max<std::size_t>(w.Config.KNeighbors,2)+1)),{},std::move(w.Batch));
            ++w.Result.GpuQueryBatches;
            if (w.Batch->State==SpatialQueryState::Failed) return fail(w.Batch->Diagnostic);
            return false;
        }
        EditorKernelDensityResult Publish(const EditorGeometryProcessingContext& context,
                                            const std::shared_ptr<DensityWork>& w)
        {
            auto& r=w->Result;
            if (!CurrentInput(context,*w))
            {r.Status=EditorCommandStatus::StaleEntity; r.Message="Density input or output changed before publication."; return r;}
            if (r.Status!=EditorCommandStatus::Applied) return r;
            struct State { bool Exists{};std::vector<float> Values{}; };
            auto before=std::make_shared<State>(State{bool(w->DensityWatch.Revision),w->BeforeDensity});
            auto after=std::make_shared<State>(State{true,w->AfterDensity});
            auto revisions=std::make_shared<std::array<Watch,1>>(std::array{w->DensityWatch});
            const auto mutate=[context,entity=w->Entity,inputs=w->Inputs,c=w->Config,revisions](const State& target)
            {
                if (!CurrentSource(context,entity,inputs) || !CurrentSource(context,entity,*revisions))
                    return EditorCommandHistoryStatus::StaleEntity;
                auto* props=MutableProperties(context.Scene->Raw(),entity,c.Positions.Domain);
                if (target.Exists) props->GetOrAdd<float>(c.Density.Name).Vector()=target.Values;
                else if (auto p=props->Get<float>(c.Density.Name)) props->Remove(p);
                const auto a=BuildGeometryAvailability(context.Scene->Raw(),entity);
                *revisions={Observe(a,c.Density.Domain,c.Density.Name)};
                ECS::Components::DirtyTags::MarkGpuDirty(context.Scene->Raw(),entity);
                ECS::Components::DirtyTags::MarkVertexAttributesDirty(context.Scene->Raw(),entity);
                if (context.InvalidateWorkspaceSnapshotCache) context.InvalidateWorkspaceSnapshotCache();
                return EditorCommandHistoryStatus::Applied;
            };
            const auto status=context.CommandHistory ? context.CommandHistory->Execute({.Label="Estimate kernel density",
                .Redo=[mutate,after]{return mutate(*after);},.Undo=[mutate,before]{return mutate(*before);}}).Status : mutate(*after);
            r.Status=GeometryProcessingDetail::ToEditorMethodCommandStatus(status);
            if (!r.Succeeded()) r.Message="Density publication rejected by history checks.";
            return r;
        }
    }
    EditorKernelDensityReadiness PreviewEditorKernelDensityCommand(
        const EditorGeometryProcessingContext& context,const KernelDensityConfig& config)
    {
        EditorKernelDensityReadiness r;
        auto w=Capture(context,config,r.Diagnostic,CapturePurpose::Readiness);
        r.Ready=bool(w);if(w)r.Resolved=w->Config;return r;
    }
    GeometryPropertyCatalogSnapshot GetEditorKernelDensityInputCatalog(
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
            KernelDensityConfig c;c.StableEntityId=id;c.Positions=entry.Ref;c.Density.Domain=entry.Ref.Domain;
            c.Density.Name=entry.Ref.Name+".density";
            while(props->Exists(c.Density.Name))c.Density.Name+="_";
            std::string diagnostic;return !Capture(context,c,diagnostic,CapturePurpose::Catalog);
        });
        return catalog;
    }
    EditorKernelDensityResult ApplyEditorKernelDensityCommand(
        const EditorGeometryProcessingContext &context, const KernelDensityConfig &config)
    {
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
        if (context.JobCommands.FindActive)
            if (auto active = context.JobCommands.FindActive(identity);
                active && IsActiveEditorJobState(active->State))
                return report(EditorCommandStatus::Pending,
                              "A density job for this output is already active.");
        auto sink = context.MethodResultSinks.KernelDensity;
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
                .PublishCompletion = [w](KernelEventBus&, const JobResultEnvelope&) { return w->GpuFinished; },
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
        const EditorGeometryProcessingContext &context)
    {
        const auto config = GetEditorKernelDensityConfig(context);
        if (!config)
            return {.Status = EditorCommandStatus::InvalidProcessingParameters,
                    .Message = "Density estimation config is unavailable."};
        return ApplyEditorKernelDensityCommand(context, *config);
    }
} // namespace Extrinsic::Runtime

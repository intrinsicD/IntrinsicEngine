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
        // Transient provenance for the most recent detection on this entity. Persisted masks
        // remain visible after reload, but removal requires detection against the live source.
        struct AnalysisStamp
        {
            GeometryPropertyRef Positions{}, Mask{};
            std::vector<Watch> Inputs{};
            Watch MaskWatch{};
        };
        struct OutlierWork
        {
            OutlierAnalysisConfig Config{};
            entt::entity Entity{};
            std::vector<Watch> Inputs{};
            Watch MaskWatch{}, ScoreWatch{};
            std::vector<glm::vec3> Points{};
            std::vector<std::uint32_t> Slots{}, BeforeMask{}, AfterMask{}, Counts{}, NeighborIds{};
            std::vector<float> BeforeScore{}, AfterScore{};
            std::shared_ptr<const SpatialIndexSnapshot> Index{};
            SpatialIndexHandle GpuIndex{};
            std::shared_ptr<SpatialNearestBatch> Batch{};
            std::size_t NextQuery{};
            bool GpuFinished{}, Abandoned{};
            std::chrono::steady_clock::time_point GpuStarted{};
            EditorOutlierAnalysisResult Result{};
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
        bool CurrentInput(const EditorGeometryProcessingContext& context, const OutlierWork& w)
        {
            const std::array outputs{w.MaskWatch, w.ScoreWatch};
            return CurrentSource(context, w.Entity, w.Inputs) && CurrentSource(context, w.Entity, outputs);
        }
        bool CurrentStamp(const EditorGeometryProcessingContext& context, entt::entity entity,
                          const OutlierAnalysisConfig& c)
        {
            const auto* stamp = context.Scene->Raw().try_get<AnalysisStamp>(entity);
            return stamp && stamp->Positions == c.Positions && stamp->Mask == c.Mask &&
                CurrentSource(context, entity, stamp->Inputs) &&
                CurrentSource(context, entity, std::span(&stamp->MaskWatch, 1));
        }
        enum class CapturePurpose { Execute, Readiness, Catalog };
        std::shared_ptr<OutlierWork> Capture(const EditorGeometryProcessingContext& context,
            OutlierAnalysisConfig c, std::string& diagnostic, CapturePurpose purpose = CapturePurpose::Execute)
        {
            auto fail = [&](std::string why) -> std::shared_ptr<OutlierWork> { diagnostic = std::move(why); return {}; };
            const auto validation = ValidateOutlierAnalysisConfigSection(
                SerializeOutlierAnalysisConfig(c), {}, kOutlierAnalysisConfigSectionName);
            if (!validation.Usable()) return fail(validation.Diagnostics.front().Message);
            if (!context.Scene) return fail("Scene is unavailable.");
            const auto entity = GeometryProcessingDetail::ResolveEditorStableEntity(context.Scene->Raw(), c.StableEntityId);
            if (!entity) return fail("Outlier target entity is stale or missing.");
            const auto a = BuildGeometryAvailability(context.Scene->Raw(), *entity);
            if (c.Positions.Domain == D::Unknown) c.Positions.Domain = DefaultDomain(a);
            if (c.Mask.Domain == D::Unknown) c.Mask.Domain = c.Positions.Domain;
            if (c.Score.Domain == D::Unknown) c.Score.Domain = c.Positions.Domain;
            const auto* props = ResolveGeometryPropertySet(a, c.Positions.Domain);
            if (!props || !ResolveGeometryProperty(a, c.Positions, props->Size(), false).Resolved())
                return fail("Choose a count-matched vec3 position property on a resolved element domain.");
            for (const auto& output : {c.Mask, c.Score})
            {
                if (output.Domain != c.Positions.Domain || output.Name == c.Positions.Name)
                    return fail("Outlier outputs must be distinct properties on the input domain.");
                for (const auto* reserved : {"v:deleted", "e:deleted", "h:deleted", "f:deleted", "v:halfedge",
                     "e:v0", "e:v1", "h:to_vertex", "h:next", "h:prev", "h:opposite", "h:face", "f:halfedge", "h:connectivity"})
                    if (output.Name == reserved) return fail("Outlier outputs cannot replace topology/deletion properties.");
                if (props->Exists(output.Name) && !ResolveGeometryProperty(a, output, props->Size(), false).Resolved())
                    return fail("Outlier outputs must be absent or count-matched uint32 mask / float score properties.");
            }
            if (props->Size() > std::numeric_limits<std::uint32_t>::max()) return fail("Input exceeds the supported slot range.");
            auto w = std::make_shared<OutlierWork>();
            w->Config = c; w->Entity = *entity;
            w->Result.Method = c.Method; w->Result.RequestedBackend = c.Backend; w->Result.Operation = c.Operation;
            w->Result.Mask = c.Mask; w->Result.Score = c.Score; w->Result.SlotCount = props->Size();
            w->Inputs.push_back(Observe(a, c.Positions.Domain, c.Positions.Name));
            w->MaskWatch = Observe(a, c.Mask.Domain, c.Mask.Name);
            w->ScoreWatch = Observe(a, c.Score.Domain, c.Score.Name);
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
            if (!w->Result.LiveCount) return fail("Outlier analysis requires live input samples.");
            if (purpose == CapturePurpose::Catalog) return w;
            if (c.Operation == OutlierAnalysisOperation::RemoveMarked)
            {
                if (c.Positions.Domain != D::PointCloudPoint || a.SourceView.EdgeSource || a.SourceView.HalfedgeSource || a.SourceView.FaceSource)
                    return fail("Remove Marked Points requires a topology-free point cloud. Mesh/graph detection preserves topology.");
                if (!CurrentStamp(context, *entity, c)) return fail("Detect outliers again: the source or published mask changed, or this mask has no current analysis.");
                return w;
            }
            if (c.Method == OutlierAnalysisMethod::Statistical && c.KNeighbors >= w->Result.LiveCount)
                return fail("Statistical outlier analysis requires more live samples than k.");
            if (c.Backend != OutlierAnalysisBackend::CpuOctree)
            {
                if (!context.SpatialIndices || !validLbvh || w->Result.LiveCount > (1u << 24) ||
                    (c.Method == OutlierAnalysisMethod::Radius && c.Radius > Geometry::PointLBVH::CoordinateLimit))
                    return fail("LBVH requires the spatial cache, at most 2^24 samples and coordinates/radius within 1e18.");
                if (c.Backend == OutlierAnalysisBackend::VulkanLBVH)
                {
                    if (!context.JobCommands.Available() || !context.SpatialIndices->GpuQueriesAvailable())
                        return fail("Vulkan outlier neighborhoods require the framed spatial cache and job service.");
                    if (w->Result.LiveCount > (1u << 20) ||
                        (c.Method == OutlierAnalysisMethod::Statistical && c.KNeighbors > 64))
                        return fail("Vulkan outlier queries support at most 2^20 live samples and statistical k=1..64.");
                }
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
        void Compute(OutlierWork& w)
        {
            const auto started = std::chrono::steady_clock::now();
            const auto& c = w.Config;
            auto& r = w.Result;
            r.Status = EditorCommandStatus::GeometryProcessingFailed;
            r.ActualBackend = ToString(c.Backend);
            PC::OutlierAnalysisResult analysis;
            if (c.Backend == OutlierAnalysisBackend::CpuOctree)
            {
                if (c.Method == OutlierAnalysisMethod::Statistical)
                    analysis = PC::AnalyzeStatisticalOutliers(w.Points, {.KNeighbors=c.KNeighbors, .StdDevMultiplier=c.StdDevMultiplier});
                else analysis = PC::AnalyzeRadiusOutliers(w.Points, {.SearchRadius=c.Radius, .MinNeighbors=c.MinimumNeighbors});
            }
            else if (c.Method == OutlierAnalysisMethod::Radius)
            {
                if (w.Index && c.Backend == OutlierAnalysisBackend::CpuLBVH)
                    for (std::uint32_t i=0; i<w.Points.size(); ++i)
                        w.Counts.push_back(w.Index->Index.Radius(w.Points[i], c.Radius, 1, i).TotalCount);
                analysis = PC::ClassifyRadiusOutliers(w.Counts, c.MinimumNeighbors);
            }
            else
            {
                std::vector<float> means(w.Points.size());
                const auto k = std::min<std::size_t>(c.KNeighbors, w.Points.size()-1);
                for (std::uint32_t i=0; i<w.Points.size(); ++i)
                {
                    float sum=0;
                    if (c.Backend == OutlierAnalysisBackend::CpuLBVH)
                    {
                        const auto neighbors = w.Index->Index.KNearest(w.Points[i], std::uint32_t(k), i);
                        if (neighbors.size()!=k) { r.Message="Incomplete CPU kNN neighborhood."; return; }
                        for (const auto& n : neighbors) sum += glm::length(w.Points[i]-w.Points[n.Index]);
                    }
                    else
                    {
                        for (std::size_t j=0; j<k; ++j)
                        {
                            const auto id=w.NeighborIds[i*k+j];
                            const auto found=std::lower_bound(w.Slots.begin(), w.Slots.end(), id);
                            if (found==w.Slots.end() || *found!=id || id==w.Slots[i])
                            { r.Message="Invalid Vulkan neighbor source row."; return; }
                            sum += glm::length(w.Points[i]-w.Points[found-w.Slots.begin()]);
                        }
                    }
                    means[i]=sum/float(k);
                }
                analysis=PC::ClassifyStatisticalOutliers(means,c.StdDevMultiplier);
            }
            if (analysis.Status!=PC::OutlierRemovalStatus::Success || analysis.Mask.size()!=w.Slots.size())
            { r.Message="Outlier classification failed."; return; }
            r.RejectedCount=analysis.RejectedCount; r.MeanDistance=analysis.MeanDistance;
            r.StdDevDistance=analysis.StdDevDistance; r.DistanceThreshold=analysis.DistanceThreshold;
            for (std::size_t i=0; i<w.Slots.size(); ++i)
            { w.AfterMask[w.Slots[i]]=analysis.Mask[i]; w.AfterScore[w.Slots[i]]=analysis.Scores[i]; }
            r.WrittenCount=w.Slots.size(); r.Status=EditorCommandStatus::Applied;
            r.CpuComputeMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
            r.Message="Outlier mask and score computed using "+r.ActualBackend+" neighborhoods and CPU classification.";
        }
        bool AdvanceGpu(const EditorGeometryProcessingContext& context, OutlierWork& w)
        {
            auto fail=[&](std::string why, EditorCommandStatus status=EditorCommandStatus::GeometryProcessingFailed)
            {w.Result.Status=status; w.Result.Message=std::move(why); w.Batch.reset(); return true;};
            if (w.Abandoned || !CurrentInput(context,w))
                return fail("Outlier inputs changed or the job was cancelled.",EditorCommandStatus::StaleEntity);
            if (w.GpuFinished) return true;
            if (w.GpuStarted==std::chrono::steady_clock::time_point{}) w.GpuStarted=std::chrono::steady_clock::now();
            if (w.Batch)
            {
                if (w.Batch->State==SpatialQueryState::Failed) return fail(w.Batch->Diagnostic);
                if (w.Batch->State!=SpatialQueryState::Ready) return false;
                for (std::size_t row=0;row<w.Batch->Counts.size();++row)
                {
                    if (w.Config.Method==OutlierAnalysisMethod::Radius) w.Counts.push_back(w.Batch->Counts[row]);
                    else
                    {
                        if (w.Batch->Counts[row]!=std::min<std::size_t>(w.Config.KNeighbors,w.Points.size()-1))
                            return fail("Incomplete Vulkan kNN neighborhood.");
                        for (std::uint32_t j=0;j<w.Batch->Counts[row];++j)
                            w.NeighborIds.push_back(w.Batch->Neighbors[row*w.Batch->Capacity+j].Index);
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
            const auto exclusions=std::span(w.Slots).subspan(w.NextQuery,count);
            if (w.Config.Method==OutlierAnalysisMethod::Radius)
                w.Batch=context.SpatialIndices->QueueGpuRadius(w.GpuIndex,queries,w.Config.Radius,1,exclusions,std::move(w.Batch));
            else w.Batch=context.SpatialIndices->QueueGpuKNearest(w.GpuIndex,queries,
                std::uint32_t(std::min<std::size_t>(w.Config.KNeighbors,w.Points.size()-1)),exclusions,std::move(w.Batch));
            ++w.Result.GpuQueryBatches;
            if (w.Batch->State==SpatialQueryState::Failed) return fail(w.Batch->Diagnostic);
            return false;
        }
        void RestoreStamp(const EditorGeometryProcessingContext& context, entt::entity entity,
                          std::optional<AnalysisStamp> stamp, bool rebaseInputs = false,
                          const GeometryPropertyRef* restoredMask = nullptr)
        {
            auto& raw=context.Scene->Raw();
            if (!stamp) { raw.remove<AnalysisStamp>(entity); return; }
            const auto a=BuildGeometryAvailability(raw,entity);
            if (rebaseInputs || (restoredMask && stamp->Mask == *restoredMask))
                stamp->MaskWatch=Observe(a,stamp->Mask.Domain,stamp->Mask.Name);
            if (rebaseInputs)
                for (auto& watch : stamp->Inputs) watch=Observe(a,watch.Domain,watch.Name);
            raw.emplace_or_replace<AnalysisStamp>(entity,std::move(*stamp));
        }
        EditorOutlierAnalysisResult Publish(const EditorGeometryProcessingContext& context,
                                            const std::shared_ptr<OutlierWork>& w)
        {
            auto& r=w->Result;
            if (!CurrentInput(context,*w))
            {r.Status=EditorCommandStatus::StaleEntity; r.Message="Outlier input or output changed before publication."; return r;}
            if (r.Status!=EditorCommandStatus::Applied) return r;
            struct State
            {
                bool MaskExists{}, ScoreExists{};
                std::vector<std::uint32_t> Mask{};
                std::vector<float> Score{};
                std::optional<AnalysisStamp> Stamp{};
            };
            std::optional<AnalysisStamp> oldStamp;
            if (auto* stamp=context.Scene->Raw().try_get<AnalysisStamp>(w->Entity)) oldStamp=*stamp;
            auto before=std::make_shared<State>(State{bool(w->MaskWatch.Revision),bool(w->ScoreWatch.Revision),w->BeforeMask,w->BeforeScore,oldStamp});
            auto after=std::make_shared<State>(State{true,true,w->AfterMask,w->AfterScore,
                AnalysisStamp{w->Config.Positions,w->Config.Mask,w->Inputs,w->MaskWatch}});
            auto revisions=std::make_shared<std::array<Watch,2>>(std::array{w->MaskWatch,w->ScoreWatch});
            const auto mutate=[context,entity=w->Entity,inputs=w->Inputs,c=w->Config,revisions](const State& target)
            {
                if (!CurrentSource(context,entity,inputs) || !CurrentSource(context,entity,*revisions))
                    return EditorCommandHistoryStatus::StaleEntity;
                auto* props=MutableProperties(context.Scene->Raw(),entity,c.Positions.Domain);
                if (target.MaskExists) props->GetOrAdd<std::uint32_t>(c.Mask.Name).Vector()=target.Mask;
                else if (auto p=props->Get<std::uint32_t>(c.Mask.Name)) props->Remove(p);
                if (target.ScoreExists) props->GetOrAdd<float>(c.Score.Name).Vector()=target.Score;
                else if (auto p=props->Get<float>(c.Score.Name)) props->Remove(p);
                const auto a=BuildGeometryAvailability(context.Scene->Raw(),entity);
                *revisions={Observe(a,c.Mask.Domain,c.Mask.Name),Observe(a,c.Score.Domain,c.Score.Name)};
                RestoreStamp(context,entity,target.Stamp,false,&c.Mask);
                ECS::Components::DirtyTags::MarkGpuDirty(context.Scene->Raw(),entity);
                ECS::Components::DirtyTags::MarkVertexAttributesDirty(context.Scene->Raw(),entity);
                if (context.InvalidateWorkspaceSnapshotCache) context.InvalidateWorkspaceSnapshotCache();
                return EditorCommandHistoryStatus::Applied;
            };
            const auto status=context.CommandHistory ? context.CommandHistory->Execute({.Label="Detect outliers",
                .Redo=[mutate,after]{return mutate(*after);},.Undo=[mutate,before]{return mutate(*before);}}).Status : mutate(*after);
            r.Status=GeometryProcessingDetail::ToEditorMethodCommandStatus(status);
            if (!r.Succeeded()) r.Message="Outlier publication rejected by history checks.";
            return r;
        }
        EditorOutlierAnalysisResult RemoveMarked(const EditorGeometryProcessingContext& context,
                                                 const std::shared_ptr<OutlierWork>& w)
        {
            auto& r=w->Result;
            auto* props=MutableProperties(context.Scene->Raw(),w->Entity,D::PointCloudPoint);
            auto before=std::make_shared<Geometry::PropertySet>(*props);
            auto after=std::make_shared<Geometry::PropertySet>(*props);
            const auto mask=std::as_const(*before).Get<std::uint32_t>(w->Config.Mask.Name);
            const auto deleted=std::as_const(*before).Get<bool>("v:deleted");
            std::size_t kept=0;
            for (std::size_t i=0;i<before->Size();++i)
            {
                if ((!deleted || !deleted[i]) && mask[i]==1) {++r.RejectedCount;continue;}
                if (kept!=i) after->Swap(kept,i);
                ++kept;
            }
            r.ActualBackend="published_mask";
            if (!r.RejectedCount) {r.Status=EditorCommandStatus::NoChange;r.Message="No live points are marked.";return r;}
            after->Resize(kept);
            auto revision=std::make_shared<Geometry::PropertyRevision>(props->Revision());
            const auto stamp=context.Scene->Raw().get<AnalysisStamp>(w->Entity);
            const auto mutate=[context,entity=w->Entity,revision](const Geometry::PropertySet& target,std::optional<AnalysisStamp> targetStamp)
            {
                if (!context.Scene || !context.Scene->Raw().valid(entity)) return EditorCommandHistoryStatus::StaleEntity;
                const auto a=BuildGeometryAvailability(context.Scene->Raw(),entity);
                if (a.SourceView.EdgeSource || a.SourceView.HalfedgeSource || a.SourceView.FaceSource)
                    return EditorCommandHistoryStatus::StaleEntity;
                auto* properties=MutableProperties(context.Scene->Raw(),entity,D::PointCloudPoint);
                if (!properties || properties->Revision()!=*revision) return EditorCommandHistoryStatus::StaleEntity;
                *properties=target;
                *revision=properties->Revision();
                RestoreStamp(context,entity,std::move(targetStamp),true);
                if (context.Selection) (void)context.Selection->EditPrimitives(*context.Scene,
                    SelectionController::ToStableEntityId(entity),D::PointCloudPoint,PrimitiveSelectionEdit::Clear);
                ECS::Components::DirtyTags::MarkVertexPositionsDirty(context.Scene->Raw(),entity);
                ECS::Components::DirtyTags::MarkVertexNormalsDirty(context.Scene->Raw(),entity);
                ECS::Components::DirtyTags::MarkVertexAttributesDirty(context.Scene->Raw(),entity);
                ECS::Components::DirtyTags::MarkGpuDirty(context.Scene->Raw(),entity);
                if (context.InvalidateWorkspaceSnapshotCache) context.InvalidateWorkspaceSnapshotCache();
                return EditorCommandHistoryStatus::Applied;
            };
            const auto status=context.CommandHistory ? context.CommandHistory->Execute({.Label="Remove marked points",
                .Redo=[mutate,after]{return mutate(*after,{});},.Undo=[mutate,before,stamp]{return mutate(*before,stamp);}}).Status : mutate(*after,{});
            r.Status=GeometryProcessingDetail::ToEditorMethodCommandStatus(status);
            r.WrittenCount=kept;
            r.Message=r.Succeeded()?"Marked points removed; all surviving properties retained in source order.":"Removal rejected by history checks.";
            return r;
        }
    }
    EditorOutlierAnalysisReadiness PreviewEditorOutlierAnalysisCommand(
        const EditorGeometryProcessingContext& context,const OutlierAnalysisConfig& config)
    {
        EditorOutlierAnalysisReadiness r;
        auto w=Capture(context,config,r.Diagnostic,CapturePurpose::Readiness);
        r.Ready=bool(w);if(w)r.Resolved=w->Config;return r;
    }
    GeometryPropertyCatalogSnapshot GetEditorOutlierAnalysisInputCatalog(
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
            OutlierAnalysisConfig c;c.StableEntityId=id;c.Positions=entry.Ref;c.Mask.Domain=c.Score.Domain=entry.Ref.Domain;
            c.Mask.Name=entry.Ref.Name+".outlier_mask";c.Score.Name=entry.Ref.Name+".outlier_score";
            while(props->Exists(c.Mask.Name))c.Mask.Name+="_";
            while(props->Exists(c.Score.Name))c.Score.Name+="_";
            std::string diagnostic;return !Capture(context,c,diagnostic,CapturePurpose::Catalog);
        });
        return catalog;
    }
    EditorOutlierAnalysisResult ApplyEditorOutlierAnalysisCommand(
        const EditorGeometryProcessingContext &context, const OutlierAnalysisConfig &config)
    {
        std::string diagnostic;
        auto w = Capture(context, config, diagnostic);
        const auto report = [&config, &w](EditorCommandStatus status, std::string message) {
            auto result = w ? w->Result
                            : EditorOutlierAnalysisResult{.Method = config.Method,
                                                           .RequestedBackend = config.Backend,
                                                           .Operation = config.Operation, .Mask = config.Mask, .Score = config.Score};
            result.Status = status;
            result.Message = std::move(message);
            return result;
        };
        if (!w)
            return report(EditorCommandStatus::InvalidProcessingParameters, std::move(diagnostic));
        if (w->Config.Operation == OutlierAnalysisOperation::RemoveMarked) return RemoveMarked(context, w);
        if (w->Config.Backend != OutlierAnalysisBackend::CpuOctree)
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
                              "Outlier index snapshot does not match the selected samples.");
        }
        if (!context.JobCommands.Available())
        {
            Compute(*w);
            return Publish(context, w);
        }
        const EditorJobIdentity identity{.EntityId = config.StableEntityId,
                                         .Scope = ToEditorJobScope(w->Config.Mask.Domain),
                                         .OutputSemantic = GeometryPresentationSlotSemantic::ScalarField,
                                         .OutputName = w->Config.Mask.Name};
        if (context.JobCommands.FindActive)
            if (auto active = context.JobCommands.FindActive(identity);
                active && IsActiveEditorJobState(active->State))
                return report(EditorCommandStatus::Pending,
                              "An outlier job for this output is already active.");
        auto sink = context.MethodResultSinks.OutlierAnalysis;
        auto delivered = std::make_shared<bool>(false);
        auto pending = w->Result;
        pending.Status = EditorCommandStatus::Pending;
        pending.Message = "Outlier estimation queued.";
        JobDesc desc{
            .DebugName = "Outlier estimation",
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
                            pending.Message = "Outlier job was cancelled or its source became stale; previous output retained.";
                        }
                        sink(std::move(pending));
                    }
                }};
        if (w->Config.Backend == OutlierAnalysisBackend::VulkanLBVH)
        {
            JobDesc gpu{
                .DebugName = "Outlier neighborhoods (Vulkan)", .Scope = context.World,
                .Kind = RuntimeTaskKinds::GeometryProcess,
                .Work = [](const JobCancellation&) { return JobResultEnvelope::Make(true); },
                .IsReadyToApply = [context, w] { return AdvanceGpu(context, *w); },
                .PublishCompletion = [w](KernelEventBus&, const JobResultEnvelope&) { return w->GpuFinished; },
                .FinalizeUnpublishedOnMainThread = [w] { w->Abandoned = true; }};
            const auto prerequisite = context.JobCommands.Submit(std::move(gpu), identity);
            if (!prerequisite.IsValid())
                return report(EditorCommandStatus::GeometryProcessingFailed, "GPU outlier job submission was rejected.");
            desc.DependsOn.push_back({prerequisite, "Complete Vulkan outlier neighborhoods before CPU classification"});
        }
        const auto token = context.JobCommands.Submit(std::move(desc), identity);
        if (!token.IsValid())
        {
            w->Abandoned = true;
            pending.Status = EditorCommandStatus::GeometryProcessingFailed;
            pending.Message = "Outlier job submission was rejected.";
        }
        return pending;
    }
    EditorOutlierAnalysisResult ApplyEditorConfiguredOutlierAnalysis(
        const EditorGeometryProcessingContext &context)
    {
        const auto config = GetEditorOutlierAnalysisConfig(context);
        if (!config)
            return {.Status = EditorCommandStatus::InvalidProcessingParameters,
                    .Message = "Outlier estimation config is unavailable."};
        return ApplyEditorOutlierAnalysisCommand(context, *config);
    }
} // namespace Extrinsic::Runtime

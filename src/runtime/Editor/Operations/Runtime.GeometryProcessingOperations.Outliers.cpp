module;
#include <functional>
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
#include <string_view>
#include <vector>
#include <variant>
#include <utility>
#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>
module Extrinsic.Runtime.PointAnalysisOperations;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.SelectionController;
import Geometry.Properties;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Core.Error;
import Geometry.PointCloud.Utils;
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
        namespace GS = ECS::Components::GeometrySources;
        namespace PC = Geometry::PointCloud;
        using D = GeometryElementDomain;
        using GeometryProcessingDetail::PointPropertyWatch;
        using GeometryProcessingDetail::ObserveGeometryProperty;
        using GeometryProcessingDetail::MutableGeometryProperties;
        using GeometryProcessingDetail::GeometryPropertiesCurrent;
        // Transient provenance for the most recent detection on this entity. Persisted masks
        // remain visible after reload, but removal requires detection against the live source.
        struct AnalysisStamp
        {
            GeometryPropertyRef Positions{}, Mask{};
            std::vector<PointPropertyWatch> Inputs{};
            PointPropertyWatch MaskWatch{};
        };
        struct OutlierWork : GeometryProcessingDetail::PointInputCapture
        {
            OutlierAnalysisConfig Config{};
            entt::entity Entity{};
            PointPropertyWatch MaskWatch{}, ScoreWatch{};
            GeometryScalarPropertySnapshot BeforeMask{}, BeforeScore{};
            std::vector<std::uint32_t> AfterMask{}, Counts{}, NeighborIds{};
            std::vector<float> AfterScore{};
            std::shared_ptr<const SpatialIndexSnapshot> Index{};
            SpatialIndexHandle GpuIndex{};
            GeometryProcessingDetail::GpuRowPages Pages{};
            bool Abandoned{};
            EditorOutlierAnalysisResult Result{};
        };
        bool CurrentInput(const EditorProcessingContext& context, const OutlierWork& w)
        {
            const std::array outputs{w.MaskWatch, w.ScoreWatch};
            return GeometryPropertiesCurrent(context, w.Entity, w.Inputs) && GeometryPropertiesCurrent(context, w.Entity, outputs);
        }
        bool CurrentStamp(const EditorProcessingContext& context, entt::entity entity,
                          const OutlierAnalysisConfig& c)
        {
            const auto* stamp = context.Scene->Raw().try_get<AnalysisStamp>(entity);
            return stamp && stamp->Positions == c.Positions && stamp->Mask == c.Mask &&
                GeometryPropertiesCurrent(context, entity, stamp->Inputs) &&
                GeometryPropertiesCurrent(context, entity, std::span(&stamp->MaskWatch, 1));
        }
        enum class CapturePurpose { Execute, Readiness };
        std::shared_ptr<OutlierWork> Capture(const EditorProcessingContext& context,
            OutlierAnalysisConfig c, std::string& diagnostic, CapturePurpose purpose = CapturePurpose::Execute)
        {
            auto fail = [&](std::string why) -> std::shared_ptr<OutlierWork> { diagnostic = std::move(why); return {}; };
            const auto validation = ValidateOutlierAnalysisConfigSection(
                SerializeOutlierAnalysisConfig(c), {}, kOutlierAnalysisConfigSectionName);
            if (!validation.Usable()) return fail(validation.Diagnostics.front().Message);
            if ((context.AttachmentActive && !context.AttachmentActive()) || !context.Scene)
                return fail("Scene is unavailable.");
            const auto entity = EditorFeatureDetail::ResolveStableEntity(context.Scene->Raw(), c.StableEntityId);
            if (!entity) return fail("Outlier target entity is stale or missing.");
            const auto a = BuildGeometryAvailability(context.Scene->Raw(), *entity);
            auto w = std::make_shared<OutlierWork>();
            const bool captured = purpose == CapturePurpose::Execute
                ? GeometryProcessingDetail::CapturePointInput(a, c.Positions, true, *w, diagnostic)
                : GeometryProcessingDetail::PreparePointInput(context, *entity, a, c.Positions, *w, diagnostic);
            if (!captured) return {};
            if (c.Mask.Domain == D::Unknown) c.Mask.Domain = c.Positions.Domain;
            if (c.Score.Domain == D::Unknown) c.Score.Domain = c.Positions.Domain;
            const std::array outputs{c.Mask, c.Score};
            if (!GeometryProcessingDetail::ValidatePointOutputs(a, c.Positions, outputs,
                                                                 "Outlier", diagnostic)) return {};
            const auto* props = ResolveGeometryPropertySet(a, c.Positions.Domain);
            w->Config = c; w->Entity = *entity;
            w->Result.RequestedBackend = c.Backend;
            w->Result.Method = c.Method; w->Result.Operation = c.Operation;
            w->Result.Mask = c.Mask; w->Result.Score = c.Score;
            w->Result.SlotCount = w->SlotCount; w->Result.LiveCount = w->LiveCount;
            w->MaskWatch = ObserveGeometryProperty(a, c.Mask.Domain, c.Mask.Name);
            w->ScoreWatch = ObserveGeometryProperty(a, c.Score.Domain, c.Score.Name);
            if (!w->Result.LiveCount) return fail("Outlier analysis requires live input samples.");
            if (c.Operation == OutlierAnalysisOperation::RemoveMarked)
            {
                if (c.Positions.Domain != D::PointCloudPoint || a.SourceView.EdgeSource || a.SourceView.HalfedgeSource || a.SourceView.FaceSource)
                    return fail("Remove Marked Points requires a topology-free point cloud. Mesh/graph detection preserves topology.");
                if (!CurrentStamp(context, *entity, c)) return fail("Detect outliers again: the source or published mask changed, or this mask has no current analysis.");
                return w;
            }
            if (c.Method == OutlierAnalysisMethod::LocalDistanceRatio && w->Result.LiveCount < 2)
                return fail("Local distance ratio requires at least two live samples.");
            if (c.Method == OutlierAnalysisMethod::Statistical && c.KNeighbors >= w->Result.LiveCount)
                return fail("Statistical outlier analysis requires more live samples than k.");
            if (c.Backend != OutlierAnalysisBackend::CpuOctree)
            {
                if (!context.SpatialIndices || !w->ValidLbvh || w->Result.LiveCount > (1u << 24) ||
                    (c.Method == OutlierAnalysisMethod::Radius && c.Radius > Geometry::PointLBVH::CoordinateLimit))
                    return fail("LBVH requires the spatial cache, at most 2^24 samples and coordinates/radius within 1e18.");
                if (c.Backend == OutlierAnalysisBackend::VulkanLBVH)
                {
                    if (!context.JobCommands.Available() || !context.SpatialIndices->GpuQueriesAvailable())
                        return fail("Vulkan outlier neighborhoods require the framed spatial cache and job service.");
                    if (w->Result.LiveCount > (1u << 20) ||
                        (c.Method == OutlierAnalysisMethod::Statistical && c.KNeighbors > 64) ||
                        (c.Method == OutlierAnalysisMethod::LocalDistanceRatio && c.KNeighbors > 63))
                        return fail("Vulkan outlier queries support at most 2^20 live samples and statistical k=1..64 or distance-ratio k<=63 (64 candidates).");
                }
            }
            if (purpose == CapturePurpose::Execute)
            {
                w->BeforeMask = CaptureGeometryScalarProperty(*props, c.Mask);
                w->BeforeScore = CaptureGeometryScalarProperty(*props, c.Score);
                w->AfterMask.resize(props->Size());
                w->AfterScore.resize(props->Size());
            }
            return w;
        }
        std::uint32_t QueryWidth(const OutlierAnalysisConfig& c, std::size_t count)
        {
            if (c.Method == OutlierAnalysisMethod::LocalDistanceRatio)
                return std::uint32_t(std::min(count - 1, std::max<std::size_t>(c.KNeighbors, 2)) + 1);
            return std::uint32_t(std::min<std::size_t>(c.KNeighbors, count - 1));
        }
        void Compute(OutlierWork& w)
        {
            const auto started = std::chrono::steady_clock::now();
            const auto& c = w.Config;
            auto& r = w.Result;
            r.Status = EditorCommandStatus::GeometryProcessingFailed;
            r.ActualBackend = ToString(c.Backend);
            PC::OutlierAnalysisResult analysis;
            if (c.Method == OutlierAnalysisMethod::LocalDistanceRatio)
            {
                const PC::OutlierEstimationParams params{.KNeighbors=c.KNeighbors,.ScoreThreshold=c.ScoreThreshold};
                std::optional<PC::OutlierEstimationResult> ratio;
                if (c.Backend == OutlierAnalysisBackend::CpuOctree)
                    ratio = PC::EstimateOutlierProbability(w.Points, params);
                else
                {
                    const auto width = QueryWidth(c, w.Points.size());
                    if (c.Backend == OutlierAnalysisBackend::CpuLBVH)
                    {
                        if (!GeometryProcessingDetail::AppendPointKnnRows(
                                *w.Index, w.Points, width, w.NeighborIds, r.Message))
                            return;
                    }
                    else
                        for (auto& id : w.NeighborIds)
                        {
                            const auto found = std::lower_bound(w.Slots.begin(), w.Slots.end(), id);
                            if (found == w.Slots.end() || *found != id) { r.Message="Invalid Vulkan neighbor source row."; return; }
                            id = std::uint32_t(found - w.Slots.begin());
                        }
                    ratio = PC::EstimateOutlierProbabilityFromNeighbors(w.Points, w.NeighborIds, params);
                }
                if (!ratio) { r.Message="Local distance ratio failed: invalid neighborhoods or unrepresentable float scores."; return; }
                analysis.Scores = std::move(ratio->Scores);
                analysis.Mask.reserve(analysis.Scores.size());
                for (const float score : analysis.Scores) analysis.Mask.push_back(score > c.ScoreThreshold);
                analysis.RejectedCount = ratio->OutlierCount;
            }
            else if (c.Backend == OutlierAnalysisBackend::CpuOctree)
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
        // Radius rows keep only counts; kNN rows keep raw source IDs, decoded by Compute.
        bool AdvanceGpu(const EditorProcessingContext& context, OutlierWork& w)
        {
            if (w.Abandoned || !CurrentInput(context,w))
            {w.Result.Status=EditorCommandStatus::StaleEntity; w.Result.Message="Outlier inputs changed or the job was cancelled."; w.Pages.Batch.reset(); return true;}
            const auto& c=w.Config;
            const auto width=QueryWidth(c,w.Points.size());
            const auto state=GeometryProcessingDetail::AdvanceGpuRowPages(w.Pages,w.Points.size(),c.GpuQueryBatchSize,w.Result.Message,
                [&](const SpatialNearestBatch& batch, std::string& why)
                {
                    for (std::size_t row=0;row<batch.Counts.size();++row)
                    {
                        if (c.Method==OutlierAnalysisMethod::Radius) { w.Counts.push_back(batch.Counts[row]); continue; }
                        if (batch.Counts[row]!=width) { why="Incomplete Vulkan kNN neighborhood."; return false; }
                        for (std::uint32_t j=0;j<batch.Counts[row];++j)
                            w.NeighborIds.push_back(batch.Neighbors[row*batch.Capacity+j].Index);
                    }
                    return true;
                },
                [&](std::size_t first, std::size_t count, std::shared_ptr<SpatialNearestBatch> reuse)
                {
                    const auto queries=std::span(w.Points).subspan(first,count);
                    const auto exclusions=c.Method==OutlierAnalysisMethod::LocalDistanceRatio ?
                        std::span<const std::uint32_t>{} : std::span<const std::uint32_t>(w.Slots).subspan(first,count);
                    return c.Method==OutlierAnalysisMethod::Radius
                        ? context.SpatialIndices->QueueGpuRadius(w.GpuIndex,queries,c.Radius,1,exclusions,std::move(reuse))
                        : context.SpatialIndices->QueueGpuKNearest(w.GpuIndex,queries,width,exclusions,std::move(reuse));
                });
            w.Result.GpuQueryBatches=w.Pages.QueryBatches;
            if (state==GeometryProcessingDetail::RowsState::Ready) w.Result.GpuNeighborhoodMilliseconds=w.Pages.Milliseconds;
            if (state==GeometryProcessingDetail::RowsState::Failed) w.Result.Status=EditorCommandStatus::GeometryProcessingFailed;
            return state!=GeometryProcessingDetail::RowsState::Pending;
        }
        void RestoreStamp(const EditorProcessingContext& context, entt::entity entity,
                          std::optional<AnalysisStamp> stamp, bool rebaseInputs = false,
                          const GeometryPropertyRef* restoredMask = nullptr)
        {
            auto& raw=context.Scene->Raw();
            if (!stamp) { raw.remove<AnalysisStamp>(entity); return; }
            const auto a=BuildGeometryAvailability(raw,entity);
            if (rebaseInputs || (restoredMask && stamp->Mask == *restoredMask))
                stamp->MaskWatch=ObserveGeometryProperty(a,stamp->Mask.Domain,stamp->Mask.Name);
            if (rebaseInputs)
                for (auto& watch : stamp->Inputs) watch=ObserveGeometryProperty(a,watch.Domain,watch.Name);
            raw.emplace_or_replace<AnalysisStamp>(entity,std::move(*stamp));
        }
        EditorOutlierAnalysisResult Publish(const EditorProcessingContext& context,
                                            const std::shared_ptr<OutlierWork>& w)
        {
            auto& r=w->Result;
            if (!CurrentInput(context,*w))
            {r.Status=EditorCommandStatus::StaleEntity; r.Message="Outlier input or output changed before publication."; return r;}
            if (r.Status!=EditorCommandStatus::Applied) return r;
            struct State
            {
                GeometryScalarPropertySnapshot Mask{}, Score{};
                std::optional<AnalysisStamp> Stamp{};
            };
            std::optional<AnalysisStamp> oldStamp;
            if (auto* stamp=context.Scene->Raw().try_get<AnalysisStamp>(w->Entity)) oldStamp=*stamp;
            auto before=std::make_shared<State>(State{w->BeforeMask,w->BeforeScore,oldStamp});
            auto after=std::make_shared<State>(*before);
            after->Stamp = AnalysisStamp{w->Config.Positions,w->Config.Mask,w->Inputs,w->MaskWatch};
            if (!PrepareGeometryScalarProperty(after->Mask, w->Config.Mask.ValueKind, w->SlotCount, w->Slots, w->AfterMask) ||
                !PrepareGeometryScalarProperty(after->Score, w->Config.Score.ValueKind, w->SlotCount, w->Slots, w->AfterScore))
            { r.Status=EditorCommandStatus::InvalidProcessingParameters; r.Message="Output values are not exactly representable in the selected scalar storage."; return r; }
            auto revisions=std::make_shared<std::array<PointPropertyWatch,2>>(std::array{w->MaskWatch,w->ScoreWatch});
            const auto mutate=[context,entity=w->Entity,inputs=w->Inputs,c=w->Config,revisions](const State& target)
            {
                if (!GeometryPropertiesCurrent(context,entity,inputs) || !GeometryPropertiesCurrent(context,entity,*revisions))
                    return EditorCommandHistoryStatus::StaleEntity;
                auto* props=MutableGeometryProperties(context.Scene->Raw(),entity,c.Positions.Domain);
                if (!CanApplyGeometryScalarProperty(*props, c.Mask, target.Mask) ||
                    !CanApplyGeometryScalarProperty(*props, c.Score, target.Score))
                    return EditorCommandHistoryStatus::InvalidCommand;
                (void)ApplyGeometryScalarProperty(*props, c.Mask, target.Mask);
                (void)ApplyGeometryScalarProperty(*props, c.Score, target.Score);
                const auto a=BuildGeometryAvailability(context.Scene->Raw(),entity);
                *revisions={ObserveGeometryProperty(a,c.Mask.Domain,c.Mask.Name),ObserveGeometryProperty(a,c.Score.Domain,c.Score.Name)};
                RestoreStamp(context,entity,target.Stamp,false,&c.Mask);
                if (context.InvalidateWorkspaceSnapshotCache) context.InvalidateWorkspaceSnapshotCache();
                return EditorCommandHistoryStatus::Applied;
            };
            const auto status=context.CommandHistory ? context.CommandHistory->Execute({.Label="Detect outliers",
                .Redo=[mutate,after]{return mutate(*after);},.Undo=[mutate,before]{return mutate(*before);}}).Status : mutate(*after);
            r.Status = status == EditorCommandHistoryStatus::InvalidCommand
                ? EditorCommandStatus::InvalidProcessingParameters
                : EditorFeatureDetail::ToEditorCommandStatus(status);
            if (!r.Succeeded()) r.Message="Outlier publication rejected by history checks.";
            return r;
        }
        EditorOutlierAnalysisResult RemoveMarked(const EditorProcessingContext& context,
                                                 const std::shared_ptr<OutlierWork>& w)
        {
            auto& r=w->Result;
            auto* props=MutableGeometryProperties(context.Scene->Raw(),w->Entity,D::PointCloudPoint);
            auto before=std::make_shared<Geometry::PropertySet>(*props);
            auto after=std::make_shared<Geometry::PropertySet>(*props);
            const auto mask=CaptureGeometryScalarProperty(*before, w->Config.Mask);
            const auto deleted=std::as_const(*before).Get<bool>("v:deleted");
            std::size_t kept=0;
            for (std::size_t i=0;i<before->Size();++i)
            {
                if ((!deleted || !deleted[i]) && std::visit([i](const auto& values) { return values[i] == 1; }, mask.Values)) {++r.RejectedCount;continue;}
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
                if ((context.AttachmentActive && !context.AttachmentActive()) ||
                    !context.Scene || !context.Scene->Raw().valid(entity)) return EditorCommandHistoryStatus::StaleEntity;
                const auto a=BuildGeometryAvailability(context.Scene->Raw(),entity);
                if (a.SourceView.EdgeSource || a.SourceView.HalfedgeSource || a.SourceView.FaceSource)
                    return EditorCommandHistoryStatus::StaleEntity;
                auto* properties=MutableGeometryProperties(context.Scene->Raw(),entity,D::PointCloudPoint);
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
            r.Status = status == EditorCommandHistoryStatus::InvalidCommand
                ? EditorCommandStatus::InvalidProcessingParameters
                : EditorFeatureDetail::ToEditorCommandStatus(status);
            r.WrittenCount=kept;
            r.Message=r.Succeeded()?"Marked points removed; all surviving properties retained in source order.":"Removal rejected by history checks.";
            return r;
        }
    }
    ActionReadiness PreviewEditorOutlierAnalysisCommand(
        const EditorProcessingCommands& commands,const OutlierAnalysisConfig& config)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        std::string diagnostic;
        const auto work = Capture(context, config, diagnostic, CapturePurpose::Readiness);
        return {bool(work), std::move(diagnostic)};
    }
    EditorOutlierAnalysisResult ApplyEditorOutlierAnalysisCommand(
        const EditorProcessingCommands& commands, const OutlierAnalysisConfig &config, std::function<void(EditorOutlierAnalysisResult)> onComplete)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
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
            const auto indexState = GeometryProcessingDetail::AcquirePointIndex(
                *context.SpatialIndices, context.World, w->Entity, w->Config.Positions,
                w->Slots, w->Points, w->GpuIndex, w->Index, w->Result.IndexReused, diagnostic);
            if (indexState == GeometryProcessingDetail::PointIndexState::Unavailable)
                return report(EditorCommandStatus::InvalidProcessingParameters, diagnostic);
            if (indexState == GeometryProcessingDetail::PointIndexState::Mismatched)
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
        if (auto active = GeometryProcessingDetail::MeshSupport::FindActiveEditorJob(context, identity);
            active && IsActiveEditorJobState(active->State))
            return report(EditorCommandStatus::Pending,
                          "An outlier job for this output is already active.");
        auto sink = GuardEditorProcessingResult(context, std::move(onComplete));
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
                .PublishCompletion = [w](KernelEventBus&, const JobResultEnvelope&) { return w->Pages.Finished; },
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
        const EditorProcessingCommands& commands, std::function<void(EditorOutlierAnalysisResult)> onComplete)
    {
        const auto config = GetEditorOutlierAnalysisConfig(commands);
        if (!config)
            return {.Status = EditorCommandStatus::InvalidProcessingParameters,
                    .Message = "Outlier estimation config is unavailable."};
        return ApplyEditorOutlierAnalysisCommand(commands, *config, std::move(onComplete));
    }
} // namespace Extrinsic::Runtime

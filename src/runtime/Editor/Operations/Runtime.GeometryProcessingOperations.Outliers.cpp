module;
#include <functional>
#include <cstring>
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
import Extrinsic.Runtime.GpuPropertyBinding;
import Extrinsic.Graphics.OutlierAnalysis;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.CommandContext;
#include "Editor/internal/Runtime.EditorProcessingAccess.hpp"
#include "Editor/internal/Runtime.EditorGeometryHelpers.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.PointFields.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.JobFailure.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.RadiusRows.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.GpuFront.hpp"

#pragma clang fp contract(off)
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
                    if (!context.Device || !context.Device->SupportsShaderFloat64())
                        return fail("Vulkan outlier classification requires shader float64.");
                    if (c.Score.ValueKind != Geometry::PropertyValueKind::Float || c.Mask.ValueKind != Geometry::PropertyValueKind::UInt32)
                        return fail("Vulkan outliers require a float score and uint32 mask.");
                    const auto width = Graphics::OutlierNeighborWidth(std::uint32_t(c.Method), c.KNeighbors,
                                                                      std::uint32_t(w->LiveCount));
                    if (w->SlotCount > (1u << 20) || std::uint64_t(w->LiveCount) * width > (1u << 24))
                        return fail("Vulkan outlier storage exceeds the bounded device workspace.");
                    if (c.Method==OutlierAnalysisMethod::Radius && std::fpclassify(c.Radius)==FP_SUBNORMAL)
                        return fail("Vulkan radius outliers refuse subnormal radius parameters.");
                    if (std::fpclassify(c.ScoreThreshold)==FP_SUBNORMAL || std::fpclassify(c.StdDevMultiplier)==FP_SUBNORMAL)
                        return fail("Vulkan outliers refuse subnormal float threshold parameters.");
                    if (w->HasSubnormalCoordinates)
                        return fail("Vulkan outliers do not admit subnormal positions.");
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
                    if (!GeometryProcessingDetail::AppendPointKnnRows(
                            *w.Index, w.Points, width, w.NeighborIds, r.Message))
                        return;
                    ratio = PC::EstimateOutlierProbabilityFromNeighbors(w.Points, w.NeighborIds, params);
                }
                if (!ratio) { r.Message="Local distance ratio failed: invalid neighborhoods or unrepresentable float scores."; return; }
                analysis.Scores = std::move(ratio->Scores);
                analysis.Mask = std::move(ratio->Mask);
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
                        w.Counts.push_back(w.Index->Index.Radius(w.Points[i], c.Radius, 1, i, true).TotalCount);
                analysis = PC::ClassifyRadiusOutliers(w.Counts, c.MinimumNeighbors);
            }
            else
            {
                std::vector<double> means(w.Points.size());
                const auto k = std::min<std::size_t>(c.KNeighbors, w.Points.size()-1);
                for (std::uint32_t i=0; i<w.Points.size(); ++i)
                {
                    double sum=0;
                    const auto neighbors = w.Index->Index.KNearest(w.Points[i], std::uint32_t(k+1), Geometry::PointLBVH::InvalidIndex, true);
                    if (neighbors.size()!=k+1) { r.Message="Incomplete CPU kNN neighborhood."; return; }
                    std::size_t used=0;
                    for (const auto& n : neighbors) {
                        if(n.Index==i)continue;
                        ++used;
                        const glm::dvec3 d=glm::dvec3(w.Points[i])-glm::dvec3(w.Points[n.Index]);
                        sum += std::sqrt((d.x*d.x+d.y*d.y)+d.z*d.z);
                    }
                    means[i]=used?sum/double(used):0;
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
            r.Message=r.Succeeded()?"Marked points removed by the atomic CPU compaction stage; no GPU readback; all surviving properties retained in source order.":"Removal rejected by history checks.";
            return r;
        }
    }
    struct EditorOutlierTransaction
    {
        EditorProcessingContext Context{};
        std::shared_ptr<OutlierWork> Work{};
        EditorJobIdentity Identity{};
        Graphics::GpuPropertyResidency* Residency{};
        std::array<GeometryPropertyRef,3> Refs{};
        std::array<Graphics::GpuPropertyKey,3> Keys{};
        std::array<std::uint64_t,3> Generations{};
        std::optional<Graphics::GpuPropertyView> Input{};
        std::array<std::optional<Graphics::GpuPropertyView>,3> Back{};
        std::array<std::optional<Graphics::GpuPropertyView>,2> Base{};
        std::array<std::shared_ptr<GeometryProcessingDetail::GpuFrontReadback>,2> Readback{};
        // Leased from the spatial cache; the recorder closure keeps it while its work may run.
        std::shared_ptr<Graphics::OutlierWorkspace> Workspace{};
        std::shared_ptr<SpatialGpuResult> Gpu{};
        EditorGpuTransactionPhase Phase{EditorGpuTransactionPhase::Running};
        std::uint32_t Deferrals{};
        bool AutoAccept{}, Abandoned{}, Delivered{}, TestFront{};
        std::function<void(EditorOutlierAnalysisResult)> Sink{};
    };
    namespace OutlierTransactionDetail
    {
        namespace GP=GeometryProcessingDetail;
        using Run=EditorOutlierTransactionHandle;
        bool Current(const Run& w)
        {
            for(std::size_t i=0;i<w->Keys.size();++i)
                if(w->Generations[i] && w->Residency->RingGeneration(w->Keys[i])!=w->Generations[i])return false;
            return !w->Abandoned && GP::EditorProcessingContextWorldCurrent(w->Context) && CurrentInput(w->Context,*w->Work);
        }
        void Release(const Run& w)
        {
            w->Input.reset();
            // Only completed (or never queued) work returns the workspace early.
            if(!w->Gpu||w->Gpu->State==SpatialQueryState::Ready)w->Workspace.reset();
            for(auto& v:w->Back)v.reset();
            for(auto& v:w->Base)v.reset();
            for(auto& r:w->Readback)if(r){r->Abandoned=true;r->Lease.reset();}
            if(w->Residency)for(std::size_t i=0;i<3;++i)(void)w->Residency->Discard(w->Keys[i],w->Generations[i]);
        }
        void Deliver(const Run& w)
        {
            if(std::exchange(w->Delivered,true))return;
            if(w->Sink)w->Sink(w->Work->Result);
        }
        void Finish(const Run& w,EditorGpuTransactionPhase phase,EditorCommandStatus status,std::string why)
        {
            Release(w);w->Phase=phase;w->Work->Result.Status=status;w->Work->Result.Message=std::move(why);Deliver(w);
        }
        void Fail(const Run& w,std::string why)
        { Finish(w,EditorGpuTransactionPhase::Failed,EditorCommandStatus::GeometryProcessingFailed,std::move(why)); }
        Run Make(const EditorProcessingContext& context,const std::shared_ptr<OutlierWork>& work,Graphics::GpuPropertyResidency* residency)
        {
            auto w=std::make_shared<EditorOutlierTransaction>();w->Context=context;w->Work=work;w->Residency=residency;
            w->Refs={work->Config.Score,work->Config.Mask,GpuPropertyPresentationRef(work->Config.Mask)};
            for(std::size_t i=0;i<3;++i)w->Keys[i]=MakeGpuPropertyKey(context.World,work->Entity,w->Refs[i]);
            w->Identity={.EntityId=work->Config.StableEntityId,.Scope=ToEditorJobScope(work->Config.Mask.Domain),
                .OutputSemantic=GeometryPresentationSlotSemantic::ScalarField,.OutputName=work->Config.Mask.Name};
            return w;
        }
        bool Acquire(const Run& w)
        {
            auto& r=*w->Residency;const auto& ctx=w->Context;auto& work=*w->Work;
            const auto resolve=[&](const GeometryPropertyRef& ref){
                const auto before=r.Stats().UploadBytes;
                auto v=ResolveGpuPropertyInput(r,*ctx.Scene,ctx.World,work.Entity,ref);
                const auto bytes=r.Stats().UploadBytes-before;work.Result.GpuInputUploadBytes+=bytes;
                if(v && !bytes)++work.Result.GpuInputCacheHits;
                return v;
            };
            if(!w->Input)w->Input=resolve(work.Config.Positions);
            if(!w->Input)return false;
            const std::array watches{work.ScoreWatch,work.MaskWatch};
            for(std::size_t i=0;i<2;++i)if(watches[i].Revision && !w->Base[i]){
                w->Base[i]=resolve(w->Refs[i]);if(!w->Base[i])return false;}
            for(std::size_t i=0;i<3;++i)if(!w->Back[i]){
                // Another output identity may share this score or mask name. Never acquire
                // a back in its ring; wait for it to finish or invalidate our captured output.
                if (!w->Generations[i] && r.HasRing(w->Keys[i])) return false;
                w->Back[i]=AcquireGpuPropertyOutput(r,ctx.World,work.Entity,w->Refs[i],std::uint32_t(work.SlotCount),3);
                if(!w->Back[i])return false;
                w->Generations[i]=r.RingGeneration(w->Keys[i]);}
            return true;
        }
        bool Poll(const Run& w)
        {
            if(!Current(w))return true;
            if(!w->Gpu){
                if(!Acquire(w)){
                    if(++w->Deferrals<600)return false;
                    Fail(w,"The residency refused an input or output slot.");return true;}
                auto& ctx=w->Context;
                w->Workspace=ctx.SpatialIndices->LeaseGpuWorkspace<Graphics::OutlierWorkspace>();
                if(!w->Workspace){Fail(w,"Outlier device workspace unavailable.");return true;}
                w->Work->Result.GpuQueryBatches=1;
                w->Gpu=ctx.SpatialIndices->QueueGpuCompute(w->Work->GpuIndex,sizeof(Graphics::OutlierGpuStats),
                    [w](RHI::ICommandContext& cmd,const SpatialGpuIndexView& index)->RHI::BufferHandle{
                        if(!Current(w)||!w->Input||!w->Back[0]||!w->Back[1]||!w->Back[2]||!w->Workspace)return {};
                        const auto frame=w->Context.Device->GetGlobalFrameNumber();
                        w->Residency->NoteUse(w->Input->Buffer,frame);
                        for(const auto& v:w->Back)w->Residency->NoteUse(v->Buffer,frame);
                        for(const auto& v:w->Base)if(v)w->Residency->NoteUse(v->Buffer,frame);
                        const auto& c=w->Work->Config;
                        return w->Workspace->Record(cmd,{.Method=std::uint32_t(c.Method),.K=c.KNeighbors,
                            .MinimumNeighbors=c.MinimumNeighbors,.Radius=c.Radius,.Multiplier=c.StdDevMultiplier,.ScoreThreshold=c.ScoreThreshold},
                            {.Positions=*w->Input,.Score=*w->Back[0],.Mask=*w->Back[1],.Presentation=*w->Back[2],
                             .ScoreBase=w->Base[0].value_or(Graphics::GpuPropertyView{}),.MaskBase=w->Base[1].value_or(Graphics::GpuPropertyView{}),
                             .Nodes=index.NodesBDA,.LiveSlots=index.OriginalSlotsBDA,.LiveCount=index.Count});
                    },SpatialGpuLatency::Immediate);
                if(!w->Gpu){Fail(w,"Outlier compute submission refused.");return true;}
            }
            return w->Gpu->State==SpatialQueryState::Ready||w->Gpu->State==SpatialQueryState::Failed;
        }
        void CompleteAccept(const Run& w)
        {
            auto& work=*w->Work;std::array<std::uint64_t,2> publications{};
            if(!w->TestFront){
                for(std::size_t i=0;i<2;++i){
                    const auto& r=w->Readback[i];
                    if(!r||r->Failed||r->Bytes.size()!=work.SlotCount*4){Fail(w,"Outlier front readback failed.");return;}
                    publications[i]=r->Lease?r->Lease->Publication:0;
                }
                std::memcpy(work.AfterScore.data(),w->Readback[0]->Bytes.data(),work.SlotCount*4);
                std::memcpy(work.AfterMask.data(),w->Readback[1]->Bytes.data(),work.SlotCount*4);
            }
            for(auto& r:w->Readback)if(r)r->Lease.reset();
            for(const auto row:work.Slots)if(!std::isfinite(work.AfterScore[row])||work.AfterMask[row]>1){Fail(w,"Invalid outlier device result.");return;}
            work.Result.Status=EditorCommandStatus::Applied;work.Result.WrittenCount=work.Slots.size();
            work.Result.ActualBackend="vulkan_lbvh";
            work.Result.Message="Device outlier score and mask accepted; input upload: "+std::to_string(work.Result.GpuInputUploadBytes)+
                " bytes; residency hits: "+std::to_string(work.Result.GpuInputCacheHits)+"; CPU-stage readback: "+std::to_string(work.Result.CpuStageReadbackBytes)+" bytes.";
            const auto published=Publish(w->Context,w->Work);
            if(!published.Succeeded()){Finish(w,EditorGpuTransactionPhase::Failed,published.Status,published.Message);return;}
            if(w->Residency){
                const auto a=BuildGeometryAvailability(w->Context.Scene->Raw(),work.Entity);
                for(std::size_t i=0;i<2;++i){
                    const auto watch=GP::ObserveGeometryProperty(a,w->Refs[i].Domain,w->Refs[i].Name);
                    if(!watch.Revision||!w->Residency->BindRevision(w->Keys[i],*watch.Revision,publications[i]))
                        (void)w->Residency->Discard(w->Keys[i],w->Generations[i]);}
                (void)w->Residency->Discard(w->Keys[2],w->Generations[2]);
            }
            w->Phase=EditorGpuTransactionPhase::Applied;Deliver(w);
        }
        EditorOutlierAnalysisResult Accept(const Run& w,std::function<void(EditorOutlierAnalysisResult)> sink)
        {
            auto refused=w->Work->Result;
            if(w->Phase!=EditorGpuTransactionPhase::ReadyToAccept){refused.Status=EditorCommandStatus::InvalidProcessingParameters;refused.Message="No outlier result waits for Accept.";return refused;}
            if(!Current(w)){refused.Status=EditorCommandStatus::StaleEntity;refused.Message="Outlier input or output changed; discard and run again.";return refused;}
            if(sink)w->Sink=GuardEditorProcessingResult(w->Context,std::move(sink));
            if(!w->TestFront)for(std::size_t i=0;i<2;++i){
                w->Readback[i]=std::make_shared<GP::GpuFrontReadback>();
                if(!GP::BeginGpuFrontReadback(w->Context,*w->Residency,w->Keys[i],w->Readback[i])){Fail(w,"Outlier front is no longer resident.");return w->Work->Result;}}
            w->Phase=EditorGpuTransactionPhase::Accepting;
            JobDesc job{.DebugName="Accept outlier fields",.Scope=w->Context.World,.Kind=RuntimeTaskKinds::GeometryProcess,
                .Work=[](const JobCancellation&){return JobResultEnvelope::Make(true);},
                .IsReadyToApply=[w]{if(!Current(w))return true;bool ready=true;for(auto& r:w->Readback)if(r)ready=GP::PollGpuFrontReadback(*r)&&ready;return ready;},
                .ValidateBeforeApply=[w]{return Current(w)?JobApplyValidation::Current:JobApplyValidation::StaleGeneration;},
                .PublishCompletion=[w](KernelEventBus&,const JobResultEnvelope&){CompleteAccept(w);return w->Phase==EditorGpuTransactionPhase::Applied;},
                .FinalizeUnpublishedOnMainThread=[w]{if(!w->Delivered)Finish(w,EditorGpuTransactionPhase::Discarded,EditorCommandStatus::StaleEntity,"Outlier Accept cancelled or stale.");}};
            if(!w->Context.JobCommands.Submit(std::move(job),w->Identity).IsValid())Fail(w,"Outlier Accept submission refused.");
            return w->Work->Result;
        }
        Run Start(const EditorProcessingContext& ctx,const std::shared_ptr<OutlierWork>& work,EditorOutlierAnalysisResult& result,
                  std::function<void(EditorOutlierAnalysisResult)> sink,bool automatic)
        {
            auto* residency=ctx.SpatialIndices?ctx.SpatialIndices->PropertyResidency():nullptr;
            if(!residency){result.Status=EditorCommandStatus::InvalidProcessingParameters;result.Message="Outliers need GPU property residency.";return {};}
            auto w=Make(ctx,work,residency);w->AutoAccept=automatic;w->Sink=GuardEditorProcessingResult(ctx,std::move(sink));
            for(const auto& key:w->Keys)if(residency->HasRing(key)){
                result.Status=EditorCommandStatus::InvalidProcessingParameters;result.Message="An outlier output awaits Accept or Discard.";return {};}
            if(auto active=GP::MeshSupport::FindActiveEditorJob(ctx,w->Identity);active&&IsActiveEditorJobState(active->State)){
                result.Status=EditorCommandStatus::Pending;result.Message="An outlier job for this output is active.";return {};}
            std::string why;
            const auto acquired=GP::AcquirePointIndex(*ctx.SpatialIndices,ctx.World,work->Entity,work->Config.Positions,
                work->Slots,work->Points,work->GpuIndex,work->Index,work->Result.IndexReused,why);
            if(acquired!=GP::PointIndexState::Ready){result.Status=EditorCommandStatus::InvalidProcessingParameters;result.Message=why;return {};}
            work->Result.Status=EditorCommandStatus::Pending;work->Result.ActualBackend="vulkan_lbvh";
            work->Result.Message="Device outlier analysis queued.";
            JobDesc job{.DebugName="Device outlier analysis",.Scope=ctx.World,.Kind=RuntimeTaskKinds::GeometryProcess,
                .Work=[](const JobCancellation&){return JobResultEnvelope::Make(true);},.IsReadyToApply=[w]{return Poll(w);},
                .ValidateBeforeApply=[w]{return Current(w)?JobApplyValidation::Current:JobApplyValidation::StaleGeneration;},
                .PublishCompletion=[w](KernelEventBus&,const JobResultEnvelope&){
                    if(w->Delivered)return false;
                    if(!w->Gpu||w->Gpu->State!=SpatialQueryState::Ready||w->Gpu->Data.size()!=sizeof(Graphics::OutlierGpuStats)){
                        Fail(w,w->Gpu?w->Gpu->Diagnostic:"Outlier compute failed.");return false;}
                    Graphics::OutlierGpuStats stats{};std::memcpy(&stats,w->Gpu->Data.data(),sizeof(stats));
                    w->Workspace.reset();
                    if(stats.Invalid){Fail(w,"Unrepresentable device outlier score.");return false;}
                    for(std::size_t i=0;i<3;++i){w->Back[i].reset();if(!w->Residency->Publish(w->Keys[i])){Fail(w,"Outlier ring publication failed.");return false;}}
                    w->Input.reset();for(auto& base:w->Base)base.reset();
                    auto& r=w->Work->Result;r.RejectedCount=stats.Rejected;r.MeanDistance=stats.Mean;r.StdDevDistance=stats.StdDev;r.DistanceThreshold=stats.Threshold;
                    r.Message="GPU outlier fields await Accept or Discard.";w->Phase=EditorGpuTransactionPhase::ReadyToAccept;
                    if(w->AutoAccept){const auto accepted=Accept(w,{});if(accepted.Status!=EditorCommandStatus::Pending)Finish(w,EditorGpuTransactionPhase::Failed,accepted.Status,accepted.Message);}
                    return !w->Delivered;},
                .FinalizeUnpublishedOnMainThread=[w]{if(!w->Delivered)Finish(w,EditorGpuTransactionPhase::Discarded,EditorCommandStatus::StaleEntity,"Outlier run cancelled or stale.");}};
            if(!ctx.JobCommands.Submit(std::move(job),w->Identity).IsValid()){w->Sink={};Fail(w,"Outlier compute submission refused.");result=work->Result;return {};}
            result=work->Result;return w;
        }
    }
    EditorOutlierTransactionHandle StartEditorOutlierAnalysisTransaction(const EditorProcessingCommands& commands,
        const OutlierAnalysisConfig& config,EditorOutlierAnalysisResult& failure)
    {
        failure={.Status=EditorCommandStatus::InvalidProcessingParameters,.Method=config.Method,.RequestedBackend=config.Backend};
        if(config.Backend!=OutlierAnalysisBackend::VulkanLBVH||config.Operation!=OutlierAnalysisOperation::Analyze){failure.Message="Only Vulkan analysis creates an outlier transaction.";return {};}
        const auto& ctx=EditorProcessingCommandsAccess::Resolve(commands);std::string why;
        auto work=Capture(ctx,config,why);if(!work){failure.Message=why;return {};}
        return OutlierTransactionDetail::Start(ctx,work,failure,{},false);
    }
    EditorOutlierTransactionSnapshot SnapshotEditorOutlierAnalysis(const EditorProcessingCommands&,const EditorOutlierTransactionHandle& w)
    {
        EditorOutlierTransactionSnapshot s;if(!w)return s;s.Phase=w->Phase;s.Result=w->Work->Result;
        if(s.Phase==EditorGpuTransactionPhase::ReadyToAccept){
            s.CanAccept=OutlierTransactionDetail::Current(w);
            if(!s.CanAccept)s.AcceptDisabledReason="Outlier input or output changed; discard and run again.";
            else if(!w->TestFront)for(std::size_t i=0;i<2;++i)if(!w->Residency->HasRing(w->Keys[i])){
                s.CanAccept=false;s.AcceptDisabledReason="The result is no longer resident.";}}
        return s;
    }
    EditorOutlierAnalysisResult AcceptEditorOutlierAnalysis(const EditorProcessingCommands&,const EditorOutlierTransactionHandle& w,
        std::function<void(EditorOutlierAnalysisResult)> sink)
    {
        if(!w)return {.Status=EditorCommandStatus::InvalidProcessingParameters,.Message="No outlier transaction."};
        return OutlierTransactionDetail::Accept(w,std::move(sink));
    }
    void DiscardEditorOutlierAnalysis(const EditorProcessingCommands&,const EditorOutlierTransactionHandle& w)
    {
        if(!w||w->Phase==EditorGpuTransactionPhase::Applied||w->Phase==EditorGpuTransactionPhase::Discarded||w->Phase==EditorGpuTransactionPhase::Failed)return;
        w->Abandoned=true;OutlierTransactionDetail::Finish(w,EditorGpuTransactionPhase::Discarded,EditorCommandStatus::NoChange,"Outlier result discarded; CPU fields retained.");
    }

    EditorOutlierTransactionHandle MakeEditorOutlierTransactionForTest(const EditorProcessingCommands& commands,
        const OutlierAnalysisConfig& config,std::vector<float> scores,std::vector<std::uint32_t> mask,
        Graphics::GpuPropertyResidency& residency)
    {
        const auto& ctx=EditorProcessingCommandsAccess::Resolve(commands);
        auto cpu=config;cpu.Backend=OutlierAnalysisBackend::CpuOctree;
        std::string why;auto work=Capture(ctx,cpu,why);
        if(!work||scores.size()!=work->SlotCount||mask.size()!=work->SlotCount)return {};
        work->Config.Backend=config.Backend;work->Result.RequestedBackend=config.Backend;
        work->AfterScore=std::move(scores);work->AfterMask=std::move(mask);
        auto w=OutlierTransactionDetail::Make(ctx,work,&residency);
        for(std::size_t i=0;i<3;++i){
            auto back=AcquireGpuPropertyOutput(residency,ctx.World,work->Entity,w->Refs[i],std::uint32_t(work->SlotCount),3);
            if(!back){OutlierTransactionDetail::Release(w);return {};}
            w->Generations[i]=residency.RingGeneration(w->Keys[i]);back.reset();
            if(!residency.Publish(w->Keys[i])){OutlierTransactionDetail::Release(w);return {};}}
        w->TestFront=true;w->Phase=EditorGpuTransactionPhase::ReadyToAccept;work->Result.Status=EditorCommandStatus::Pending;
        return w;
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
        if (w->Config.Backend == OutlierAnalysisBackend::VulkanLBVH)
        {
            auto result=w->Result;
            (void)OutlierTransactionDetail::Start(context,w,result,std::move(onComplete),true);
            return result;
        }

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
        namespace MS = GeometryProcessingDetail::MeshSupport;
        if (auto busy = MS::ActiveOutputJobRefusal(context, identity, "Outlier estimation"))
            return report(EditorCommandStatus::Pending, std::move(*busy));
        auto queued = w->Result;
        queued.Message = "Outlier estimation queued.";
        const MS::QueuedJobDelivery<EditorOutlierAnalysisResult> delivery{
            context, std::move(onComplete), std::move(queued), "Outlier estimation"};
        JobDesc desc{
            .DebugName = "Outlier estimation",
            .Scope = context.World,
            .Kind = RuntimeTaskKinds::GeometryProcess,
            .Work = [w](const JobCancellation &) -> JobResultEnvelope {
                Compute(*w);
                return JobResultEnvelope::Make(true);
            },
            .ValidateBeforeApply = [context, w] { return MS::ValidateQueuedJob(w->Abandoned, CurrentInput(context, *w)); },
            .PublishCompletion = [context, w, delivery](KernelEventBus &, const JobResultEnvelope &) {
                return delivery.Publish(Publish(context, w));
            },
            .FinalizeUnpublishedOnMainThread = [w, delivery] {
                w->Abandoned = true;
                delivery.Finalize(w->Result.Status == EditorCommandStatus::GeometryProcessingFailed ? &w->Result : nullptr);
            }};
        if (!context.JobCommands.Submit(std::move(desc), identity).IsValid())
        {
            w->Abandoned = true;
            return delivery.Rejected();
        }
        return delivery.Pending();
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

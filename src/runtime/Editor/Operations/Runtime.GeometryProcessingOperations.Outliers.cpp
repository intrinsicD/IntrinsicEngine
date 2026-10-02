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
#include "Editor/Operations/Runtime.GpuTransactionLifecycle.hpp"

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
    // The device outlier run on the shared Run/Accept lifecycle. Rings: score and mask (both
    // read back on Accept) and the mask's float presentation ring.
    struct EditorOutlierTransaction : std::enable_shared_from_this<EditorOutlierTransaction>
    {
        GeometryProcessingDetail::GpuTransactionCore Core{};
        std::shared_ptr<OutlierWork> Work{}; // its Result is the transaction's result
        std::array<GeometryPropertyRef,3> Refs{};
        std::optional<Graphics::GpuPropertyView> Input{};
        std::array<std::optional<Graphics::GpuPropertyView>,2> Base{};
        // Leased from the spatial cache; the recorder closure keeps it while its work may run.
        std::shared_ptr<Graphics::OutlierWorkspace> Workspace{};
        std::function<void(EditorOutlierAnalysisResult)> Sink{};
    };
    namespace OutlierTransactionDetail
    {
        namespace GP=GeometryProcessingDetail;
        using Run=EditorOutlierTransactionHandle;
        bool Current(const Run& w){return GP::GpuTransactionCurrent(w->Core);}
        void Fail(const Run& w,std::string why){GP::FailGpuTransaction(w->Core,std::move(why));}
        bool Acquire(const Run& w)
        {
            auto& t=w->Core;auto& r=*t.Residency;const auto& ctx=t.Context;auto& work=*w->Work;
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
            // Another output identity may share this score or mask name: a ring this run did
            // not create defers until it ends or invalidates the captured output.
            for(std::size_t i=0;i<3;++i)
                if(GP::AcquireGpuTransactionBack(t,i,work.Entity,w->Refs[i],std::uint32_t(work.SlotCount),3)!=GP::GpuRingAcquisition::Ready)
                    return false;
            return true;
        }
        bool Poll(const Run& w)
        {
            auto& t=w->Core;
            if(!t.Gpu){
                if(!Acquire(w)){
                    if(!GP::GpuTransactionDeferralsExhausted(t))return false;
                    Fail(w,"The residency refused an input or output slot.");return true;}
                auto& ctx=t.Context;
                w->Workspace=ctx.SpatialIndices->LeaseGpuWorkspace<Graphics::OutlierWorkspace>();
                if(!w->Workspace){Fail(w,"Outlier device workspace unavailable.");return true;}
                w->Work->Result.GpuQueryBatches=1;
                t.Gpu=ctx.SpatialIndices->QueueGpuCompute(w->Work->GpuIndex,sizeof(Graphics::OutlierGpuStats),
                    // The recorder owns its workspace lease; the cache keeps it until the readback is safe.
                    [w,workspace=w->Workspace](RHI::ICommandContext& cmd,const SpatialGpuIndexView& index)->RHI::BufferHandle{
                        const auto& rings=w->Core.Rings;
                        if(!Current(w)||!w->Input||!rings[0].Back||!rings[1].Back||!rings[2].Back)return {};
                        auto& residency=*w->Core.Residency;
                        const auto frame=w->Core.Context.Device->GetGlobalFrameNumber();
                        residency.NoteUse(w->Input->Buffer,frame);
                        for(std::size_t i=0;i<3;++i)residency.NoteUse(rings[i].Back->Buffer,frame);
                        for(const auto& v:w->Base)if(v)residency.NoteUse(v->Buffer,frame);
                        const auto& c=w->Work->Config;
                        return workspace->Record(cmd,{.Method=std::uint32_t(c.Method),.K=c.KNeighbors,
                            .MinimumNeighbors=c.MinimumNeighbors,.Radius=c.Radius,.Multiplier=c.StdDevMultiplier,.ScoreThreshold=c.ScoreThreshold},
                            {.Positions=*w->Input,.Score=*rings[0].Back,.Mask=*rings[1].Back,.Presentation=*rings[2].Back,
                             .ScoreBase=w->Base[0].value_or(Graphics::GpuPropertyView{}),.MaskBase=w->Base[1].value_or(Graphics::GpuPropertyView{}),
                             .Nodes=index.NodesBDA,.LiveSlots=index.OriginalSlotsBDA,.LiveCount=index.Count});
                    },SpatialGpuLatency::Immediate);
            }
            return t.Gpu->State==SpatialQueryState::Ready||t.Gpu->State==SpatialQueryState::Failed;
        }
        void CompleteRun(const Run& w)
        {
            auto& t=w->Core;
            if(!t.Gpu||t.Gpu->State!=SpatialQueryState::Ready||t.Gpu->Data.size()!=sizeof(Graphics::OutlierGpuStats)){
                Fail(w,t.Gpu?t.Gpu->Diagnostic:"Outlier compute failed.");return;}
            Graphics::OutlierGpuStats stats{};std::memcpy(&stats,t.Gpu->Data.data(),sizeof(stats));
            w->Workspace.reset();
            if(stats.Invalid){Fail(w,"Unrepresentable device outlier score.");return;}
            for(std::size_t i=0;i<3;++i){t.Rings[i].Back.reset();if(!t.Residency->Publish(t.Rings[i].Key)){Fail(w,"Outlier ring publication failed.");return;}}
            w->Input.reset();for(auto& base:w->Base)base.reset();
            auto& r=w->Work->Result;r.RejectedCount=stats.Rejected;r.MeanDistance=stats.Mean;r.StdDevDistance=stats.StdDev;r.DistanceThreshold=stats.Threshold;
            r.Message="GPU outlier fields await Accept or Discard.";
            GP::ReadyGpuTransaction(t);
        }
        void CompleteAccept(const Run& w)
        {
            auto& t=w->Core;auto& work=*w->Work;std::array<std::uint64_t,2> publications{};
            if(!t.TestFront){
                for(std::size_t i=0;i<2;++i){
                    const auto& r=t.Rings[i].Readback;
                    if(!r||r->Failed||r->Bytes.size()!=work.SlotCount*4){Fail(w,"Outlier front readback failed.");return;}
                    publications[i]=r->Lease?r->Lease->Publication:0;
                }
                std::memcpy(work.AfterScore.data(),t.Rings[0].Readback->Bytes.data(),work.SlotCount*4);
                std::memcpy(work.AfterMask.data(),t.Rings[1].Readback->Bytes.data(),work.SlotCount*4);
            }
            for(std::size_t i=0;i<2;++i)if(const auto& r=t.Rings[i].Readback)r->Lease.reset();
            for(const auto row:work.Slots)if(!std::isfinite(work.AfterScore[row])||work.AfterMask[row]>1){Fail(w,"Invalid outlier device result.");return;}
            work.Result.Status=EditorCommandStatus::Applied;work.Result.WrittenCount=work.Slots.size();
            work.Result.ActualBackend="vulkan_lbvh";
            const auto published=Publish(t.Context,w->Work);
            if(!published.Succeeded()){GP::FinishGpuTransaction(t,EditorGpuTransactionPhase::Failed,published.Status,published.Message);return;}
            if(t.Residency){
                const auto a=BuildGeometryAvailability(t.Context.Scene->Raw(),work.Entity);
                for(std::size_t i=0;i<2;++i){
                    const auto watch=GP::ObserveGeometryProperty(a,w->Refs[i].Domain,w->Refs[i].Name);
                    if(!watch.Revision||!t.Residency->BindRevision(t.Rings[i].Key,*watch.Revision,publications[i]))
                        (void)t.Residency->Discard(t.Rings[i].Key,t.Rings[i].Generation);}
                (void)t.Residency->Discard(t.Rings[2].Key,t.Rings[2].Generation);
            }
            GP::FinishGpuTransaction(t,EditorGpuTransactionPhase::Applied,EditorCommandStatus::Applied,
                "Device outlier score and mask accepted; input upload: "+std::to_string(work.Result.GpuInputUploadBytes)+
                " bytes; residency hits: "+std::to_string(work.Result.GpuInputCacheHits)+"; CPU-stage readback: "+std::to_string(work.Result.CpuStageReadbackBytes)+" bytes.");
        }
        Run Make(const EditorProcessingContext& context,const std::shared_ptr<OutlierWork>& work,Graphics::GpuPropertyResidency* residency)
        {
            auto w=std::make_shared<EditorOutlierTransaction>();w->Work=work;
            auto& t=w->Core;t.Context=context;t.Residency=residency;t.Label="Outlier analysis";t.AcceptJobName="Accept outlier fields";t.JobLabel="Outlier estimation";
            w->Refs={work->Config.Score,work->Config.Mask,GpuPropertyPresentationRef(work->Config.Mask)};
            for(std::size_t i=0;i<3;++i)t.Rings[i]={.Key=MakeGpuPropertyKey(context.World,work->Entity,w->Refs[i]),.ReadBack=i<2};
            t.RingCount=3;
            t.Identity={.EntityId=work->Config.StableEntityId,.Scope=ToEditorJobScope(work->Config.Mask.Domain),
                .OutputSemantic=GeometryPresentationSlotSemantic::ScalarField,.OutputName=work->Config.Mask.Name};
            auto* raw=w.get();
            // Hooks run while a job or caller owns the transaction; recorders take ownership.
            const auto self=[raw]{return raw->shared_from_this();};
            t.Hooks={
                .Current=[raw]{return CurrentInput(raw->Core.Context,*raw->Work);},
                .Poll=[self]{return Poll(self());},
                .CompleteRun=[self]{CompleteRun(self());},
                .CompleteAccept=[self]{CompleteAccept(self());},
                .Release=[raw]{raw->Input.reset();for(auto& v:raw->Base)v.reset();raw->Workspace.reset();},
                .Deliver=[raw](EditorCommandStatus status,std::string message){
                    auto& r=raw->Work->Result;r.Status=status;r.Message=std::move(message);
                    if(auto sink=std::move(raw->Sink))sink(r);}};
            return w;
        }
        EditorOutlierAnalysisResult Accept(const Run& w,std::function<void(EditorOutlierAnalysisResult)> sink)
        {
            if(auto refused=GP::GpuTransactionAcceptRefusal(w->Core,bool(sink))){
                auto result=w->Work->Result;result.Status=refused->Status;result.Message=std::move(refused->Message);return result;}
            if(sink)w->Sink=GuardEditorProcessingResult(w->Core.Context,std::move(sink));
            (void)GP::BeginGpuTransactionAccept(GP::GpuTransactionOf(w));
            return w->Work->Result;
        }
        Run Start(const EditorProcessingContext& ctx,const std::shared_ptr<OutlierWork>& work,EditorOutlierAnalysisResult& result,
                  std::function<void(EditorOutlierAnalysisResult)> sink,bool automatic)
        {
            auto w=Make(ctx,work,ctx.SpatialIndices?ctx.SpatialIndices->PropertyResidency():nullptr);w->Core.AutoAccept=automatic;
            if(auto refused=GP::GpuTransactionStartRefusal(w->Core)){
                result.Status=refused->Status;result.Message=std::move(refused->Message);return {};}
            std::string why;
            const auto acquired=GP::AcquirePointIndex(*ctx.SpatialIndices,ctx.World,work->Entity,work->Config.Positions,
                work->Slots,work->Points,work->GpuIndex,work->Index,work->Result.IndexReused,why);
            if(acquired!=GP::PointIndexState::Ready){result.Status=EditorCommandStatus::InvalidProcessingParameters;result.Message=why;return {};}
            work->Result.Status=EditorCommandStatus::Pending;work->Result.ActualBackend="vulkan_lbvh";
            work->Result.Message="Device outlier analysis queued.";
            w->Sink=GuardEditorProcessingResult(ctx,std::move(sink));
            if(!GP::SubmitGpuTransactionRun(GP::GpuTransactionOf(w),"Device outlier analysis").IsValid()){
                result=work->Result;result.Status=EditorCommandStatus::GeometryProcessingFailed;result.Message=GP::MeshSupport::QueuedJobRejectedMessage(w->Core.JobLabel);return {};}
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
        EditorOutlierTransactionSnapshot s;if(!w)return s;s.Phase=w->Core.Phase;s.Result=w->Work->Result;
        if(s.Phase==EditorGpuTransactionPhase::ReadyToAccept){
            s.CanAccept=OutlierTransactionDetail::Current(w);
            if(!s.CanAccept)s.AcceptDisabledReason="Outlier input or output changed; discard and run again.";
            else if(!w->Core.TestFront)for(std::size_t i=0;i<2;++i)if(!w->Core.Residency->HasRing(w->Core.Rings[i].Key)){
                s.CanAccept=false;s.AcceptDisabledReason="The result is no longer resident.";}}
        // Running, accepting or finished: the lifecycle's own refusal says why Accept is not available now.
        else if(const auto refused=GeometryProcessingDetail::GpuTransactionAcceptRefusal(w->Core))s.AcceptDisabledReason=refused->Message;
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
        if(w)GeometryProcessingDetail::DiscardGpuTransaction(w->Core,EditorCommandStatus::NoChange,"Outlier result discarded; CPU fields retained.");
    }

    EditorOutlierTransactionHandle MakeEditorOutlierTransactionForTest(const EditorProcessingCommands& commands,
        const OutlierAnalysisConfig& config,std::vector<float> scores,std::vector<std::uint32_t> mask,
        Graphics::GpuPropertyResidency& residency)
    {
        namespace GP=GeometryProcessingDetail;
        const auto& ctx=EditorProcessingCommandsAccess::Resolve(commands);
        auto cpu=config;cpu.Backend=OutlierAnalysisBackend::CpuOctree;
        std::string why;auto work=Capture(ctx,cpu,why);
        if(!work||scores.size()!=work->SlotCount||mask.size()!=work->SlotCount)return {};
        work->Config.Backend=config.Backend;work->Result.RequestedBackend=config.Backend;
        work->AfterScore=std::move(scores);work->AfterMask=std::move(mask);
        auto w=OutlierTransactionDetail::Make(ctx,work,&residency);
        auto& t=w->Core;
        const auto rollback=[&]{t.Delivered=true;GP::FailGpuTransaction(t,{});return EditorOutlierTransactionHandle{};};
        for(std::size_t i=0;i<3;++i){
            if(GP::AcquireGpuTransactionBack(t,i,work->Entity,w->Refs[i],std::uint32_t(work->SlotCount),3)!=GP::GpuRingAcquisition::Ready)return rollback();
            t.Rings[i].Back.reset();
            if(!residency.Publish(t.Rings[i].Key))return rollback();}
        t.TestFront=true;GP::ReadyGpuTransaction(t);work->Result.Status=EditorCommandStatus::Pending;
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
                                         .OutputName = w->Config.Mask.Name,
                                         .RequestedDomain = EditorJobDomainOfBackend(ToString(config.Backend))};
        namespace MS = GeometryProcessingDetail::MeshSupport;
        if (auto busy = MS::ActiveOutputJobRefusal(context, identity, "Outlier estimation"))
            return report(EditorCommandStatus::Pending, std::move(busy->Message));
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
                delivery.FinalizeAfterWorker(w->Result);
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

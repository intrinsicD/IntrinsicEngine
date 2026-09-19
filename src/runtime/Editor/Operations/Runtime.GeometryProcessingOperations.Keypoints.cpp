module;
#include <functional>
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <utility>
#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>
module Extrinsic.Runtime.PointAnalysisOperations;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Graphics.PointKeypoints;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.SelectionController;
import Geometry.Properties;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Core.Error;
import Geometry.PointCloud.Features;
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
        namespace Features = Geometry::PointCloud::Features;
        using D = GeometryElementDomain;
        using GeometryProcessingDetail::PointPropertyWatch;
        using GeometryProcessingDetail::ObserveGeometryProperty;
        using GeometryProcessingDetail::MutableGeometryProperties;
        using GeometryProcessingDetail::GeometryPropertiesCurrent;
        struct KeypointWork : GeometryProcessingDetail::PointInputCapture
        {
            KeypointAnalysisConfig Config{};
            entt::entity Entity{};
            PointPropertyWatch MaskWatch{}, ScoreWatch{};
            std::vector<std::uint32_t> BeforeMask{}, AfterMask{};
            GeometryProcessingDetail::PointRadiusRows Rows{};
            std::vector<float> BeforeScore{}, AfterScore{};
            std::shared_ptr<const SpatialIndexSnapshot> Index{};
            SpatialIndexHandle GpuIndex{};
            std::shared_ptr<SpatialGpuResult> GpuResult{};
            bool Abandoned{};
            std::optional<EditorKeypointAnalysisResult> MainFailure{};
            EditorKeypointAnalysisResult Result{};
        };
        bool CurrentInput(const EditorProcessingContext& context, const KeypointWork& w)
        {
            const std::array outputs{w.MaskWatch, w.ScoreWatch};
            return GeometryPropertiesCurrent(context, w.Entity, w.Inputs) && GeometryPropertiesCurrent(context, w.Entity, outputs);
        }
        enum class CapturePurpose { Execute, Readiness };
        std::shared_ptr<KeypointWork> Capture(const EditorProcessingContext& context,
            KeypointAnalysisConfig c, std::string& diagnostic, CapturePurpose purpose = CapturePurpose::Execute)
        {
            auto fail = [&](std::string why) -> std::shared_ptr<KeypointWork> { diagnostic = std::move(why); return {}; };
            const auto validation = ValidateKeypointAnalysisConfigSection(
                SerializeKeypointAnalysisConfig(c), {}, kKeypointAnalysisConfigSectionName);
            if (!validation.Usable()) return fail(validation.Diagnostics.front().Message);
            if ((context.AttachmentActive && !context.AttachmentActive()) || !context.Scene)
                return fail("Scene is unavailable.");
            const auto entity = EditorFeatureDetail::ResolveStableEntity(context.Scene->Raw(), c.StableEntityId);
            if (!entity) return fail("Keypoint target entity is stale or missing.");
            const auto a = BuildGeometryAvailability(context.Scene->Raw(), *entity);
            auto w = std::make_shared<KeypointWork>();
            const bool captured = purpose == CapturePurpose::Execute
                ? GeometryProcessingDetail::CapturePointInput(a, c.Positions, true, *w, diagnostic)
                : GeometryProcessingDetail::PreparePointInput(context, *entity, a, c.Positions, *w, diagnostic);
            if (!captured) return {};
            if (c.Mask.Domain == D::Unknown) c.Mask.Domain = c.Positions.Domain;
            if (c.Score.Domain == D::Unknown) c.Score.Domain = c.Positions.Domain;
            const std::array outputs{c.Mask, c.Score};
            if (!GeometryProcessingDetail::ValidatePointOutputs(a, c.Positions, outputs,
                                                                 "Keypoint", diagnostic)) return {};
            const auto* props = ResolveGeometryPropertySet(a, c.Positions.Domain);
            w->Config = c; w->Entity = *entity;
            w->Result.RequestedBackend = c.Backend;
            w->Result.Mask = c.Mask; w->Result.Score = c.Score;
            w->Result.SlotCount = w->SlotCount; w->Result.LiveCount = w->LiveCount;
            w->MaskWatch = ObserveGeometryProperty(a, c.Mask.Domain, c.Mask.Name);
            w->ScoreWatch = ObserveGeometryProperty(a, c.Score.Domain, c.Score.Name);
            if (!w->Result.LiveCount) return fail("Keypoint analysis requires live input samples.");
            if(w->Result.LiveCount<2 || c.MinimumNeighbors>=w->Result.LiveCount)
                return fail("Keypoint analysis requires positive spacing and more live samples than minimum neighbors.");
            if(c.Backend!=KeypointAnalysisBackend::CpuKDTree)
            {
                if(!context.SpatialIndices || !w->ValidLbvh || w->Result.LiveCount>(1u<<24) ||
                   std::max(c.SalientRadius,c.NonMaxRadius)>Geometry::PointLBVH::CoordinateLimit)
                    return fail("LBVH needs the spatial cache, at most 2^24 samples and coordinates/radii within 1e18.");
                if((c.Backend==KeypointAnalysisBackend::VulkanLBVH || c.Backend==KeypointAnalysisBackend::VulkanCompute) &&
                   (!context.JobCommands.Available() || !context.SpatialIndices->GpuQueriesAvailable() || w->Result.LiveCount>(1u<<20)))
                    return fail("Vulkan keypoints require framed GPU queries/jobs and at most 2^20 samples.");
                if(c.Backend==KeypointAnalysisBackend::VulkanCompute &&
                   (!context.Device || !context.Device->SupportsShaderFloat64()))
                    return fail("Vulkan keypoint computation requires shader double-precision support.");
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
            w.Result.MeanSpacing=scale->MeanSpacing;
            w.Result.SalientRadius=scale->SalientRadius;
            w.Result.NonMaxRadius=scale->NonMaxRadius;
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
                    const auto radius=std::max(r.SalientRadius,r.NonMaxRadius);
                    for(std::uint32_t i=0;i<w.Points.size();++i)
                    {
                        const auto neighbors=w.Index->Index.Radius(w.Points[i],radius,std::uint32_t(w.Points.size()-1),i);
                        if(neighbors.Overflowed() || neighbors.TotalCount!=neighbors.Neighbors.size())
                        {r.Message="Incomplete CPU keypoint radius support.";return;}
                        row.clear();for(const auto& n:neighbors.Neighbors)row.push_back(n.Index);
                        if(!AppendRow(w,row))return;
                    }
                }
                analysis=Features::AnalyzeKeypointsFromNeighbors(w.Points,Parameters(c),{.MeanSpacing=r.MeanSpacing, .SalientRadius=r.SalientRadius, .NonMaxRadius=r.NonMaxRadius},
                    {w.Rows.Offsets,w.Rows.Indices});
            }
            if(!analysis) {r.Message="Keypoint analysis rejected invalid support or unrepresentable covariance.";return;}
            for(std::size_t i=0;i<w.Slots.size();++i)
            {w.AfterMask[w.Slots[i]]=analysis->Mask[i];w.AfterScore[w.Slots[i]]=analysis->Saliency[i];}
            r.KeypointCount=analysis->Keypoints.Indices.size();r.WrittenCount=w.Slots.size();
            r.MeanSpacing=analysis->Scale.MeanSpacing;
            r.SalientRadius=analysis->Scale.SalientRadius;
            r.NonMaxRadius=analysis->Scale.NonMaxRadius;
            r.Status=EditorCommandStatus::Applied;
            r.CpuComputeMilliseconds=(c.Backend==KeypointAnalysisBackend::VulkanLBVH?r.CpuComputeMilliseconds:0)+
                std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
            r.Message="Keypoint mask and saliency computed with "+r.ActualBackend+" neighborhoods; scale, covariance and suppression run on CPU.";
        }
        bool AdvanceGpu(const EditorProcessingContext& context,KeypointWork& w)
        {
            if(w.Abandoned || !CurrentInput(context,w))
            {
                w.Result.Status=EditorCommandStatus::StaleEntity;
                w.Result.Message="Keypoint input changed or the job was cancelled.";
                w.MainFailure=w.Result;w.Rows.Batch.reset();return true;
            }
            std::string diagnostic;
            const auto state=GeometryProcessingDetail::AdvancePointRadiusRows(*context.SpatialIndices,w.GpuIndex,
                w.Points,w.Slots,std::max(w.Result.SalientRadius,w.Result.NonMaxRadius),w.Config.GpuQueryBatchSize,w.Config.GpuRadiusCapacity,0,w.Rows,diagnostic);
            w.Result.MaximumNeighbors=w.Rows.MaximumNeighbors;w.Result.GpuQueryBatches=w.Rows.QueryBatches;
            w.Result.GpuNeighborhoodMilliseconds=w.Rows.Milliseconds;
            if(w.Rows.Queried)w.Result.ActualBackend="vulkan_lbvh";
            if(state==GeometryProcessingDetail::RadiusRowsState::Failed)
            {w.Result.Status=EditorCommandStatus::GeometryProcessingFailed;w.Result.Message=std::move(diagnostic);w.MainFailure=w.Result;}
            return state!=GeometryProcessingDetail::RadiusRowsState::Pending;
        }
        EditorKeypointAnalysisResult Publish(const EditorProcessingContext& context,
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
                if (context.InvalidateWorkspaceSnapshotCache) context.InvalidateWorkspaceSnapshotCache();
                return EditorCommandHistoryStatus::Applied;
            };
            const auto status=context.CommandHistory ? context.CommandHistory->Execute({.Label="Detect keypoints",
                .Redo=[mutate,after]{return mutate(*after);},.Undo=[mutate,before]{return mutate(*before);}}).Status : mutate(*after);
            r.Status=EditorFeatureDetail::ToEditorCommandStatus(status);
            if (!r.Succeeded()) r.Message="Keypoint publication rejected by history checks.";
            return r;
        }
    }
    ActionReadiness PreviewEditorKeypointAnalysisCommand(
        const EditorProcessingCommands& commands,const KeypointAnalysisConfig& config)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        std::string diagnostic;
        const auto work = Capture(context, config, diagnostic, CapturePurpose::Readiness);
        return {bool(work), std::move(diagnostic)};
    }
    EditorKeypointAnalysisResult ApplyEditorKeypointAnalysisCommand(
        const EditorProcessingCommands& commands,const KeypointAnalysisConfig& config, std::function<void(EditorKeypointAnalysisResult)> onComplete)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
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
            const auto indexState = GeometryProcessingDetail::AcquirePointIndex(
                *context.SpatialIndices, context.World, w->Entity, w->Config.Positions,
                w->Slots, w->Points, w->GpuIndex, w->Index, w->Result.IndexReused, diagnostic);
            if (indexState == GeometryProcessingDetail::PointIndexState::Unavailable)
                return report(EditorCommandStatus::InvalidProcessingParameters, diagnostic);
            if (indexState == GeometryProcessingDetail::PointIndexState::Mismatched)
                return report(EditorCommandStatus::StaleEntity,
                              "Keypoint index does not match the selected samples.");
        }
        if(!context.JobCommands.Available()){Compute(*w);return Publish(context,w);}
        const EditorJobIdentity identity{.EntityId=config.StableEntityId,.Scope=ToEditorJobScope(w->Config.Mask.Domain),
            .OutputSemantic=GeometryPresentationSlotSemantic::ScalarField,.OutputName=w->Config.Mask.Name};
        if (auto active = GeometryProcessingDetail::MeshSupport::FindActiveEditorJob(context, identity);
            active && IsActiveEditorJobState(active->State))
            return report(EditorCommandStatus::Pending,"A keypoint job for this output is already active.");
        auto sink=GuardEditorProcessingResult(context, std::move(onComplete));auto delivered=std::make_shared<bool>(false);
        auto pending=report(EditorCommandStatus::Pending,"Keypoint analysis queued.");
        // Once submitted, Result belongs to the running stage. Submission failures
        // report from this immutable snapshot while earlier stages wind down.
        const auto rejected=[pending](std::string message)
        {auto result=pending;result.Status=EditorCommandStatus::GeometryProcessingFailed;result.Message=std::move(message);return result;};
        if(w->Config.Backend==KeypointAnalysisBackend::VulkanCompute)
        {
            JobDesc gpu{
                .DebugName="Vulkan keypoint computation", .Scope=context.World, .Kind=RuntimeTaskKinds::GeometryProcess,
                .Work=[](const JobCancellation&){return JobResultEnvelope::Make(true);},
                .IsReadyToApply=[context,w] {
                    if(w->Abandoned || !CurrentInput(context,*w))return true;
                    if(!w->GpuResult)
                    {
                        auto workspace=std::make_shared<Graphics::PointKeypointWorkspace>(*context.Device);
                        const auto& c=w->Config;
                        const Graphics::PointKeypointParams params{c.SalientRadius,c.NonMaxRadius,c.Gamma21,c.Gamma32,
                            c.MinimumNeighbors,c.GpuRadiusCapacity,c.GpuQueryBatchSize};
                        w->GpuResult=context.SpatialIndices->QueueGpuCompute(w->GpuIndex,
                            sizeof(Graphics::PointKeypointHeader)+w->Slots.size()*sizeof(Graphics::PointKeypointValue),
                            [workspace,params,w](auto& commands,const SpatialGpuIndexView& view) -> RHI::BufferHandle {
                                if(w->Abandoned)return {};
                                return workspace->Record(commands,view.NodesBDA,view.PositionsBDA,
                                    view.OriginalSlotsBDA,view.Count,params);
                            });
                    }
                    return w->GpuResult->State==SpatialQueryState::Ready || w->GpuResult->State==SpatialQueryState::Failed;
                },
                .ValidateBeforeApply=[context,w] {return !w->Abandoned && CurrentInput(context,*w)?JobApplyValidation::Current:JobApplyValidation::StaleGeneration;},
                .PublishCompletion=[context,w,sink,delivered](KernelEventBus&,const JobResultEnvelope&) {
                    auto& r=w->Result;r.Status=EditorCommandStatus::GeometryProcessingFailed;
                    if(!w->GpuResult || w->GpuResult->State!=SpatialQueryState::Ready)
                        r.Message=w->GpuResult?w->GpuResult->Diagnostic:"Vulkan keypoint computation did not return a result.";
                    else
                    {
                        r.ActualBackend="vulkan_compute";
                        Graphics::PointKeypointHeader header{};
                        std::memcpy(&header,w->GpuResult->Data.data(),sizeof(header));
                        r.MaximumNeighbors=header.MaximumNeighbors;r.KeypointCount=header.KeypointCount;
                        r.GpuQueryBatches=3*((w->Slots.size()+w->Config.GpuQueryBatchSize-1)/w->Config.GpuQueryBatchSize)+1;
                        r.MeanSpacing=header.MeanSpacing;
                        r.SalientRadius=header.SalientRadius;
                        r.NonMaxRadius=header.NonMaxRadius;
                        if(header.Error)
                            r.Message=(header.Error&2)?"Vulkan keypoint support overflowed capacity; previous outputs retained.":
                                (header.Error&8)?"Vulkan keypoint spatial traversal exceeded its stack limit.":
                                "Vulkan keypoint computation rejected invalid scale or nonfinite covariance.";
                        else
                        {
                            for(std::size_t i=0;i<w->Slots.size();++i)
                            {
                                Graphics::PointKeypointValue value{};
                                std::memcpy(&value,w->GpuResult->Data.data()+sizeof(header)+i*sizeof(value),sizeof(value));
                                w->AfterMask[w->Slots[i]]=value.Mask;w->AfterScore[w->Slots[i]]=value.Saliency;
                            }
                            r.WrittenCount=w->Slots.size();r.Status=EditorCommandStatus::Applied;
                            r.Message="Spacing, covariance, saliency and suppression computed on Vulkan; final properties read back.";
                        }
                    }
                    auto result=Publish(context,w);*delivered=true;if(sink)sink(result);return result.Succeeded();
                },
                .FinalizeUnpublishedOnMainThread=[w,sink,delivered,pending]() mutable {
                    w->Abandoned=true;if(*delivered)return;*delivered=true;
                    pending.Status=EditorCommandStatus::StaleEntity;
                    pending.Message="Vulkan keypoint job cancelled or stale; previous outputs retained.";
                    if(sink)sink(std::move(pending));
                }};
            if(!context.JobCommands.Submit(std::move(gpu),identity).IsValid())
                return rejected("Vulkan keypoint submission rejected.");
            return pending;
        }
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
    EditorKeypointAnalysisResult ApplyEditorConfiguredKeypointAnalysis(const EditorProcessingCommands& commands, std::function<void(EditorKeypointAnalysisResult)> onComplete)
    {
        const auto config=GetEditorKeypointAnalysisConfig(commands);
        if(!config)return {.Status=EditorCommandStatus::InvalidProcessingParameters,.Message="Keypoint config is unavailable."};
        return ApplyEditorKeypointAnalysisCommand(commands, *config, std::move(onComplete));
    }
} // namespace Extrinsic::Runtime

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
#include "Modules/KeypointAnalysis/Runtime.KeypointPaging.TestSupport.hpp"
module Extrinsic.Runtime.PointAnalysisOperations;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Graphics.PointScalarAnalysis;
import Extrinsic.Graphics.PointKeypoints;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.CommandContext;
import Extrinsic.Runtime.GpuPropertyBinding;
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
#include "Editor/internal/Runtime.EditorFramedGpuJob.hpp"
#include "Editor/internal/Runtime.EditorGeometryHelpers.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.PointFields.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.JobFailure.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.GpuScalar.hpp"
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
        using GeometryProcessingDetail::GeometryPropertiesCurrent;
        struct KeypointWork : GeometryProcessingDetail::PointInputCapture
        {
            KeypointAnalysisConfig Config{};
            entt::entity Entity{};
            PointPropertyWatch MaskWatch{}, ScoreWatch{};
            GeometryScalarPropertySnapshot BeforeMask{}, BeforeScore{};
            std::vector<std::uint32_t> AfterMask{};
            GeometryProcessingDetail::PointRadiusRows Rows{};
            std::vector<float> AfterScore{};
            std::shared_ptr<const SpatialIndexSnapshot> Index{};
            SpatialIndexHandle GpuIndex{};
            bool Abandoned{}, Admitted{};
            std::weak_ptr<EditorPointScalarTransaction> Transaction{};
            std::shared_ptr<Graphics::PointKeypointWorkspace> Workspace{};
            std::optional<Graphics::GpuPropertyView> Input{}, ScoreBase{}, MaskBase{};
            std::optional<EditorPointScalarBack> Back{};
            Graphics::PointKeypointPage Page{};
            KeypointPagingLimits Limits{};
            Graphics::PointKeypointHeader Header{};
            std::optional<EditorKeypointAnalysisResult> MainFailure{};
            EditorKeypointAnalysisResult Result{};
        };
        bool CurrentInput(const EditorProcessingContext& context, const KeypointWork& w)
        {
            const std::array outputs{w.MaskWatch, w.ScoreWatch};
            return GeometryProcessingDetail::EditorProcessingContextWorldCurrent(context) && GeometryPropertiesCurrent(context, w.Entity, w.Inputs) && GeometryPropertiesCurrent(context, w.Entity, outputs);
        }
        enum class CapturePurpose { Execute, Readiness };
        std::shared_ptr<KeypointWork> Capture(const EditorProcessingContext& context,
            KeypointAnalysisConfig c, std::string& diagnostic, CapturePurpose purpose = CapturePurpose::Execute)
        {
            auto fail = [&](std::string why) -> std::shared_ptr<KeypointWork> { diagnostic = std::move(why); return {}; };
            const auto validation = ValidateKeypointAnalysisConfigSection(
                SerializeKeypointAnalysisConfig(c), {}, kKeypointAnalysisConfigSectionName);
            if (!validation.Usable()) return fail(validation.Diagnostics.front().Message);
            if (!GeometryProcessingDetail::EditorProcessingContextWorldCurrent(context) || !context.Scene)
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
            w->Result.ImplementationId = c.Backend==KeypointAnalysisBackend::VulkanCompute ? "vulkan.keypoints.resident.paged.v1" : "geometry.keypoints.centroid_pca";
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
                w->BeforeMask = CaptureGeometryScalarProperty(*props, c.Mask);
                w->BeforeScore = CaptureGeometryScalarProperty(*props, c.Score);
                w->AfterMask.resize(props->Size());
                w->AfterScore.resize(props->Size());
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
            if(state==GeometryProcessingDetail::RowsState::Failed)
            {w.Result.Status=EditorCommandStatus::GeometryProcessingFailed;w.Result.Message=std::move(diagnostic);w.MainFailure=w.Result;}
            return state!=GeometryProcessingDetail::RowsState::Pending;
        }
        EditorKeypointAnalysisResult Publish(const EditorProcessingContext& context,
                                            const std::shared_ptr<KeypointWork>& w)
        {
            auto& r=w->Result;
            if (w->Abandoned || !CurrentInput(context,*w))
            {r.Status=EditorCommandStatus::StaleEntity; r.Message="Keypoint input or output changed before publication."; return r;}
            if (r.Status!=EditorCommandStatus::Applied) return r;
            const std::array outputs{
                GeometryProcessingDetail::PointScalarOutput{.Output=w->Config.Mask,.Watch=w->MaskWatch,.Before=&w->BeforeMask,.AfterUInt=w->AfterMask},
                GeometryProcessingDetail::PointScalarOutput{.Output=w->Config.Score,.Watch=w->ScoreWatch,.Before=&w->BeforeScore,.After=w->AfterScore}};
            const auto status=GeometryProcessingDetail::PublishPointScalarField(context,w->Entity,*w,outputs,"Detect keypoints");
            r.Status = status == EditorCommandHistoryStatus::InvalidCommand
                ? EditorCommandStatus::InvalidProcessingParameters
                : EditorFeatureDetail::ToEditorCommandStatus(status);
            if (!r.Succeeded()) r.Message = status == EditorCommandHistoryStatus::InvalidCommand
                ? "Output values are not exactly representable in the selected scalar storage."
                : "Keypoint publication rejected by history checks.";
            return r;
        }
    }
    EditorPointScalarTransactionHandle StartEditorKeypointAnalysisTransaction(
        const EditorProcessingCommands& commands,const KeypointAnalysisConfig& config,
        EditorKeypointAnalysisResult& result,std::function<void(EditorKeypointAnalysisResult)> sink,bool automatic)
    {
        const auto& context=EditorProcessingCommandsAccess::Resolve(commands);
        std::string why;
        auto w=Capture(context,config,why);
        result={.Status=EditorCommandStatus::InvalidProcessingParameters,.RequestedBackend=config.Backend,.Message=why};
        if(!w)return {};
        result=w->Result;
        const auto refuse=[&](std::string message)->EditorPointScalarTransactionHandle {
            result.Status=EditorCommandStatus::GeometryProcessingFailed;result.Message=std::move(message);return {};
        };
        if(config.Backend!=KeypointAnalysisBackend::VulkanCompute)
            return refuse("Resident keypoints require the Vulkan compute backend.");
        const EditorJobIdentity identity{.EntityId=config.StableEntityId,.Scope=ToEditorJobScope(w->Config.Mask.Domain),
            .OutputSemantic=GeometryPresentationSlotSemantic::ScalarField,.OutputName=w->Config.Mask.Name};
        // A duplicate is Pending like every queued editor job; the active run keeps its callback.
        if(auto busy=GeometryProcessingDetail::MeshSupport::ActiveOutputJobRefusal(context,identity,"Keypoint analysis"))
        {result.Status=EditorCommandStatus::Pending;result.Message=std::move(busy->Message);return {};}
        auto scoreOutput=w->Config.Score, maskOutput=w->Config.Mask;
        scoreOutput.ValueKind=Geometry::PropertyValueKind::Float;
        maskOutput.ValueKind=Geometry::PropertyValueKind::UInt32;
        auto* residency=context.SpatialIndices->PropertyResidency();
        if(!residency)return refuse("Keypoint residency unavailable.");
        const auto uploads=residency->Stats().UploadBytes;
        w->Input=ResolveGpuPropertyInput(*residency,*context.Scene,context.World,w->Entity,w->Config.Positions);
        if(!w->Input)return refuse("Keypoint resident input acquisition refused.");
        w->Result.GpuInputUploadBytes=residency->Stats().UploadBytes-uploads;
        w->Result.GpuInputCacheHits=w->Result.GpuInputUploadBytes?0:1;
        if(w->ScoreWatch.Revision && w->Config.Score.ValueKind==scoreOutput.ValueKind)w->ScoreBase=ResolveGpuPropertyInput(*residency,*context.Scene,context.World,w->Entity,w->Config.Score);
        if(w->MaskWatch.Revision && w->Config.Mask.ValueKind==maskOutput.ValueKind)w->MaskBase=ResolveGpuPropertyInput(*residency,*context.Scene,context.World,w->Entity,w->Config.Mask);
        if((w->ScoreWatch.Revision && w->Config.Score.ValueKind==scoreOutput.ValueKind && !w->ScoreBase)||
            (w->MaskWatch.Revision && w->Config.Mask.ValueKind==maskOutput.ValueKind && !w->MaskBase))return refuse("Keypoint output base acquisition refused.");
        w->Result.CpuStageUploadBytes=residency->Stats().UploadBytes-uploads-w->Result.GpuInputUploadBytes;
        const auto indexState=GeometryProcessingDetail::AcquirePointIndex(*context.SpatialIndices,context.World,w->Entity,
            w->Config.Positions,w->Slots,w->Points,w->GpuIndex,w->Index,w->Result.IndexReused,why);
        if(indexState==GeometryProcessingDetail::PointIndexState::Unavailable || indexState==GeometryProcessingDetail::PointIndexState::Mismatched)return refuse(why);
        w->Result.Status=EditorCommandStatus::Pending;
        w->Result.Message="Keypoint traversal queued; score preview follows suppression.";
        EditorPointScalarTransactionSnapshot initial;
        auto transaction=BeginEditorPointScalarPublication(commands,{
            .EntityId=SelectionController::ToStableEntityId(w->Entity),.Count=std::uint32_t(w->SlotCount),
            .Output=scoreOutput,.Label="Detect keypoints",
            .Current=[context,w]{return !w->Abandoned && CurrentInput(context,*w);},
            .Companion=maskOutput,
            .PublishPair=[context,w](std::span<const std::byte> score,std::span<const std::byte> mask) {
                std::memcpy(w->AfterScore.data(),score.data(),score.size());
                std::memcpy(w->AfterMask.data(),mask.data(),mask.size());
                w->Result.Status=EditorCommandStatus::Applied;w->Result.WrittenCount=w->Slots.size();
                return Publish(context,w).Status;
            }},initial,[w,sink=GuardEditorProcessingResult(context,std::move(sink))](auto snapshot) {
                w->Input.reset();w->ScoreBase.reset();w->MaskBase.reset();w->Back.reset();
                if(w->Result.Status!=EditorCommandStatus::GeometryProcessingFailed && w->Result.Status!=EditorCommandStatus::StaleEntity) {
                    w->Result.Status=snapshot.Status;w->Result.Message=snapshot.Message;
                }
                w->Result.CpuStageReadbackBytes+=snapshot.CpuStageReadbackBytes;
                w->Abandoned=true;
                if(w->Admitted && sink)sink(w->Result);
            });
        if(!transaction)return refuse(initial.Message);
        w->Transaction=transaction;
        w->Back=AcquireEditorPointScalarBack(transaction);
        if(!w->Back) {DiscardEditorPointScalar(commands,transaction);return refuse("Keypoint output rings unavailable.");}
        w->Limits=KeypointPagingForTesting;
        w->Limits.Pairs=std::clamp(w->Limits.Pairs,1u,1u<<24u);
        w->Limits.Visits=std::clamp(w->Limits.Visits,1u,std::min(1024u,std::max(1u,w->Limits.Pairs)));
        w->Limits.Rows=std::max(1u,std::min({w->Limits.Rows,
            std::max(1u,w->Limits.Pairs/w->Limits.Visits),16384u}));
        w->Page={.Rows=std::min(w->Limits.Rows,std::uint32_t(w->LiveCount)),.Visits=w->Limits.Visits};
        w->Workspace=context.SpatialIndices->LeaseGpuWorkspace<Graphics::PointKeypointWorkspace>();
        if(!w->Workspace) {
            DiscardEditorPointScalar(commands,transaction);
            return refuse("Keypoint device workspace unavailable.");
        }
        const auto current=[context,w] {
            const auto t=w->Transaction.lock();
            return t && !w->Abandoned && CurrentInput(context,*w) &&
                SnapshotEditorPointScalar(BindEditorProcessingCommands(context),t).Phase==EditorGpuTransactionPhase::Running;
        };
        JobDesc job=EditorFeatureDetail::MakeFramedGpuJobDesc({
            .DebugName="Resident paged keypoints",.Scope=context.World,.Current=current,
            .Queue=[context,w,residency] {
                ++w->Result.GpuQueryBatches;
                w->Result.CpuStageUploadBytes+=(w->Page.Mode==0 && w->Page.First==0 && !w->Page.Resume)?32:4;
                if(w->Page.Mode==2 && w->Page.First==0 && !w->Page.Resume)w->Result.CpuStageUploadBytes+=4;
                return context.SpatialIndices->QueueGpuCompute(w->GpuIndex,sizeof(Graphics::PointKeypointHeader),
                    [context,w,residency,input=w->Input,back=w->Back,scoreBase=w->ScoreBase,maskBase=w->MaskBase](RHI::ICommandContext& cmd,const SpatialGpuIndexView& index) {
                        if(w->Abandoned || !w->Workspace || !input || !back)return RHI::BufferHandle{};
                        const auto frame=context.Device->GetGlobalFrameNumber();
                        for(auto buffer:{input->Buffer,back->Typed.Buffer,back->Companion.Buffer})residency->NoteUse(buffer,frame);
                        if(w->Page.Mode==4 && w->Page.First==0) {
                            const auto initialize=[&](const auto& base,auto output) {
                                if(base) {
                                    residency->NoteUse(base->Buffer,frame);
                                    cmd.BufferBarrier(base->Buffer,RHI::MemoryAccess::TransferWrite|RHI::MemoryAccess::ShaderWrite,RHI::MemoryAccess::TransferRead);
                                    cmd.CopyBuffer(base->Buffer,output,0,0,w->SlotCount*4);
                                } else cmd.FillBuffer(output,0,w->SlotCount*4,0);
                            };
                            initialize(scoreBase,back->Typed.Buffer);
                            initialize(maskBase,back->Companion.Buffer);
                            for(auto b:{back->Typed.Buffer,back->Companion.Buffer})
                                cmd.BufferBarrier(b,RHI::MemoryAccess::TransferWrite,RHI::MemoryAccess::ShaderWrite);
                        }
                        const auto& c=w->Config;
                        return w->Workspace->RecordPage(cmd,index.NodesBDA,*input,index.OriginalSlotsBDA,index.Count,
                            {c.SalientRadius,c.NonMaxRadius,c.Gamma21,c.Gamma32,c.MinimumNeighbors},
                            w->Page,back->Typed,back->Companion);
                    },SpatialGpuLatency::Immediate);
            },
            .Observe=[w](const SpatialGpuResult& gpu) {
                if(gpu.Data.size()!=sizeof(w->Header))return true;
                w->Result.ActualBackend="vulkan_compute";
                w->Result.CpuStageReadbackBytes+=gpu.Data.size();
                w->Result.CpuStageUploadBytes+=gpu.CpuStageUploadBytes;
                std::memcpy(&w->Header,gpu.Data.data(),sizeof(w->Header));
                auto& p=w->Page;
                if(w->Header.Error)return true;
                if(w->Header.Reserved){p.Resume=1;return false;}
                p.Resume=0;p.First+=p.Rows;
                if(p.First>=w->LiveCount || p.Mode==1) {
                    p.First=0;
                    if(p.Mode==4)return true;
                    p.Mode=p.Mode==0?5:p.Mode==5?1:p.Mode==1?2:p.Mode+1;
                }
                if(p.Mode==4 || p.Mode==5)p.Rows=std::uint32_t(w->LiveCount);
                else p.Rows=std::min(w->Limits.Rows,std::uint32_t(w->LiveCount)-p.First);
                return false;
            },
            .Publish=[commands,w,transaction,automatic](const SpatialGpuResult* gpu) {
                if(!gpu || gpu->State!=SpatialQueryState::Ready || gpu->Data.size()!=sizeof(w->Header) || w->Header.Error) {
                    w->Result.Status=EditorCommandStatus::GeometryProcessingFailed;
                    w->Result.Message=w->Header.Error?"Keypoints rejected invalid scale, covariance or spatial traversal.":"Keypoint GPU submission failed.";
                    DiscardEditorPointScalar(commands,transaction);return false;
                }
                auto& r=w->Result;r.MeanSpacing=w->Header.MeanSpacing;r.SalientRadius=w->Header.SalientRadius;
                r.NonMaxRadius=w->Header.NonMaxRadius;r.MaximumNeighbors=w->Header.MaximumNeighbors;r.KeypointCount=w->Header.KeypointCount;
                w->Back.reset();w->Input.reset();w->ScoreBase.reset();w->MaskBase.reset();
                w->Workspace.reset(); // Final page completion proves scratch is no longer in use.
                if(!PublishEditorPointScalarBack(transaction,true)){DiscardEditorPointScalar(commands,transaction);return false;}
                if(automatic) {
                    const auto accepted=AcceptEditorPointScalar(commands,transaction);
                    if(accepted.Status!=EditorCommandStatus::Pending) {
                        w->Result.Status=accepted.Phase==EditorGpuTransactionPhase::Failed?accepted.Status:EditorCommandStatus::StaleEntity;w->Result.Message=accepted.Message;
                        DiscardEditorPointScalar(commands,transaction);
                    }
                }
                return true;
            },
            .Abandon=[commands,w,transaction] {
                if(!w->Abandoned) {w->Result.Status=EditorCommandStatus::StaleEntity;w->Result.Message="Keypoint job stopped, detached or stale.";DiscardEditorPointScalar(commands,transaction);}
            }});
        EditorJobIdentity runIdentity{.EntityId=config.StableEntityId,.Scope=ToEditorJobScope(w->Config.Mask.Domain),.OutputSemantic=GeometryPresentationSlotSemantic::ScalarField,.OutputName=w->Config.Mask.Name};
        const auto token=context.JobCommands.Submit(std::move(job),runIdentity);
        if(!token.IsValid()) {
            DiscardEditorPointScalar(commands,transaction);return refuse(GeometryProcessingDetail::MeshSupport::QueuedJobRejectedMessage("Keypoint analysis"));
        }
        runIdentity.Run=token; // the publication's Accept stage joins this run
        GeometryProcessingDetail::JoinPointScalarRun(transaction,std::move(runIdentity));
        w->Admitted=true;result=w->Result;return transaction;
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
        if(config.Backend==KeypointAnalysisBackend::VulkanCompute) {
            EditorKeypointAnalysisResult result;
            (void)StartEditorKeypointAnalysisTransaction(commands,config,result,std::move(onComplete),true);
            return result;
        }
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
        EditorJobIdentity identity{.EntityId=config.StableEntityId,.Scope=ToEditorJobScope(w->Config.Mask.Domain),
            .OutputSemantic=GeometryPresentationSlotSemantic::ScalarField,.OutputName=w->Config.Mask.Name};
        namespace MS = GeometryProcessingDetail::MeshSupport;
        if (auto busy = MS::ActiveOutputJobRefusal(context, identity, "Keypoint analysis"))
            return report(EditorCommandStatus::Pending, std::move(busy->Message)); // the active job owns the callback
        // Once submitted, Result belongs to the running stage. Submission failures
        // report from the delivery's immutable snapshot while earlier stages wind down.
        const MS::QueuedJobDelivery<EditorKeypointAnalysisResult> delivery{
            context, std::move(onComplete), report(EditorCommandStatus::Pending, "Keypoint analysis queued."), "Keypoint analysis"};
        const auto validate=[context,w]{return MS::ValidateQueuedJob(w->Abandoned,CurrentInput(context,*w));};
        JobDesc desc{
            .DebugName="Keypoint covariance and suppression",.Scope=context.World,.Kind=RuntimeTaskKinds::GeometryProcess,
            .Work=[w](const JobCancellation&){Compute(*w);return JobResultEnvelope::Make(true);},
            .ValidateBeforeApply=validate,
            .PublishCompletion=[context,w,delivery](KernelEventBus&,const JobResultEnvelope&){return delivery.Publish(Publish(context,w));},
            .FinalizeUnpublishedOnMainThread=[w,delivery]{w->Abandoned=true;delivery.Finalize(w->MainFailure);}};
        if(w->Config.Backend==KeypointAnalysisBackend::VulkanLBVH)
        {
            JobDesc prepare{
                .DebugName="Keypoint scale",.Scope=context.World,.Kind=RuntimeTaskKinds::GeometryProcess,
                .Work=[w](const JobCancellation&){Prepare(*w);return JobResultEnvelope::Make(true);},
                .ValidateBeforeApply=validate,
                .PublishCompletion=[w](KernelEventBus&,const JobResultEnvelope&)
                {if(w->Result.Status==EditorCommandStatus::GeometryProcessingFailed){w->MainFailure=w->Result;return false;}return true;},
                .FinalizeUnpublishedOnMainThread=[w]{w->Abandoned=true;}};
            const auto scale=context.JobCommands.Submit(std::move(prepare),identity);
            identity.Run=scale; // later stages join the run
            if(!scale.IsValid())return delivery.Rejected("scale");
            JobDesc gpu{
                .DebugName="Keypoint radius support (Vulkan)",.Scope=context.World,.Kind=RuntimeTaskKinds::GeometryProcess,
                .Work=[](const JobCancellation&){return JobResultEnvelope::Make(true);},
                .IsReadyToApply=[context,w]{return AdvanceGpu(context,*w);},
                .PublishCompletion=[w](KernelEventBus&,const JobResultEnvelope&){return w->Rows.Finished;},
                .FinalizeUnpublishedOnMainThread=[w]{w->Abandoned=true;}};
            gpu.DependsOn.push_back({scale,"Resolve keypoint radii before complete Vulkan support"});
            const auto support=context.JobCommands.Submit(std::move(gpu),identity);
            if(!support.IsValid()){w->Abandoned=true;return delivery.Rejected("Vulkan radius support");}
            desc.DependsOn.push_back({support,"Complete radius support before keypoint covariance and suppression"});
        }
        const auto token=context.JobCommands.Submit(std::move(desc),identity);
        if(!token.IsValid()){w->Abandoned=true;return delivery.Rejected();}
        return delivery.Pending();
    }
    EditorKeypointAnalysisResult ApplyEditorConfiguredKeypointAnalysis(const EditorProcessingCommands& commands, std::function<void(EditorKeypointAnalysisResult)> onComplete)
    {
        const auto config=GetEditorKeypointAnalysisConfig(commands);
        if(!config)return {.Status=EditorCommandStatus::InvalidProcessingParameters,.Message="Keypoint config is unavailable."};
        return ApplyEditorKeypointAnalysisCommand(commands, *config, std::move(onComplete));
    }
} // namespace Extrinsic::Runtime

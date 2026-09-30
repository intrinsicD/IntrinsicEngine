module;
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include <glm/glm.hpp>
#include <entt/entity/entity.hpp>
#include "Modules/Clustering/Runtime.KMeansPaging.TestSupport.hpp"
module Extrinsic.Runtime.ClusteringModule;
import :GpuBackend;
import Geometry.KMeans;
import Extrinsic.Runtime.EditorProcessing;
import Extrinsic.Runtime.PointScalarTransaction;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.GpuPropertyBinding;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.TransferQueue;
import Extrinsic.RHI.Types;
#include "Modules/Clustering/Runtime.ClusteringGpuState.Internal.hpp"
namespace Extrinsic::Runtime
{
    struct ClusteringGpuState::Impl
    {
        struct Operation
        {
            KMeansSnapshot Snapshot{};
            EditorProcessingContext Context{};
            EditorProcessingCommands Commands{};
            EditorPointScalarTransactionHandle Transaction{};
            std::shared_ptr<KMeansGpuWorkspace> Workspace{};
            std::optional<Graphics::GpuPropertyView> Input{}, Base{};
            std::optional<EditorPointScalarBack> Back{};
            std::shared_ptr<SpatialGpuResult> Page{};
            std::function<bool()> Current{};
            std::function<EditorCommandStatus(const Geometry::KMeans::KMeansResult&)> Publish{};
            Geometry::KMeans::KMeansResult Geometry{};
            KMeansRunCompleted Result{};
            KMeansGpuPhase Phase{KMeansGpuPhase::Initialize};
            KMeansPagingLimits Limits{KMeansPagingForTesting};
            std::uint32_t Row{}, Inner{}, Count{}, Clusters{}, PreviewDeferrals{};
            bool Boundary{}, Terminal{}, Ready{}, Accepting{}, Stop{}, Discard{}, CentroidReadback{}, Recorded{};
        };
        RHI::IDevice& Device;
        std::shared_ptr<bool> Alive{std::make_shared<bool>(true)};
        std::shared_ptr<Operation> Active{};
        // Recorder failures can follow recorded commands. Retain their workspaces and
        // leases until the participant's device-idle shutdown.
        std::vector<std::shared_ptr<Operation>> Retired{};
        std::optional<ClusteringGpuResult> Completed{};
        explicit Impl(RHI::IDevice& d):Device(d){}
        ~Impl(){*Alive=false;if(Active)DiscardEditorPointScalar(Active->Commands,Active->Transaction);}
        void Finish(KMeansRunStatus status,std::string why)
        {
            auto a=std::move(Active);if(!a)return;
            a->Result.Status=status;a->Result.Message=std::move(why);
            a->Result.Error=status==KMeansRunStatus::Applied?Core::ErrorCode::Success:Core::ErrorCode::InvalidState;
            if(a->Recorded&&a->Page&&a->Page->State==SpatialQueryState::Failed)Retired.push_back(a);
            DiscardEditorPointScalar(a->Commands,a->Transaction);
            Completed=ClusteringGpuResult{.Published=std::move(a->Result)};
        }
        ClusteringGpuSubmission Start(KMeansSnapshot& snapshot,const EditorProcessingContext& context,
            std::function<bool()> current,std::function<EditorCommandStatus(const Geometry::KMeans::KMeansResult&)> publish)
        {
            if(Active||Completed)return {.Refused=true,.Diagnostic="A K-Means result awaits completion or Accept/Discard."};
            if(!Device.IsOperational()||!Device.SupportsShaderFloat64()||!context.SpatialIndices||!context.SpatialIndices->GpuQueriesAvailable()||
                !context.SpatialIndices->PropertyResidency())return {.Diagnostic="K-Means requires an operational float64 device and SpatialIndexCache residency."};
            if(snapshot.Command.Properties.OutputLabels.ValueKind!=Geometry::PropertyValueKind::UInt32)
                return {.Diagnostic="GPU K-Means requires UInt32 label storage; the CPU reference preserves the configured scalar type."};
            if(!Retired.empty())return {.Refused=true,.Diagnostic="A failed recording retains GPU resources until device shutdown."};
            if(snapshot.Points.empty()||snapshot.Points.size()>std::numeric_limits<std::uint32_t>::max()||
                snapshot.SlotCount>std::numeric_limits<std::uint32_t>::max())return {.Refused=true,.Diagnostic="K-Means row count is not representable by the device."};
            auto a=std::make_shared<Operation>();a->Context=context;a->Commands=BindEditorProcessingCommands(context);
            a->Current=std::move(current);a->Publish=std::move(publish);
            a->Count=std::uint32_t(snapshot.Points.size());a->Clusters=std::min(a->Count,snapshot.Params.ClusterCount);
            a->Limits.SubmissionPairs=std::max(1u,a->Limits.SubmissionPairs);
            a->Limits.PagePairs=std::clamp(a->Limits.PagePairs,1u,a->Limits.SubmissionPairs);
            auto& r=*context.SpatialIndices->PropertyResidency();
            const auto before=r.Stats();
            const auto entity=SelectionController::ToEntityHandle(snapshot.Command.StableEntityId);
            a->Input=ResolveGpuPropertyInput(r,*context.Scene,context.World,entity,snapshot.Command.Properties.InputPositions);
            if(!a->Input)return {.Refused=true,.Diagnostic="K-Means resident input is busy or unavailable."};
            auto seeds=Geometry::KMeans::BuildInitialCentroids(snapshot.Points,{},snapshot.Params,a->Clusters);
            a->Workspace=std::make_shared<KMeansGpuWorkspace>(Device);
            const bool identity=snapshot.SlotCount==a->Count;
            if(seeds.size()!=a->Clusters||!a->Workspace->Prepare(a->Count,seeds,identity?std::span<const std::uint32_t>{}:snapshot.Slots))
                return {.Diagnostic="K-Means GPU workspace or seed preparation failed."};
            a->Result.CpuStageUploadBytes=a->Workspace->CpuUploadBytes();
            auto typed=snapshot.Command.Properties.OutputLabels;typed.ValueKind=Geometry::PropertyValueKind::UInt32;
            // Existing integer bytes preserve deleted slots in every preview and Accept.
            if(!identity && snapshot.BeforeOutputs.Labels.Exists)
            {
                a->Base=ResolveGpuPropertyInput(r,*context.Scene,context.World,entity,typed);
                if(!a->Base)return {.Refused=true,.Diagnostic="Prior K-Means labels are busy or unavailable for deleted-slot preservation."};
            }
            a->Result.GpuInputUploadBytes=r.Stats().UploadBytes-before.UploadBytes;
            a->Result.GpuInputCacheHits=r.Stats().Hits-before.Hits;
            EditorPointScalarTransactionSnapshot admission;
            std::weak_ptr<Operation> weak=a;
            a->Transaction=BeginEditorPointScalarPublication(a->Commands,
                {.EntityId=snapshot.Command.StableEntityId,.Count=std::uint32_t(snapshot.SlotCount),.Output=typed,
                 .Label="Run K-Means clustering",.Current=a->Current,
                 .Publish=[weak](std::span<const std::byte> bytes){
                    auto op=weak.lock();if(!op)return EditorCommandStatus::StaleEntity;
                    op->Geometry.Labels.resize(op->Count);
                    for(std::uint32_t i=0;i<op->Count;++i){
                        std::uint32_t label{};std::memcpy(&label,bytes.data()+std::size_t(op->Snapshot.Slots[i])*4,4);
                        if(label>=op->Clusters)return EditorCommandStatus::GeometryProcessingFailed;
                        op->Geometry.Labels[i]=label;
                    }
                    return op->Publish(op->Geometry);
                 }},admission,[this,alive=Alive,weak](EditorPointScalarTransactionSnapshot result){
                    const auto op=weak.lock();if(!*alive||!op||Active!=op)return;
                    op->Result.CpuStageReadbackBytes+=result.CpuStageReadbackBytes;
                    Finish(result.Status==EditorCommandStatus::StaleEntity?KMeansRunStatus::StaleSource:
                        result.Phase==EditorGpuTransactionPhase::Discarded?KMeansRunStatus::Cancelled:
                        result.Status==EditorCommandStatus::Applied||result.Status==EditorCommandStatus::NoChange?
                        KMeansRunStatus::Applied:KMeansRunStatus::GeometryProcessingFailed,result.Message);
                 });
            if(!a->Transaction)return {.Refused=true,.Diagnostic=admission.Message};
            // The caller moves the full before-state into the publication callback
            // only after admission. Iteration retains just its live-slot map/config.
            a->Snapshot.Command=snapshot.Command;a->Snapshot.World=snapshot.World;
            a->Snapshot.Correlation=snapshot.Correlation;a->Snapshot.Slots=snapshot.Slots;
            a->Snapshot.SlotCount=snapshot.SlotCount;a->Snapshot.Params=snapshot.Params;
            auto& result=a->Result;const auto& command=a->Snapshot.Command;
            result.Correlation=a->Snapshot.Correlation;result.World=a->Snapshot.World;
            result.StableEntityId=command.StableEntityId;result.Properties=command.Properties;result.Parameters=command.Parameters;
            result.RequestedBackend=result.ActualBackend=ClusteringBackend::VulkanCompute;
            result.ImplementationId="vulkan_resident_paged_lloyd";
            result.BackendDiagnostic="Resident stride-12 positions; ordered paged Lloyd assignment/update; host convergence diagnostics.";
            result.LabelCount=a->Count;result.ClusterCount=a->Clusters;
            Active=std::move(a);return {.Accepted=true};
        }
        void Accept()
        {
            const auto a=Active;if(!a||!a->Ready||a->Accepting)return;
            const auto snapshot=SnapshotEditorPointScalar(a->Commands,a->Transaction);
            if(!snapshot.CanAccept){
                if(a->Snapshot.Command.AutoAccept)Finish(KMeansRunStatus::StaleSource,snapshot.AcceptRefusalReason);
                return;
            }
            a->Accepting=true;
            const auto result=AcceptEditorPointScalar(a->Commands,a->Transaction);
            if(Active==a&&result.Status!=EditorCommandStatus::Pending)
                Finish(result.Status==EditorCommandStatus::StaleEntity?KMeansRunStatus::StaleSource:KMeansRunStatus::GeometryProcessingFailed,result.Message);
        }
        KMeansGpuObservation Observe(CommandCorrelationId id,KMeansGpuAction action)
        {
            auto a=Active;if(!a||a->Snapshot.Correlation!=id)return {};
            if(action==KMeansGpuAction::Stop)a->Stop=true;
            if(action==KMeansGpuAction::Discard){
                if(a->Accepting)DiscardEditorPointScalar(a->Commands,a->Transaction);
                else {a->Discard=true;Advance();}
            }
            if(action==KMeansGpuAction::Accept)Accept();
            a=Active;if(!a)return {};
            const auto scalar=SnapshotEditorPointScalar(a->Commands,a->Transaction);
            return {.Running=!a->Ready,.ReadyToAccept=a->Ready,.Accepting=a->Accepting,
                .CanAccept=a->Ready&&!a->Accepting&&scalar.CanAccept,
                .Message=scalar.AcceptRefusalReason.empty()?(a->Ready?"GPU labels await Accept or Discard.":"GPU K-Means pages running."):scalar.AcceptRefusalReason,
                .Iterations=a->Result.Iterations,.Submissions=a->Result.GpuSubmissions,.Previews=a->Result.GpuPreviews,
                .InputUploadBytes=a->Result.GpuInputUploadBytes,.InputCacheHits=a->Result.GpuInputCacheHits,
                .CpuStageUploadBytes=a->Result.CpuStageUploadBytes,.CpuStageReadbackBytes=a->Result.CpuStageReadbackBytes};
        }
        RHI::BufferHandle Record(const std::shared_ptr<Operation>& a,RHI::ICommandContext& cmd)
        {
            a->Recorded=true;
            if(a->CentroidReadback)return a->Workspace->Centroids();
            auto& residency=*a->Context.SpatialIndices->PropertyResidency();
            residency.NoteUse(a->Input->Buffer,Device.GetGlobalFrameNumber());
            if(a->Phase==KMeansGpuPhase::Preview&&a->Row==0){
                const auto shader=RHI::MemoryAccess::ShaderRead|RHI::MemoryAccess::ShaderWrite;
                cmd.BufferBarrier(a->Back->Typed.Buffer,shader|RHI::MemoryAccess::TransferRead,RHI::MemoryAccess::TransferWrite);
                if(a->Snapshot.SlotCount!=a->Count){
                    if(a->Base){cmd.BufferBarrier(a->Base->Buffer,shader|RHI::MemoryAccess::TransferWrite,RHI::MemoryAccess::TransferRead);
                        residency.NoteUse(a->Base->Buffer,Device.GetGlobalFrameNumber());
                        cmd.CopyBuffer(a->Base->Buffer,a->Back->Typed.Buffer,0,0,a->Base->Bytes);}
                    else cmd.FillBuffer(a->Back->Typed.Buffer,0,a->Back->Typed.Bytes,0);
                }
                cmd.BufferBarrier(a->Back->Typed.Buffer,RHI::MemoryAccess::TransferWrite,shader);
            }
            const auto outer=a->Phase==KMeansGpuPhase::Update?a->Clusters:
                a->Phase==KMeansGpuPhase::Presentation?std::uint32_t(a->Snapshot.SlotCount):a->Count;
            const auto inner=a->Phase==KMeansGpuPhase::Assign?a->Clusters:a->Phase==KMeansGpuPhase::Update?a->Count:1u;
            std::uint64_t budget=a->Limits.SubmissionPairs;
            std::uint32_t depth=KMeansSubmissionSerialDepth;
            RHI::BufferHandle result{};
            while(a->Row<outer){
                const auto scan=std::min({inner,a->Limits.PagePairs,KMeansPageScanRows});
                const auto rows=std::min({outer-a->Row,std::max(1u,a->Limits.PagePairs/scan),
                    a->Phase==KMeansGpuPhase::Update?65535u:65535u*64u,
                    a->Phase==KMeansGpuPhase::Reduce?KMeansPageScanRows:std::numeric_limits<std::uint32_t>::max()});
                if(budget<rows)break;
                const auto columns=std::uint32_t(std::min<std::uint64_t>(std::min(inner-a->Inner,scan),std::min<std::uint64_t>(a->Limits.PagePairs,budget)/rows));
                // Conservatively sum the longest lane path of every dispatch,
                // even though workgroups can overlap on the device.
                const auto serial=a->Phase==KMeansGpuPhase::Assign?columns+1:
                    a->Phase==KMeansGpuPhase::Update?(columns+63)/64+7:
                    a->Phase==KMeansGpuPhase::Reduce?(rows+63)/64+7:1u;
                if(serial>depth)break;
                depth-=serial;
                result=a->Workspace->Record(cmd,*a->Input,{a->Phase,a->Row,rows,a->Inner,columns},
                    a->Back?a->Back->Typed:Graphics::GpuPropertyView{},a->Back?a->Back->Presentation:Graphics::GpuPropertyView{});
                budget-=std::uint64_t(rows)*columns;a->Inner+=columns;
                if(a->Inner==inner){a->Inner=0;a->Row+=rows;}
            }
            a->Boundary=a->Row==outer;
            if(a->Back){residency.NoteUse(a->Back->Typed.Buffer,Device.GetGlobalFrameNumber());
                residency.NoteUse(a->Back->Presentation.Buffer,Device.GetGlobalFrameNumber());}
            return result;
        }
        void Advance()
        {
            const auto a=Active;if(!a)return;
            if(a->Page&&a->Page->State!=SpatialQueryState::Ready&&a->Page->State!=SpatialQueryState::Failed)return;
            if(a->Discard|| (a->Context.AttachmentActive&&!a->Context.AttachmentActive())){
                Finish(KMeansRunStatus::Cancelled,"K-Means discarded; CPU outputs retained.");return;}
            if(a->Ready){
                if(a->Snapshot.Command.AutoAccept&&!a->Accepting)Accept();
                return; // Manual stale results remain inspectable with Accept disabled.
            }
            if(!a->Current()){Finish(KMeansRunStatus::StaleSource,"K-Means source or output changed.");return;}
            if(a->Page){
                if(a->Page->State==SpatialQueryState::Failed){Finish(KMeansRunStatus::GeometryProcessingFailed,a->Page->Diagnostic);return;}
                a->Result.CpuStageReadbackBytes+=a->Page->Data.size();
                if(a->CentroidReadback){
                    if(a->Page->Data.size()!=std::size_t(a->Clusters)*12){Finish(KMeansRunStatus::GeometryProcessingFailed,"K-Means centroid readback size mismatch.");return;}
                    a->Geometry.Centroids.resize(a->Clusters);std::memcpy(a->Geometry.Centroids.data(),a->Page->Data.data(),a->Page->Data.size());
                    for(const auto& centroid:a->Geometry.Centroids)
                        if(!std::isfinite(centroid.x)||!std::isfinite(centroid.y)||!std::isfinite(centroid.z)){
                            Finish(KMeansRunStatus::GeometryProcessingFailed,"K-Means centroid readback contains non-finite values.");return;
                        }
                    a->Result.Centroids=a->Geometry.Centroids;a->Ready=true;a->Page.reset();
                    if(a->Snapshot.Command.AutoAccept)Accept();return;
                }
                if(a->Boundary){
                    a->Row=a->Inner=0;a->Boundary=false;
                    switch(a->Phase){
                    case KMeansGpuPhase::Initialize:a->Phase=KMeansGpuPhase::Assign;break;
                    case KMeansGpuPhase::Assign:a->Phase=KMeansGpuPhase::Reduce;break;
                    case KMeansGpuPhase::Reduce:a->Phase=KMeansGpuPhase::Update;break;
                    case KMeansGpuPhase::Update:{
                        if(a->Page->Data.size()!=sizeof(KMeansGpuDiagnostics)){Finish(KMeansRunStatus::GeometryProcessingFailed,"K-Means diagnostic readback size mismatch.");return;}
                        KMeansGpuDiagnostics d;std::memcpy(&d,a->Page->Data.data(),sizeof(d));
                        if(d.Invalid||!std::isfinite(d.Inertia)||!std::isfinite(d.MaxShift)){Finish(KMeansRunStatus::GeometryProcessingFailed,"K-Means device numerical failure.");return;}
                        ++a->Result.Iterations;a->Result.Inertia=d.Inertia;a->Result.MaxDistanceIndex=d.MaxDistanceIndex;
                        a->Result.Converged=!d.Changed||d.MaxShift<=a->Snapshot.Params.ConvergenceTolerance*a->Snapshot.Params.ConvergenceTolerance;
                        a->Geometry.Iterations=a->Result.Iterations;a->Geometry.Converged=a->Result.Converged;
                        a->Geometry.Inertia=d.Inertia;a->Geometry.MaxDistanceIndex=d.MaxDistanceIndex;
                        a->Terminal=a->Stop||a->Result.Converged||a->Result.Iterations==a->Snapshot.Params.MaxIterations;
                        a->Phase=a->Terminal||a->Result.Iterations%a->Snapshot.Command.Parameters.GpuPreviewInterval==0?
                            KMeansGpuPhase::Preview:KMeansGpuPhase::Assign;break;}
                    case KMeansGpuPhase::Preview:a->Phase=KMeansGpuPhase::Presentation;break;
                    case KMeansGpuPhase::Presentation:
                        a->Back.reset();
                        if(!PublishEditorPointScalarBack(a->Transaction,a->Terminal)){Finish(KMeansRunStatus::GeometryProcessingFailed,"K-Means preview publication refused.");return;}
                        ++a->Result.GpuPreviews;
                        if(a->Terminal)a->CentroidReadback=true;
                        else a->Phase=KMeansGpuPhase::Assign;
                        break;
                    }
                }
                a->Page.reset();
            }
            if(a->Phase==KMeansGpuPhase::Preview&&!a->CentroidReadback&&!a->Back){
                a->Back=AcquireEditorPointScalarBack(a->Transaction);
                if(!a->Back){
                    if(!a->Terminal)a->Phase=KMeansGpuPhase::Assign;
                    else if(++a->PreviewDeferrals>=600)Finish(KMeansRunStatus::GeometryProcessingFailed,"K-Means terminal preview residency remained busy for 600 retries.");
                    return;
                }
            }
            a->PreviewDeferrals=0;
            // Every update submission returns only the small diagnostics. Other
            // intermediate submissions use completion-only immediate execution.
            const auto bytes=a->CentroidReadback?std::uint64_t(a->Clusters)*12:
                a->Phase==KMeansGpuPhase::Update?sizeof(KMeansGpuDiagnostics):0;
            a->Recorded=false;
            a->Page=a->Context.SpatialIndices->QueueGpuCompute(bytes,
                [this,alive=Alive,a](RHI::ICommandContext& cmd,const SpatialGpuIndexView&){return *alive?Record(a,cmd):RHI::BufferHandle{};},SpatialGpuLatency::Immediate);
            if(!a->Page){Finish(KMeansRunStatus::GeometryProcessingFailed,"K-Means page submission rejected.");return;}
            ++a->Result.GpuSubmissions;
        }
    };
    ClusteringGpuState::ClusteringGpuState(RHI::IDevice& d,RHI::BufferManager&,RHI::ITransferQueue&):m_Impl(std::make_unique<Impl>(d)){}
    ClusteringGpuState::~ClusteringGpuState()=default;
    ClusteringGpuSubmission ClusteringGpuState::Start(KMeansSnapshot& s,const EditorProcessingContext& c,
        std::function<bool()> current,std::function<EditorCommandStatus(const Geometry::KMeans::KMeansResult&)> publish)
    {return m_Impl->Start(s,c,std::move(current),std::move(publish));}
    KMeansGpuObservation ClusteringGpuState::GpuRun(CommandCorrelationId id,KMeansGpuAction action){return m_Impl->Observe(id,action);}
    void ClusteringGpuState::RecordFrameCommands(RHI::ICommandContext&){}
    void ClusteringGpuState::DrainCompletedTransfers(){m_Impl->Advance();}
    std::optional<ClusteringGpuResult> ClusteringGpuState::ConsumeCompleted(){return std::exchange(m_Impl->Completed,std::nullopt);}
    bool ClusteringGpuState::HasInFlightWork()const noexcept{return bool(m_Impl->Active)||bool(m_Impl->Completed)||!m_Impl->Retired.empty();}
}

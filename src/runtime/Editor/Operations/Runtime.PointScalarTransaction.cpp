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
module Extrinsic.Runtime.PointScalarTransaction;
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
import Extrinsic.Graphics.PointScalarAnalysis;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.CommandContext;
#include "Editor/internal/Runtime.EditorProcessingAccess.hpp"
#include "Editor/internal/Runtime.EditorGeometryHelpers.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.PointFields.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.JobFailure.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.RadiusRows.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.GpuFront.hpp"

#include "Editor/Operations/Runtime.GeometryProcessingOperations.GpuScalar.hpp"
namespace Extrinsic::Runtime
{
    namespace GP=GeometryProcessingDetail;
    struct EditorPointScalarTransaction
    {
        EditorProcessingContext Context{};
        std::optional<EditorPointScalarPublication> Publication{};
        Graphics::GpuPropertyKey PresentationKey{}, CompanionKey{};
        std::uint64_t CompanionGeneration{};
        std::optional<Graphics::GpuPropertyView> CompanionBack{};
        std::shared_ptr<GP::GpuFrontReadback> CompanionReadback{};
        std::uint64_t PresentationGeneration{};
        std::shared_ptr<GP::PointScalarCapture> Capture{};
        entt::entity Entity{};
        GeometryPropertyRef Positions{};
        Graphics::PointScalarGpuParams Params{};
        std::string Label{};
        EditorJobIdentity Identity{};
        Graphics::GpuPropertyResidency* Residency{};
        Graphics::GpuPropertyKey Key{};
        std::uint64_t Generation{};
        std::optional<Graphics::GpuPropertyView> Input{},Base{},Back{};
        std::shared_ptr<GP::GpuFrontReadback> Readback{};
        // Leased from the spatial cache; the recorder closure keeps it while its work may run.
        std::shared_ptr<Graphics::PointScalarWorkspace> Workspace{};
        SpatialIndexHandle Index{};
        std::shared_ptr<SpatialGpuResult> Gpu{};
        EditorPointScalarTransactionSnapshot Result{};
        std::uint32_t Deferrals{};
        bool Automatic{},Abandoned{},Delivered{},TestFront{},Publishing{};
        std::function<void(EditorPointScalarTransactionSnapshot)> Sink{};
    };
    namespace
    {
        using Run=EditorPointScalarTransactionHandle;
        bool Current(const Run& w)
        { return !w->Abandoned && GP::EditorProcessingContextWorldCurrent(w->Context) && (!w->Publication || w->Publication->Current()) && (w->Publication || GP::PointScalarFieldCurrent(w->Context,w->Entity,*w->Capture)) && (!w->Generation||w->Residency->RingGeneration(w->Key)==w->Generation) &&
            (!w->CompanionGeneration||w->Residency->RingGeneration(w->CompanionKey)==w->CompanionGeneration) &&
            (!w->PresentationGeneration||w->Residency->RingGeneration(w->PresentationKey)==w->PresentationGeneration); }
        void Release(const Run& w)
        {
            w->Input.reset();w->Base.reset();w->Back.reset();w->CompanionBack.reset();
            // Only completed (or never queued) work returns the workspace early.
            if(!w->Gpu||w->Gpu->State==SpatialQueryState::Ready)w->Workspace.reset();
            if(w->CompanionReadback){w->CompanionReadback->Abandoned=true;w->CompanionReadback->Lease.reset();}
            if(w->Readback){w->Readback->Abandoned=true;w->Readback->Lease.reset();}
            if(w->Residency){
                (void)w->Residency->Discard(w->Key,w->Generation);
                if(w->CompanionGeneration)(void)w->Residency->Discard(w->CompanionKey,w->CompanionGeneration);
                if(w->PresentationGeneration)(void)w->Residency->Discard(w->PresentationKey,w->PresentationGeneration);
            }
        }
        void Deliver(const Run& w)
        { if(!std::exchange(w->Delivered,true)&&w->Sink)w->Sink(w->Result); }
        void Finish(const Run& w,EditorGpuTransactionPhase phase,EditorCommandStatus status,std::string why)
        { Release(w);w->Result.Phase=phase;w->Result.Status=status;w->Result.Message=std::move(why);Deliver(w); }
        void Fail(const Run& w,std::string why)
        { Finish(w,EditorGpuTransactionPhase::Failed,EditorCommandStatus::GeometryProcessingFailed,std::move(why)); }
        bool Acquire(const Run& w)
        {
            auto& r=*w->Residency;
            const auto resolve=[&](const GeometryPropertyRef& ref){
                const auto before=r.Stats().UploadBytes;
                auto v=ResolveGpuPropertyInput(r,*w->Context.Scene,w->Context.World,w->Entity,ref);
                const auto bytes=r.Stats().UploadBytes-before;w->Result.GpuInputUploadBytes+=bytes;
                if(v&&!bytes)++w->Result.GpuInputCacheHits;return v;};
            if(!w->Input)w->Input=resolve(w->Positions);
            if(!w->Input)return false;
            if(w->Capture->OutputWatch.Revision&&!w->Base)w->Base=resolve(w->Capture->Output);
            if(w->Capture->OutputWatch.Revision&&!w->Base)return false;
            if(!w->Back){
                if(!w->Generation&&r.HasRing(w->Key))return false;
                w->Back=AcquireGpuPropertyOutput(r,w->Context.World,w->Entity,w->Capture->Output,std::uint32_t(w->Capture->SlotCount),3);
                if(!w->Back)return false;w->Generation=r.RingGeneration(w->Key);}
            return true;
        }
        bool Poll(const Run& w)
        {
            if(!Current(w))return true;
            if(!w->Gpu){
                if(!Acquire(w)){if(++w->Deferrals<600)return false;Fail(w,"Scalar residency refused an input or output slot.");return true;}
                w->Workspace=w->Context.SpatialIndices->LeaseGpuWorkspace<Graphics::PointScalarWorkspace>();
                if(!w->Workspace){Fail(w,"Scalar device workspace unavailable.");return true;}
                w->Gpu=w->Context.SpatialIndices->QueueGpuCompute(w->Index,sizeof(Graphics::PointScalarGpuStats),
                    [w](RHI::ICommandContext& cmd,const SpatialGpuIndexView& index)->RHI::BufferHandle{
                        if(!Current(w)||!w->Input||!w->Back||!w->Workspace)return {};
                        const auto frame=w->Context.Device->GetGlobalFrameNumber();
                        w->Residency->NoteUse(w->Input->Buffer,frame);w->Residency->NoteUse(w->Back->Buffer,frame);
                        if(w->Base)w->Residency->NoteUse(w->Base->Buffer,frame);
                        return w->Workspace->Record(cmd,w->Params,{.Positions=*w->Input,.Output=*w->Back,
                            .Base=w->Base.value_or(Graphics::GpuPropertyView{}),.Nodes=index.NodesBDA,
                            .LiveSlots=index.OriginalSlotsBDA,.LiveCount=index.Count});},SpatialGpuLatency::Immediate);
                if(!w->Gpu){Fail(w,"Scalar compute submission rejected.");return true;}
                w->Result.GpuQueryBatches=1;}
            return w->Gpu->State==SpatialQueryState::Ready||w->Gpu->State==SpatialQueryState::Failed;
        }
        void CompleteAccept(const Run& w)
        {
            if (w->Publication)
            {
                const auto& readback = w->Readback;
                const auto& output = w->Publication->Output;
                const auto layout = MakeGpuPropertyLayout(output.ValueKind, w->Publication->Count);
                if (!readback || readback->Failed || !layout || readback->Bytes.size() != layout->Bytes())
                { Fail(w, "Typed scalar front readback failed."); return; }
                if (w->Publication->Companion && (!w->CompanionReadback || w->CompanionReadback->Failed ||
                    w->CompanionReadback->Bytes.size() != MakeGpuPropertyLayout(w->Publication->Companion->ValueKind, w->Publication->Count)->Bytes()))
                { Fail(w, "Companion scalar front readback failed."); return; }
                const auto publication = readback->Lease ? readback->Lease->Publication : 0;
                w->Result.CpuStageReadbackBytes += readback->Bytes.size() + (w->CompanionReadback ? w->CompanionReadback->Bytes.size() : 0);
                // The publication callback may run history observers synchronously.
                // Discard while Accept is publishing is ignored below.
                w->Publishing = true;
                const auto& companion = w->CompanionReadback;
                const auto status = w->Publication->Companion
                    ? w->Publication->PublishPair(readback->Bytes, companion->Bytes)
                    : w->Publication->Publish(readback->Bytes);
                w->Publishing = false;
                readback->Lease.reset();
                if (status != EditorCommandStatus::Applied && status != EditorCommandStatus::NoChange)
                { Finish(w, EditorGpuTransactionPhase::Failed, status, "Typed scalar publication rejected."); return; }
                if (w->Publication->Companion) {
                    const auto companionPublication = companion->Lease ? companion->Lease->Publication : 0;
                    companion->Lease.reset();
                    const auto a = BuildGeometryAvailability(w->Context.Scene->Raw(), w->Entity);
                    const auto watch = GP::ObserveGeometryProperty(a, w->Publication->Companion->Domain, w->Publication->Companion->Name);
                    const auto resolution = ResolveGeometryProperty(a, *w->Publication->Companion);
                    if (!resolution.Resolved() || resolution.ResolvedValueKind != w->Publication->Companion->ValueKind ||
                        !watch.Revision || !w->Residency->BindRevision(w->CompanionKey, *watch.Revision, companionPublication))
                        (void)w->Residency->Discard(w->CompanionKey, w->CompanionGeneration);
                }
                const auto availability = BuildGeometryAvailability(w->Context.Scene->Raw(), w->Entity);
                const auto watch = GP::ObserveGeometryProperty(availability, output.Domain, output.Name);
                const auto resolution = ResolveGeometryProperty(availability, output);
                if (!resolution.Resolved() || resolution.ResolvedValueKind != output.ValueKind || !watch.Revision || !w->Residency->BindRevision(w->Key, *watch.Revision, publication))
                    (void)w->Residency->Discard(w->Key, w->Generation);
                if (w->PresentationGeneration)
                {
                    if (!watch.Revision || !w->Residency->BindRevision(w->PresentationKey, *watch.Revision))
                        (void)w->Residency->Discard(w->PresentationKey, w->PresentationGeneration);
                }
                w->Result.Phase = EditorGpuTransactionPhase::Applied;
                w->Result.Status = status;
                w->Result.Message = "Typed scalar result accepted.";
                Deliver(w);
                return;
            }
            auto& capture=*w->Capture;std::uint64_t publication{};
            if(!w->TestFront){
                const auto& r=w->Readback;
                if(!r||r->Failed||r->Bytes.size()!=capture.SlotCount*4){Fail(w,"Scalar front readback failed.");return;}
                publication=r->Lease?r->Lease->Publication:0;
                std::memcpy(capture.AfterValues.data(),r->Bytes.data(),r->Bytes.size());
                w->Result.CpuStageReadbackBytes+=r->Bytes.size();r->Lease.reset();}
            else if(auto front=w->Residency->Front(w->Key))publication=front->Publication;
            w->Publishing=true;
            const auto status=GP::PublishPointScalarField(w->Context,w->Entity,capture,w->Label);
            w->Publishing=false;
            w->Result.Status=EditorFeatureDetail::ToEditorCommandStatus(status);
            if(w->Result.Status!=EditorCommandStatus::Applied){Finish(w,EditorGpuTransactionPhase::Failed,w->Result.Status,"Scalar publication rejected.");return;}
            const auto a=BuildGeometryAvailability(w->Context.Scene->Raw(),w->Entity);
            const auto watch=GP::ObserveGeometryProperty(a,capture.Output.Domain,capture.Output.Name);
            if(!watch.Revision||!w->Residency->BindRevision(w->Key,*watch.Revision,publication))
                (void)w->Residency->Discard(w->Key,w->Generation);
            w->Result.Phase=EditorGpuTransactionPhase::Applied;
            w->Result.Message="Device scalar accepted; input upload: "+std::to_string(w->Result.GpuInputUploadBytes)+
                " bytes; residency hits: "+std::to_string(w->Result.GpuInputCacheHits)+"; CPU-stage readback: "+std::to_string(w->Result.CpuStageReadbackBytes)+" bytes.";
            Deliver(w);
        }
        EditorPointScalarTransactionSnapshot Accept(const Run& w, std::function<void(EditorPointScalarTransactionSnapshot)> sink = {})
        {
            auto result=w->Result;
            if(result.Phase!=EditorGpuTransactionPhase::ReadyToAccept){result.Status=EditorCommandStatus::InvalidProcessingParameters;result.Message="No scalar result awaits Accept.";return result;}
            if(!Current(w)){result.Status=EditorCommandStatus::StaleEntity;result.Message="Scalar input or output changed; discard and run again.";return result;}
            if(sink)w->Sink=GuardEditorProcessingResult(w->Context,std::move(sink));
            if(!w->TestFront){w->Readback=std::make_shared<GP::GpuFrontReadback>();
                if(!GP::BeginGpuFrontReadback(w->Context,*w->Residency,w->Key,w->Readback)){Fail(w,"Scalar front is no longer resident.");return w->Result;}}
            if(!w->TestFront && w->Publication && w->Publication->Companion) {
                w->CompanionReadback=std::make_shared<GP::GpuFrontReadback>();
                if(!GP::BeginGpuFrontReadback(w->Context,*w->Residency,w->CompanionKey,w->CompanionReadback))
                {Fail(w,"Companion front is no longer resident.");return w->Result;}
            }
            w->Result.Phase=EditorGpuTransactionPhase::Accepting;
            JobDesc job{.DebugName="Accept point scalar",.Scope=w->Context.World,.Kind=RuntimeTaskKinds::GeometryProcess,
                .Work=[](const JobCancellation&){return JobResultEnvelope::Make(true);},
                .IsReadyToApply=[w]{return !Current(w)||w->TestFront||(GP::PollGpuFrontReadback(*w->Readback) && (!w->CompanionReadback || GP::PollGpuFrontReadback(*w->CompanionReadback)));},
                .ValidateBeforeApply=[w]{return Current(w)?JobApplyValidation::Current:JobApplyValidation::StaleGeneration;},
                .PublishCompletion=[w](KernelEventBus&,const JobResultEnvelope&){CompleteAccept(w);return w->Result.Phase==EditorGpuTransactionPhase::Applied;},
                .FinalizeUnpublishedOnMainThread=[w]{if(!w->Delivered)Finish(w,EditorGpuTransactionPhase::Discarded,EditorCommandStatus::StaleEntity,"Scalar Accept cancelled or stale.");}};
            if(!w->Context.JobCommands.Submit(std::move(job),w->Identity).IsValid())Fail(w,"Scalar Accept submission rejected.");
            return w->Result;
        }
    }
    extern "C++" { namespace GeometryProcessingDetail
    {
        bool AdmitPointScalarGpu(const EditorProcessingContext& ctx,const PointScalarCapture& c,const Graphics::PointScalarGpuParams& p,std::string& why)
        {
            const auto fail=[&](const char* message){why=message;return false;};
            if(c.Output.ValueKind!=Geometry::PropertyValueKind::Float)return fail("Vulkan scalar analysis supports float outputs only; select the CPU backend for other scalar storage.");
            if(c.HasSubnormalCoordinates)return fail("Vulkan scalar analysis requires normal or zero coordinates; subnormal components are unsupported.");
            const auto width=Graphics::PointScalarNeighborWidth(p,std::uint32_t(c.LiveCount));
            if(!c.ValidLbvh||!c.LiveCount||c.LiveCount>(1u<<20)||c.SlotCount>(1u<<24)||!width||width>4096||std::uint64_t(width)*c.LiveCount>(1u<<24))
                return fail("Vulkan scalar analysis exceeds LBVH coordinate, row or neighbor workspace limits.");
            if((p.Method<2&&p.K>63)||std::fpclassify(p.Bandwidth)==FP_SUBNORMAL||std::fpclassify(p.Scale)==FP_SUBNORMAL)
                return fail("Vulkan scalar analysis requires k<=63 and normal or zero parameters.");
            if(p.Method==2&&(!std::isfinite(p.QueryRadius)||p.QueryRadius>1e18f))
                return fail("Vulkan scalar query radius must be finite and within 1e18.");
            if(!ctx.Device||!ctx.Device->IsOperational()||!ctx.Device->SupportsShaderFloat64()||!ctx.JobCommands.Available()||!ctx.SpatialIndices||!ctx.SpatialIndices->GpuQueriesAvailable())
                return fail("Vulkan scalar analysis requires an operational float64 device and framed spatial cache/jobs.");
            return true;
        }
        EditorPointScalarTransactionHandle StartPointScalarGpu(const EditorProcessingContext& ctx,std::shared_ptr<PointScalarCapture> capture,
            entt::entity entity,std::uint32_t stableId,GeometryPropertyRef positions,const Graphics::PointScalarGpuParams& params,std::string label,
            std::string_view jobLabel,EditorPointScalarTransactionSnapshot& result,std::function<void(EditorPointScalarTransactionSnapshot)> sink,bool automatic,Graphics::GpuPropertyResidency* testResidency, const EditorPointScalarTransactionSnapshot& testResult)
        {
            auto* residency=testResidency?testResidency:(ctx.SpatialIndices?ctx.SpatialIndices->PropertyResidency():nullptr);
            const auto refuse=[&](std::string why)->EditorPointScalarTransactionHandle {result.Status=EditorCommandStatus::InvalidProcessingParameters;result.Message=std::move(why);return {};};
            if(!residency)return refuse("Scalar analysis needs GPU property residency.");
            auto w=std::make_shared<EditorPointScalarTransaction>();w->Context=ctx;w->Capture=std::move(capture);w->Entity=entity;
            w->Positions=std::move(positions);w->Params=params;w->Label=std::move(label);w->Residency=residency;w->Automatic=automatic;
            w->Sink=GuardEditorProcessingResult(ctx,std::move(sink));w->Key=MakeGpuPropertyKey(ctx.World,entity,w->Capture->Output);
            w->Identity={.EntityId=stableId,.Scope=ToEditorJobScope(w->Capture->Output.Domain),.OutputSemantic=GeometryPresentationSlotSemantic::ScalarField,.OutputName=w->Capture->Output.Name};
            if(residency->HasRing(w->Key))return refuse("Scalar output awaits Accept or Discard.");
            w->Result.LiveCount=w->Capture->LiveCount;
            if(testResidency){w->Result=testResult;w->Result.LiveCount=w->Capture->LiveCount;w->Back=AcquireGpuPropertyOutput(*residency,ctx.World,entity,w->Capture->Output,std::uint32_t(w->Capture->SlotCount),3);
                if(!w->Back)return refuse("Test scalar ring allocation failed.");w->Generation=residency->RingGeneration(w->Key);w->Back.reset();
                if(!residency->Publish(w->Key))return refuse("Test scalar publication failed.");w->TestFront=true;w->Result.Phase=EditorGpuTransactionPhase::ReadyToAccept;result=w->Result;return w;}
            if(auto busy=MeshSupport::ActiveOutputJobRefusal(ctx,w->Identity,jobLabel))
            {result.Status=EditorCommandStatus::Pending;result.Message=std::move(busy->Message);return {};}
            std::shared_ptr<const SpatialIndexSnapshot> snapshot;bool reused{};std::string why;
            if(AcquirePointIndex(*ctx.SpatialIndices,ctx.World,entity,w->Positions,w->Capture->Slots,w->Capture->Points,w->Index,snapshot,reused,why)!=PointIndexState::Ready)return refuse(why);
            w->Result.IndexReused=reused;w->Result.Message="Device scalar analysis queued.";
            JobDesc job{.DebugName="Device point scalar",.Scope=ctx.World,.Kind=RuntimeTaskKinds::GeometryProcess,
                .Work=[](const JobCancellation&){return JobResultEnvelope::Make(true);},.IsReadyToApply=[w]{return Poll(w);},
                .ValidateBeforeApply=[w]{return Current(w)?JobApplyValidation::Current:JobApplyValidation::StaleGeneration;},
                .PublishCompletion=[w](KernelEventBus&,const JobResultEnvelope&){
                    if(w->Delivered)return false;
                    if(!w->Gpu||w->Gpu->State!=SpatialQueryState::Ready||w->Gpu->Data.size()!=sizeof(Graphics::PointScalarGpuStats)){
                        Fail(w,w->Gpu?w->Gpu->Diagnostic:"Scalar compute failed.");return false;}
                    std::memcpy(&w->Result.Statistics,w->Gpu->Data.data(),sizeof(Graphics::PointScalarGpuStats));
                    w->Workspace.reset();
                    if(w->Result.Statistics.Invalid){Fail(w,"Scalar numerical failure or radius candidate overflow; increase capacity or select CPU.");return false;}
                    w->Back.reset();if(!w->Residency->Publish(w->Key, std::array<float, 2>{
                        float(w->Result.Statistics.Minimum), float(w->Result.Statistics.Maximum)})){Fail(w,"Scalar ring publication failed.");return false;}
                    w->Input.reset();w->Base.reset();w->Result.Message="GPU scalar awaits Accept or Discard.";w->Result.Phase=EditorGpuTransactionPhase::ReadyToAccept;
                    if(w->Automatic){const auto accepted=Accept(w);if(accepted.Status!=EditorCommandStatus::Pending)Finish(w,EditorGpuTransactionPhase::Failed,accepted.Status,accepted.Message);}
                    return !w->Delivered;},
                .FinalizeUnpublishedOnMainThread=[w]{if(!w->Delivered)Finish(w,EditorGpuTransactionPhase::Discarded,EditorCommandStatus::StaleEntity,"Scalar run cancelled or stale.");}};
            const auto token=ctx.JobCommands.Submit(std::move(job),w->Identity);
            if(!token.IsValid()){w->Sink={};Fail(w,"Scalar compute submission rejected.");result=w->Result;return {};}
            w->Identity.Run=token; // the Accept stage joins this run
            result=w->Result;return w;
        }
    }
    }
    extern "C++" void GeometryProcessingDetail::JoinPointScalarRun(
        const EditorPointScalarTransactionHandle& run, EditorJobIdentity identity)
    {
        if (run) run->Identity = std::move(identity);
    }
    EditorPointScalarTransactionHandle BeginEditorPointScalarPublication(
        const EditorProcessingCommands& commands, EditorPointScalarPublication publication,
        EditorPointScalarTransactionSnapshot& result,
        std::function<void(EditorPointScalarTransactionSnapshot)> sink)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        auto* residency = context.SpatialIndices ? context.SpatialIndices->PropertyResidency() : nullptr;
        const auto refuse = [&](const char* why) -> EditorPointScalarTransactionHandle {
            result = {.Phase=EditorGpuTransactionPhase::Failed,
                .Status=EditorCommandStatus::InvalidProcessingParameters, .Message=why}; return {};
        };
        if (!residency || !publication.Current || (!publication.Companion && !publication.Publish) || (publication.Companion && !publication.PublishPair) || !publication.Count ||
            !context.JobCommands.Available() || GeometryPropertyComponentCount(publication.Output.ValueKind)!=1 ||
            !MakeGpuPropertyLayout(publication.Output.ValueKind, publication.Count))
            return refuse("Typed scalar publication requires residency, jobs and a valid output.");
        if (publication.Companion && (publication.Companion->Name == publication.Output.Name ||
            publication.Companion->Domain != publication.Output.Domain ||
            GeometryPropertyComponentCount(publication.Companion->ValueKind)!=1 ||
            !MakeGpuPropertyLayout(publication.Companion->ValueKind, publication.Count)))
            return refuse("Invalid companion scalar output.");
        auto w = std::make_shared<EditorPointScalarTransaction>();
        w->Context = context; w->Residency = residency;
        w->Entity = SelectionController::ToEntityHandle(publication.EntityId);
        w->Key = MakeGpuPropertyKey(context.World, w->Entity, publication.Output);
        w->PresentationKey = MakeGpuPropertyKey(context.World, w->Entity, GpuPropertyPresentationRef(publication.Output));
        if (residency->HasRing(w->Key) || residency->HasRing(w->PresentationKey))
            return refuse("Scalar output awaits Accept or Discard.");
        // Admission and rollback run on the runtime thread, before another owner
        // can acquire either key. The rings own their reserved slots.
        w->Back = AcquireGpuPropertyOutput(*residency, context.World, w->Entity, publication.Output, publication.Count, 3);
        w->Generation = residency->RingGeneration(w->Key);
        if (!w->Back) { Release(w); return refuse("Typed scalar output reservation failed."); }
        if (w->PresentationKey != w->Key) {
            const auto presentation = AcquireGpuPropertyOutput(*residency, context.World, w->Entity,
                GpuPropertyPresentationRef(publication.Output), publication.Count, 3);
            w->PresentationGeneration = residency->RingGeneration(w->PresentationKey);
            if (!presentation) { Release(w); return refuse("Scalar presentation reservation failed."); }
        }
        if (publication.Companion) {
            w->CompanionKey = MakeGpuPropertyKey(context.World, w->Entity, *publication.Companion);
            if (residency->HasRing(w->CompanionKey)) { Release(w); return refuse("Companion output awaits Accept or Discard."); }
            w->CompanionBack = AcquireGpuPropertyOutput(*residency, context.World, w->Entity, *publication.Companion, publication.Count, 3);
            w->CompanionGeneration = residency->RingGeneration(w->CompanionKey);
            if (!w->CompanionBack) { Release(w); return refuse("Companion output reservation failed."); }
        }
        w->Publication = std::move(publication);
        w->Sink = GuardEditorProcessingResult(context, std::move(sink));
        w->Result.LiveCount = w->Publication->Count;
        result = w->Result;
        return w;
    }
    std::optional<EditorPointScalarBack> AcquireEditorPointScalarBack(const EditorPointScalarTransactionHandle& w)
    {
        if (!w || !w->Publication || !Current(w)) return std::nullopt;
        const auto& p = *w->Publication;
        w->Back.reset(); // Drop the admission lease before selecting a preview back.
        auto typed = AcquireGpuPropertyOutput(*w->Residency, w->Context.World, w->Entity, p.Output, p.Count, 3);
        if (!typed) return std::nullopt;
        w->Generation = w->Residency->RingGeneration(w->Key);
        auto presentation = p.Output.ValueKind == Geometry::PropertyValueKind::Float ? typed :
            AcquireGpuPropertyOutput(*w->Residency, w->Context.World, w->Entity, GpuPropertyPresentationRef(p.Output), p.Count, 3);
        if (!presentation) return std::nullopt;
        if (p.Output.ValueKind != Geometry::PropertyValueKind::Float)
            w->PresentationGeneration = w->Residency->RingGeneration(w->PresentationKey);
        std::optional<Graphics::GpuPropertyView> companion;
        if (p.Companion) {
            w->CompanionBack.reset();
            companion = AcquireGpuPropertyOutput(*w->Residency, w->Context.World, w->Entity, *p.Companion, p.Count, 3);
            if (!companion) return std::nullopt;
        }
        return EditorPointScalarBack{*typed, *presentation, companion.value_or(Graphics::GpuPropertyView{})};
    }
    bool PublishEditorPointScalarBack(const EditorPointScalarTransactionHandle& w, bool ready,
        std::optional<std::array<float, 2>> scalarRange)
    {
        if (!w || !w->Publication || !Current(w) || !w->Residency->Publish(w->Key, scalarRange)) return false;
        if (w->PresentationGeneration && !w->Residency->Publish(w->PresentationKey, scalarRange)) return false;
        if (w->CompanionGeneration && !w->Residency->Publish(w->CompanionKey)) return false;
        if (ready) w->Result.Phase = EditorGpuTransactionPhase::ReadyToAccept;
        return true;
    }
    EditorPointScalarTransactionSnapshot SnapshotEditorPointScalar(const EditorProcessingCommands&,const EditorPointScalarTransactionHandle& w)
    {
        if(!w)return {.Phase=EditorGpuTransactionPhase::Failed,.Status=EditorCommandStatus::InvalidProcessingParameters,.Message="No scalar transaction."};
        auto s=w->Result;if(s.Phase==EditorGpuTransactionPhase::ReadyToAccept){s.CanAccept=Current(w);if(!s.CanAccept)s.AcceptRefusalReason="Scalar input or output changed; discard and run again.";}return s;
    }
    EditorPointScalarTransactionSnapshot AcceptEditorPointScalar(const EditorProcessingCommands&,const EditorPointScalarTransactionHandle& w,
        std::function<void(EditorPointScalarTransactionSnapshot)> sink)
    { return w?Accept(w,std::move(sink)):EditorPointScalarTransactionSnapshot{.Phase=EditorGpuTransactionPhase::Failed,.Status=EditorCommandStatus::InvalidProcessingParameters,.Message="No scalar transaction."}; }
    void DiscardEditorPointScalar(const EditorProcessingCommands&,const EditorPointScalarTransactionHandle& w)
    {
        if(!w||w->Publishing||w->Result.Phase==EditorGpuTransactionPhase::Applied||w->Result.Phase==EditorGpuTransactionPhase::Discarded||w->Result.Phase==EditorGpuTransactionPhase::Failed)return;
        w->Abandoned=true;Finish(w,EditorGpuTransactionPhase::Discarded,EditorCommandStatus::NoChange,"Scalar preview discarded; CPU fields retained.");
    }
}

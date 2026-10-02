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
#include "Editor/Operations/Runtime.GpuTransactionLifecycle.hpp"
namespace Extrinsic::Runtime
{
    namespace GP=GeometryProcessingDetail;
    // The typed scalar transaction on the shared Run/Accept lifecycle. Rings: the typed output
    // (read back on Accept), the companion field (publication mode, read back) and the float
    // presentation ring beside a non-float typed output (publication mode).
    struct EditorPointScalarTransaction : std::enable_shared_from_this<EditorPointScalarTransaction>
    {
        GP::GpuTransactionCore Core{};
        std::optional<EditorPointScalarPublication> Publication{};
        std::shared_ptr<GP::PointScalarCapture> Capture{};
        entt::entity Entity{};
        GeometryPropertyRef Positions{};
        Graphics::PointScalarGpuParams Params{};
        std::string Label{};
        std::optional<Graphics::GpuPropertyView> Input{},Base{};
        // Leased from the spatial cache; the recorder closure keeps it while its work may run.
        std::shared_ptr<Graphics::PointScalarWorkspace> Workspace{};
        SpatialIndexHandle Index{};
        EditorPointScalarTransactionSnapshot Result{};
        std::function<void(EditorPointScalarTransactionSnapshot)> Sink{};
    };
    namespace
    {
        using Run=EditorPointScalarTransactionHandle;
        constexpr std::size_t kTyped=0,kCompanion=1,kPresentation=2;
        auto& Typed(const Run& w){return w->Core.Rings[kTyped];}
        bool Current(const Run& w){return GP::GpuTransactionCurrent(w->Core);}
        void Fail(const Run& w,std::string why){GP::FailGpuTransaction(w->Core,std::move(why));}
        bool Acquire(const Run& w)
        {
            auto& r=*w->Core.Residency;const auto& ctx=w->Core.Context;
            const auto resolve=[&](const GeometryPropertyRef& ref){
                const auto before=r.Stats().UploadBytes;
                auto v=ResolveGpuPropertyInput(r,*ctx.Scene,ctx.World,w->Entity,ref);
                const auto bytes=r.Stats().UploadBytes-before;w->Result.GpuInputUploadBytes+=bytes;
                if(v&&!bytes)++w->Result.GpuInputCacheHits;return v;};
            if(!w->Input)w->Input=resolve(w->Positions);
            if(!w->Input)return false;
            if(w->Capture->OutputWatch.Revision&&!w->Base)w->Base=resolve(w->Capture->Output);
            if(w->Capture->OutputWatch.Revision&&!w->Base)return false;
            // A ring another run created defers like an exhausted one.
            return GP::AcquireGpuTransactionBack(w->Core,kTyped,w->Entity,w->Capture->Output,std::uint32_t(w->Capture->SlotCount),3)==GP::GpuRingAcquisition::Ready;
        }
        bool Poll(const Run& w)
        {
            auto& t=w->Core;
            if(!t.Gpu){
                if(!Acquire(w)){if(!GP::GpuTransactionDeferralsExhausted(t))return false;Fail(w,"Scalar residency refused an input or output slot.");return true;}
                w->Workspace=t.Context.SpatialIndices->LeaseGpuWorkspace<Graphics::PointScalarWorkspace>();
                if(!w->Workspace){Fail(w,"Scalar device workspace unavailable.");return true;}
                t.Gpu=t.Context.SpatialIndices->QueueGpuCompute(w->Index,sizeof(Graphics::PointScalarGpuStats),
                    // The recorder owns its workspace lease; the cache keeps it until the readback is safe.
                    [w,workspace=w->Workspace](RHI::ICommandContext& cmd,const SpatialGpuIndexView& index)->RHI::BufferHandle{
                        const auto& back=Typed(w).Back;
                        if(!Current(w)||!w->Input||!back)return {};
                        const auto frame=w->Core.Context.Device->GetGlobalFrameNumber();
                        auto& residency=*w->Core.Residency;
                        residency.NoteUse(w->Input->Buffer,frame);residency.NoteUse(back->Buffer,frame);
                        if(w->Base)residency.NoteUse(w->Base->Buffer,frame);
                        return workspace->Record(cmd,w->Params,{.Positions=*w->Input,.Output=*back,
                            .Base=w->Base.value_or(Graphics::GpuPropertyView{}),.Nodes=index.NodesBDA,
                            .LiveSlots=index.OriginalSlotsBDA,.LiveCount=index.Count});},SpatialGpuLatency::Immediate);
                w->Result.GpuQueryBatches=1;}
            return t.Gpu->State==SpatialQueryState::Ready||t.Gpu->State==SpatialQueryState::Failed;
        }
        void CompleteRun(const Run& w)
        {
            auto& t=w->Core;
            if(!t.Gpu||t.Gpu->State!=SpatialQueryState::Ready||t.Gpu->Data.size()!=sizeof(Graphics::PointScalarGpuStats)){
                Fail(w,t.Gpu?t.Gpu->Diagnostic:"Scalar compute failed.");return;}
            std::memcpy(&w->Result.Statistics,t.Gpu->Data.data(),sizeof(Graphics::PointScalarGpuStats));
            w->Workspace.reset();
            if(w->Result.Statistics.Invalid){Fail(w,"Scalar numerical failure or radius candidate overflow; increase capacity or select CPU.");return;}
            Typed(w).Back.reset();
            if(!t.Residency->Publish(Typed(w).Key,std::array<float,2>{float(w->Result.Statistics.Minimum),float(w->Result.Statistics.Maximum)})){
                Fail(w,"Scalar ring publication failed.");return;}
            w->Input.reset();w->Base.reset();w->Result.Message="GPU scalar awaits Accept or Discard.";
            GP::ReadyGpuTransaction(t);
        }
        void CompleteAccept(const Run& w)
        {
            auto& t=w->Core;auto& residency=*t.Residency;
            if (w->Publication)
            {
                const auto& readback = Typed(w).Readback;
                const auto& companion = t.Rings[kCompanion].Readback;
                const auto& output = w->Publication->Output;
                const auto layout = MakeGpuPropertyLayout(output.ValueKind, w->Publication->Count);
                if (!readback || readback->Failed || !layout || readback->Bytes.size() != layout->Bytes())
                { Fail(w, "Typed scalar front readback failed."); return; }
                if (w->Publication->Companion && (!companion || companion->Failed ||
                    companion->Bytes.size() != MakeGpuPropertyLayout(w->Publication->Companion->ValueKind, w->Publication->Count)->Bytes()))
                { Fail(w, "Companion scalar front readback failed."); return; }
                const auto publication = readback->Lease ? readback->Lease->Publication : 0;
                w->Result.CpuStageReadbackBytes += readback->Bytes.size() + (companion ? companion->Bytes.size() : 0);
                // The publication callback may run history observers synchronously (the
                // lifecycle ignores a Discard they issue).
                const auto status = w->Publication->Companion
                    ? w->Publication->PublishPair(readback->Bytes, companion->Bytes)
                    : w->Publication->Publish(readback->Bytes);
                readback->Lease.reset();
                if (status != EditorCommandStatus::Applied && status != EditorCommandStatus::NoChange)
                { GP::FinishGpuTransaction(t, EditorGpuTransactionPhase::Failed, status, "Typed scalar publication rejected."); return; }
                if (w->Publication->Companion) {
                    auto& ring = t.Rings[kCompanion];
                    const auto companionPublication = companion->Lease ? companion->Lease->Publication : 0;
                    companion->Lease.reset();
                    const auto a = BuildGeometryAvailability(t.Context.Scene->Raw(), w->Entity);
                    const auto watch = GP::ObserveGeometryProperty(a, w->Publication->Companion->Domain, w->Publication->Companion->Name);
                    const auto resolution = ResolveGeometryProperty(a, *w->Publication->Companion);
                    if (!resolution.Resolved() || resolution.ResolvedValueKind != w->Publication->Companion->ValueKind ||
                        !watch.Revision || !residency.BindRevision(ring.Key, *watch.Revision, companionPublication))
                        (void)residency.Discard(ring.Key, ring.Generation);
                }
                const auto availability = BuildGeometryAvailability(t.Context.Scene->Raw(), w->Entity);
                const auto watch = GP::ObserveGeometryProperty(availability, output.Domain, output.Name);
                const auto resolution = ResolveGeometryProperty(availability, output);
                if (!resolution.Resolved() || resolution.ResolvedValueKind != output.ValueKind || !watch.Revision || !residency.BindRevision(Typed(w).Key, *watch.Revision, publication))
                    (void)residency.Discard(Typed(w).Key, Typed(w).Generation);
                if (const auto& ring = t.Rings[kPresentation]; ring.Generation)
                {
                    if (!watch.Revision || !residency.BindRevision(ring.Key, *watch.Revision))
                        (void)residency.Discard(ring.Key, ring.Generation);
                }
                GP::FinishGpuTransaction(t, EditorGpuTransactionPhase::Applied, status, "Typed scalar result accepted.");
                return;
            }
            auto& capture=*w->Capture;std::uint64_t publication{};
            if(!t.TestFront){
                const auto& r=Typed(w).Readback;
                if(!r||r->Failed||r->Bytes.size()!=capture.SlotCount*4){Fail(w,"Scalar front readback failed.");return;}
                publication=r->Lease?r->Lease->Publication:0;
                std::memcpy(capture.AfterValues.data(),r->Bytes.data(),r->Bytes.size());
                w->Result.CpuStageReadbackBytes+=r->Bytes.size();r->Lease.reset();}
            else if(auto front=residency.Front(Typed(w).Key))publication=front->Publication;
            const auto status=EditorFeatureDetail::ToEditorCommandStatus(GP::PublishPointScalarField(t.Context,w->Entity,capture,w->Label));
            if(status!=EditorCommandStatus::Applied){GP::FinishGpuTransaction(t,EditorGpuTransactionPhase::Failed,status,"Scalar publication rejected.");return;}
            const auto a=BuildGeometryAvailability(t.Context.Scene->Raw(),w->Entity);
            const auto watch=GP::ObserveGeometryProperty(a,capture.Output.Domain,capture.Output.Name);
            if(!watch.Revision||!residency.BindRevision(Typed(w).Key,*watch.Revision,publication))
                (void)residency.Discard(Typed(w).Key,Typed(w).Generation);
            GP::FinishGpuTransaction(t,EditorGpuTransactionPhase::Applied,status,"Device scalar accepted; input upload: "+std::to_string(w->Result.GpuInputUploadBytes)+
                " bytes; residency hits: "+std::to_string(w->Result.GpuInputCacheHits)+"; CPU-stage readback: "+std::to_string(w->Result.CpuStageReadbackBytes)+" bytes.");
        }
        // Builds the transaction on the shared lifecycle; hooks reach it by raw pointer.
        Run Make(const EditorProcessingContext& ctx,Graphics::GpuPropertyResidency* residency)
        {
            auto w=std::make_shared<EditorPointScalarTransaction>();
            auto& t=w->Core;t.Context=ctx;t.Residency=residency;t.Label="Scalar analysis";t.AcceptJobName="Accept point scalar";
            auto* raw=w.get();
            // Hooks run while a job or caller owns the transaction; recorders take ownership.
            const auto self=[raw]{return raw->shared_from_this();};
            t.Hooks={
                .Current=[raw]{return (!raw->Publication||raw->Publication->Current())&&
                    (raw->Publication||GP::PointScalarFieldCurrent(raw->Core.Context,raw->Entity,*raw->Capture));},
                .Poll=[self]{return Poll(self());},
                .CompleteRun=[self]{CompleteRun(self());},
                .CompleteAccept=[self]{CompleteAccept(self());},
                .Release=[raw]{raw->Input.reset();raw->Base.reset();raw->Workspace.reset();},
                .Deliver=[raw](EditorCommandStatus status,std::string message){
                    raw->Result.Phase=raw->Core.Phase;raw->Result.Status=status;raw->Result.Message=std::move(message);
                    if(auto sink=std::move(raw->Sink))sink(raw->Result);}};
            return w;
        }
        EditorPointScalarTransactionSnapshot Snapshot(const Run& w)
        { auto s=w->Result;s.Phase=w->Core.Phase;return s; }
        EditorPointScalarTransactionSnapshot Accept(const Run& w, std::function<void(EditorPointScalarTransactionSnapshot)> sink = {})
        {
            if(auto refused=GP::GpuTransactionAcceptRefusal(w->Core,bool(sink))){
                auto result=Snapshot(w);result.Status=refused->Status;result.Message=std::move(refused->Message);return result;}
            if(sink)w->Sink=GuardEditorProcessingResult(w->Core.Context,std::move(sink));
            (void)GP::BeginGpuTransactionAccept(GP::GpuTransactionOf(w));
            return Snapshot(w);
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
            auto w=Make(ctx,residency);auto& t=w->Core;w->Capture=std::move(capture);w->Entity=entity;
            w->Positions=std::move(positions);w->Params=params;w->Label=std::move(label);t.AutoAccept=automatic;t.JobLabel=std::string(jobLabel);
            t.Rings[kTyped]={.Key=MakeGpuPropertyKey(ctx.World,entity,w->Capture->Output),.ReadBack=true};t.RingCount=1;
            t.Identity={.EntityId=stableId,.Scope=ToEditorJobScope(w->Capture->Output.Domain),.OutputSemantic=GeometryPresentationSlotSemantic::ScalarField,.OutputName=w->Capture->Output.Name};
            if(residency->HasRing(Typed(w).Key))return refuse("Scalar output awaits Accept or Discard.");
            w->Result.LiveCount=w->Capture->LiveCount;
            if(testResidency){w->Result=testResult;w->Result.LiveCount=w->Capture->LiveCount;
                if(GP::AcquireGpuTransactionBack(t,kTyped,entity,w->Capture->Output,std::uint32_t(w->Capture->SlotCount),3)!=GpuRingAcquisition::Ready)
                    return refuse("Test scalar ring allocation failed.");
                Typed(w).Back.reset();
                if(!residency->Publish(Typed(w).Key))return refuse("Test scalar publication failed.");
                w->Sink=GuardEditorProcessingResult(ctx,std::move(sink));
                t.TestFront=true;GP::ReadyGpuTransaction(t);result=Snapshot(w);return w;}
            if(auto refused=GP::GpuTransactionStartRefusal(t)){result.Status=refused->Status;result.Message=std::move(refused->Message);return {};}
            std::shared_ptr<const SpatialIndexSnapshot> snapshot;bool reused{};std::string why;
            if(AcquirePointIndex(*ctx.SpatialIndices,ctx.World,entity,w->Positions,w->Capture->Slots,w->Capture->Points,w->Index,snapshot,reused,why)!=PointIndexState::Ready)return refuse(why);
            w->Result.IndexReused=reused;w->Result.Message="Device scalar analysis queued.";
            w->Sink=GuardEditorProcessingResult(ctx,std::move(sink));
            if(!GP::SubmitGpuTransactionRun(GP::GpuTransactionOf(w),"Device point scalar").IsValid()){
                result=Snapshot(w);result.Status=EditorCommandStatus::GeometryProcessingFailed;result.Message=MeshSupport::QueuedJobRejectedMessage(t.JobLabel);return {};}
            result=Snapshot(w);return w;
        }
    }
    }
    extern "C++" void GeometryProcessingDetail::JoinPointScalarRun(
        const EditorPointScalarTransactionHandle& run, EditorJobIdentity identity)
    {
        if (run) run->Core.Identity = std::move(identity);
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
        auto w = Make(context, residency);
        auto& t = w->Core;
        w->Entity = SelectionController::ToEntityHandle(publication.EntityId);
        t.RingCount = 3;
        auto& typed = t.Rings[kTyped];
        auto& presentation = t.Rings[kPresentation];
        auto& companion = t.Rings[kCompanion];
        typed = {.Key = MakeGpuPropertyKey(context.World, w->Entity, publication.Output), .ReadBack = true};
        presentation.Key = MakeGpuPropertyKey(context.World, w->Entity, GpuPropertyPresentationRef(publication.Output));
        if (residency->HasRing(typed.Key) || residency->HasRing(presentation.Key))
            return refuse("Scalar output awaits Accept or Discard.");
        // Admission and rollback run on the runtime thread, before another owner can acquire
        // either key. The rings own their reserved slots; a rollback delivers nothing.
        const auto rollback = [&](const char* why) { t.Delivered = true; GP::FailGpuTransaction(t, {}); return refuse(why); };
        typed.Back = AcquireGpuPropertyOutput(*residency, context.World, w->Entity, publication.Output, publication.Count, 3);
        typed.Generation = residency->RingGeneration(typed.Key);
        if (!typed.Back) return rollback("Typed scalar output reservation failed.");
        if (presentation.Key != typed.Key) {
            const auto view = AcquireGpuPropertyOutput(*residency, context.World, w->Entity,
                GpuPropertyPresentationRef(publication.Output), publication.Count, 3);
            presentation.Generation = residency->RingGeneration(presentation.Key);
            if (!view) return rollback("Scalar presentation reservation failed.");
        }
        if (publication.Companion) {
            companion = {.Key = MakeGpuPropertyKey(context.World, w->Entity, *publication.Companion), .ReadBack = true};
            if (residency->HasRing(companion.Key)) return rollback("Companion output awaits Accept or Discard.");
            companion.Back = AcquireGpuPropertyOutput(*residency, context.World, w->Entity, *publication.Companion, publication.Count, 3);
            companion.Generation = residency->RingGeneration(companion.Key);
            if (!companion.Back) return rollback("Companion output reservation failed.");
        }
        t.JobLabel = publication.Label;
        w->Publication = std::move(publication);
        w->Sink = GuardEditorProcessingResult(context, std::move(sink));
        w->Result.LiveCount = w->Publication->Count;
        result = Snapshot(w);
        return w;
    }
    std::optional<EditorPointScalarBack> AcquireEditorPointScalarBack(const EditorPointScalarTransactionHandle& w)
    {
        if (!w || !w->Publication || !Current(w)) return std::nullopt;
        const auto& p = *w->Publication;
        auto& t = w->Core;
        auto& residency = *t.Residency;
        Typed(w).Back.reset(); // Drop the admission lease before selecting a preview back.
        auto typed = AcquireGpuPropertyOutput(residency, t.Context.World, w->Entity, p.Output, p.Count, 3);
        if (!typed) return std::nullopt;
        Typed(w).Generation = residency.RingGeneration(Typed(w).Key);
        auto presentation = p.Output.ValueKind == Geometry::PropertyValueKind::Float ? typed :
            AcquireGpuPropertyOutput(residency, t.Context.World, w->Entity, GpuPropertyPresentationRef(p.Output), p.Count, 3);
        if (!presentation) return std::nullopt;
        if (p.Output.ValueKind != Geometry::PropertyValueKind::Float)
            t.Rings[kPresentation].Generation = residency.RingGeneration(t.Rings[kPresentation].Key);
        std::optional<Graphics::GpuPropertyView> companion;
        if (p.Companion) {
            t.Rings[kCompanion].Back.reset();
            companion = AcquireGpuPropertyOutput(residency, t.Context.World, w->Entity, *p.Companion, p.Count, 3);
            if (!companion) return std::nullopt;
        }
        return EditorPointScalarBack{*typed, *presentation, companion.value_or(Graphics::GpuPropertyView{})};
    }
    bool PublishEditorPointScalarBack(const EditorPointScalarTransactionHandle& w, bool ready,
        std::optional<std::array<float, 2>> scalarRange)
    {
        if (!w || !w->Publication || !Current(w)) return false;
        auto& t = w->Core;
        if (!t.Residency->Publish(Typed(w).Key, scalarRange)) return false;
        if (const auto& ring = t.Rings[kPresentation]; ring.Generation && !t.Residency->Publish(ring.Key, scalarRange)) return false;
        if (const auto& ring = t.Rings[kCompanion]; ring.Generation && !t.Residency->Publish(ring.Key)) return false;
        if (ready) GP::ReadyGpuTransaction(t);
        return true;
    }
    EditorPointScalarTransactionSnapshot SnapshotEditorPointScalar(const EditorProcessingCommands&,const EditorPointScalarTransactionHandle& w)
    {
        if(!w)return {.Phase=EditorGpuTransactionPhase::Failed,.Status=EditorCommandStatus::InvalidProcessingParameters,.Message="No scalar transaction."};
        auto s=Snapshot(w);if(s.Phase==EditorGpuTransactionPhase::ReadyToAccept){s.CanAccept=Current(w);if(!s.CanAccept)s.AcceptRefusalReason="Scalar input or output changed; discard and run again.";}return s;
    }
    EditorPointScalarTransactionSnapshot AcceptEditorPointScalar(const EditorProcessingCommands&,const EditorPointScalarTransactionHandle& w,
        std::function<void(EditorPointScalarTransactionSnapshot)> sink)
    { return w?Accept(w,std::move(sink)):EditorPointScalarTransactionSnapshot{.Phase=EditorGpuTransactionPhase::Failed,.Status=EditorCommandStatus::InvalidProcessingParameters,.Message="No scalar transaction."}; }
    void DiscardEditorPointScalar(const EditorProcessingCommands&,const EditorPointScalarTransactionHandle& w)
    {
        if(w)GP::DiscardGpuTransaction(w->Core,EditorCommandStatus::NoChange,"Scalar preview discarded; CPU fields retained.");
    }
}

module;
#include <atomic>
#include <functional>
#include <cstring>
#include <mutex>
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <format>
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
import Extrinsic.Runtime.Private.WorkspaceLease;
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
import Extrinsic.Runtime.StepRunExecution;
import Extrinsic.Runtime.StepRuns; // the worker-step hooks of Runtime.GpuTransactionLifecycle.hpp
import Extrinsic.Runtime.GpuPropertyBinding;
import Extrinsic.Graphics.PointScalarAnalysis;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.TransferQueue;
#include "Editor/internal/Runtime.EditorProcessingAccess.hpp"
#include "Editor/internal/Runtime.EditorGeometryHelpers.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.PointFields.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.JobFailure.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.RadiusRows.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.GpuFront.hpp"

#include "Editor/Operations/Runtime.GeometryProcessingOperations.GpuScalar.hpp"
#include "Editor/Operations/Runtime.GpuTransactionLifecycle.hpp"
#include "Services/Runtime.WorkerGpuPages.hpp"
namespace Extrinsic::Runtime
{
    template std::shared_ptr<Graphics::PointScalarWorkspace>
    SpatialIndexCache::LeaseGpuWorkspace<Graphics::PointScalarWorkspace>();

    namespace GP = GeometryProcessingDetail;
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
        std::optional<Graphics::GpuPropertyView> Input{}, Base{};
        // Leased from the spatial cache; the recorder closure keeps it while its work may run.
        std::shared_ptr<Graphics::PointScalarWorkspace> Workspace{};
        SpatialIndexHandle Index{};
        EditorPointScalarTransactionSnapshot Result{};
        EditorPointScalarTransactionSnapshot Initial{}; // Result as queued: a restart's new generation begins here
        std::function<void(EditorPointScalarTransactionSnapshot)> Sink{};
        // RUNTIME-348: the operation runtime's retention, called before the sink at the one terminal
        // delivery; Accept may replace the sink, never this.
        std::function<void(EditorPointScalarTransactionSnapshot)> Retain{};
        // ARCH-023: a publication transaction's own producer (SubmitEditorPointScalarRun) and its one step: Observed
        // once the owner took the step's outcome (ProducerFailure: why it failed).
        std::optional<EditorPointScalarProducer> Producer{};
        bool ProducerObserved{};
        std::string ProducerFailure{};
        // ARCH-023, the kernel's worker step: the retained index view (Init), and what the worker records with (its
        // own workspace reference, the views without their leases) and writes (the statistics it pulled).
        std::shared_ptr<SpatialGpuIndexLease> IndexView{};
        struct WorkerView
        {
            std::shared_ptr<Graphics::PointScalarWorkspace> Workspace{};
            std::shared_ptr<SpatialGpuIndexLease> Index{};
            Graphics::PointScalarResidentIo Io{};
            Graphics::PointScalarGpuStats Stats{};
        } Worker{};
    };
    namespace
    {
        using Run = EditorPointScalarTransactionHandle;
        constexpr std::size_t kTyped = 0, kCompanion = 1, kPresentation = 2;
        auto& Typed(const Run& w)
        {
            return w->Core.Rings[kTyped];
        }
        bool Current(const Run& w)
        {
            return GP::GpuTransactionCurrent(w->Core);
        }
        void Fail(const Run& w, std::string why)
        {
            GP::FailGpuTransaction(w->Core, std::move(why));
        }
        bool Acquire(const Run& w)
        {
            auto& r = *w->Core.Residency;
            const auto& ctx = w->Core.Context;
            const auto resolve = [&](const GeometryPropertyRef& ref)
            {
                const auto before = r.Stats().UploadBytes;
                auto v = ResolveGpuPropertyInput(r, *ctx.Scene, ctx.World, w->Entity, ref);
                const auto bytes = r.Stats().UploadBytes - before;
                w->Result.GpuInputUploadBytes += bytes;
                if (v && !bytes)
                    ++w->Result.GpuInputCacheHits;
                return v;
            };
            if (!w->Input)
                w->Input = resolve(w->Positions);
            if (!w->Input)
                return false;
            if (w->Capture->OutputWatch.Revision && !w->Base)
                w->Base = resolve(w->Capture->Output);
            if (w->Capture->OutputWatch.Revision && !w->Base)
                return false;
            // A ring another run created defers like an exhausted one.
            return GP::AcquireGpuTransactionBack(
                       w->Core, kTyped, w->Entity, w->Capture->Output, std::uint32_t(w->Capture->SlotCount), 3) ==
                GP::GpuRingAcquisition::Ready;
        }
        // A view for a worker's recording: the handles and addresses, not the lease (the owner keeps that).
        Graphics::GpuPropertyView Stripped(Graphics::GpuPropertyView view)
        {
            view.Lease.reset();
            return view;
        }
        // The Init (main thread, once the run computes): the canonical inputs and the output's write slot, the
        // workspace sized for this capture, and the retained index view; then the immutable recording view.
        GP::GpuTransactionPrepare Prepare(const Run& w, std::string& why)
        {
            auto& t = w->Core;
            if (!Acquire(w))
            {
                if (!GP::GpuTransactionDeferralsExhausted(t))
                    return GP::GpuTransactionPrepare::Pending;
                why = "Scalar residency refused an input or output slot.";
                return GP::GpuTransactionPrepare::Failed;
            }
            if (!w->Workspace)
                w->Workspace = t.Context.SpatialIndices->LeaseGpuWorkspace<Graphics::PointScalarWorkspace>();
            if (!w->IndexView)
                w->IndexView = t.Context.SpatialIndices->PrepareGpuIndex(w->Index, why);
            if (!w->IndexView)
                return GP::GpuTransactionPrepare::Failed;
            const auto& index = w->IndexView->View();
            const auto refusals = t.Context.SpatialIndices->Stats().ScratchRefusals;
            if (!w->Workspace || !w->Workspace->Prepare(w->Params, index.Count))
            {
                // A method budget refusal says so (bytes and the config key); else the generic reason.
                why =
                    t.Context.SpatialIndices->MethodScratchDiagnostic(refusals, "Scalar device workspace unavailable.");
                return GP::GpuTransactionPrepare::Failed;
            }
            w->Worker.Workspace = w->Workspace;
            w->Worker.Index = w->IndexView;
            w->Worker.Io = {.Positions = Stripped(*w->Input),
                .Output = Stripped(*Typed(w).Back),
                .Base = w->Base ? Stripped(*w->Base) : Graphics::GpuPropertyView{},
                .Nodes = index.NodesBDA,
                .LiveSlots = index.OriginalSlotsBDA,
                .LiveCount = index.Count};
            w->Result.GpuQueryBatches = 1;
            return GP::GpuTransactionPrepare::Ready;
        }
        // The step run's Step, on a worker: the kernel's one page against the retained index, its statistics pulled.
        StepOutcome Step(EditorPointScalarTransaction& w)
        {
            auto& worker = w.Worker;
            switch (w.Core.Pages->Run(
                [&](RHI::ICommandContext& cmd)
                {
                    for (const auto buffer : worker.Index->Barriers())
                        cmd.BufferBarrier(buffer,
                            RHI::MemoryAccess::ShaderWrite | RHI::MemoryAccess::TransferWrite,
                            RHI::MemoryAccess::ShaderRead);
                    return worker.Workspace->Record(cmd, w.Params, worker.Io);
                },
                std::as_writable_bytes(std::span{&worker.Stats, 1u})))
            {
            case WorkerGpuPageStatus::Complete: return StepOutcome::Converged;
            case WorkerGpuPageStatus::Aborted:  return StepOutcome::Failed;
            case WorkerGpuPageStatus::Refused:
                w.Core.WorkerFailure = "Scalar compute failed.";
                return StepOutcome::Failed;
            case WorkerGpuPageStatus::Failed:
            case WorkerGpuPageStatus::TimedOut: break;
            }
            w.Core.WorkerFailure = "Scalar compute did not complete: the device was lost.";
            return StepOutcome::Failed;
        }
        // The owner's side: ready once the step completed (its statistics are written).
        bool Poll(const Run& w)
        {
            return GP::GpuTransactionStepCompleted(w->Core);
        }
        void CompleteRun(const Run& w)
        {
            auto& t = w->Core;
            w->Result.Statistics = w->Worker.Stats; // the step's pulled statistics (no step runs now)
            if (const auto& s = w->Result.Statistics; s.Invalid)
            {
                const auto& p = w->Params;
                const auto capacity = Graphics::PointScalarNeighborWidth(p, std::uint32_t(w->Result.LiveCount));
                // R*C/N is an unverified hint: clustered or coincident points can overflow at any radius.
                if (p.Method == 2 && s.MaximumNeighbors > capacity)
                    Fail(w,
                        std::format("Radius candidate overflow: support radius {:.4g} is too large ({} candidates, "
                                    "capacity {}). Use about {:.4g}, 0 for automatic, or a CPU backend.",
                            p.SupportRadius,
                            s.MaximumNeighbors,
                            capacity,
                            p.SupportRadius * capacity / s.MaximumNeighbors));
                else
                    Fail(w, "Scalar numerical failure or radius candidate overflow; increase capacity or select CPU.");
                return;
            }
            Typed(w).Back.reset();
            if (!t.Residency->Publish(Typed(w).Key,
                    std::array<float, 2>{float(w->Result.Statistics.Minimum), float(w->Result.Statistics.Maximum)}))
            {
                Fail(w, "Scalar ring publication failed.");
                return;
            }
            // The input views and the workspace stay until Accept, Discard or the run's end (Release), so a restart
            // records again without resolving or leasing anything.
            w->Result.Message = "GPU scalar awaits Accept or Discard.";
            GP::ReadyGpuTransaction(t);
        }
        void CompleteAccept(const Run& w)
        {
            auto& t = w->Core;
            auto& residency = *t.Residency;
            if (w->Publication)
            {
                const auto& readback = Typed(w).Readback;
                const auto& companion = t.Rings[kCompanion].Readback;
                const auto& output = w->Publication->Output;
                const auto layout = MakeGpuPropertyLayout(output.ValueKind, w->Publication->Count);
                if (!readback || readback->Failed || !layout || readback->Bytes.size() != layout->Bytes())
                {
                    Fail(w, "Typed scalar front readback failed.");
                    return;
                }
                if (w->Publication->Companion &&
                    (!companion || companion->Failed ||
                        companion->Bytes.size() !=
                            MakeGpuPropertyLayout(w->Publication->Companion->ValueKind, w->Publication->Count)
                                ->Bytes()))
                {
                    Fail(w, "Companion scalar front readback failed.");
                    return;
                }
                const auto publication = readback->Lease ? readback->Lease->Publication : 0;
                w->Result.CpuStageReadbackBytes += readback->Bytes.size() + (companion ? companion->Bytes.size() : 0);
                // The publication callback may run history observers synchronously (the
                // lifecycle ignores a Discard they issue).
                const auto status = w->Publication->Companion
                    ? w->Publication->PublishPair(readback->Bytes, companion->Bytes)
                    : w->Publication->Publish(readback->Bytes);
                readback->Lease.reset();
                if (status != EditorCommandStatus::Applied && status != EditorCommandStatus::NoChange)
                {
                    GP::FinishGpuTransaction(
                        t, EditorGpuTransactionPhase::Failed, status, "Typed scalar publication rejected.");
                    return;
                }
                if (w->Publication->Companion)
                {
                    auto& ring = t.Rings[kCompanion];
                    const auto companionPublication = companion->Lease ? companion->Lease->Publication : 0;
                    companion->Lease.reset();
                    const auto a = BuildGeometryAvailability(t.Context.Scene->Raw(), w->Entity);
                    const auto watch = GP::ObserveGeometryProperty(
                        a, w->Publication->Companion->Domain, w->Publication->Companion->Name);
                    const auto resolution = ResolveGeometryProperty(a, *w->Publication->Companion);
                    if (!resolution.Resolved() ||
                        resolution.ResolvedValueKind != w->Publication->Companion->ValueKind || !watch.Revision ||
                        !residency.BindRevision(ring.Key, *watch.Revision, companionPublication))
                        (void)residency.Discard(ring.Key, ring.Generation);
                }
                const auto availability = BuildGeometryAvailability(t.Context.Scene->Raw(), w->Entity);
                const auto watch = GP::ObserveGeometryProperty(availability, output.Domain, output.Name);
                const auto resolution = ResolveGeometryProperty(availability, output);
                if (!resolution.Resolved() || resolution.ResolvedValueKind != output.ValueKind || !watch.Revision ||
                    !residency.BindRevision(Typed(w).Key, *watch.Revision, publication))
                    (void)residency.Discard(Typed(w).Key, Typed(w).Generation);
                if (const auto& ring = t.Rings[kPresentation]; ring.Generation)
                {
                    if (!watch.Revision || !residency.BindRevision(ring.Key, *watch.Revision))
                        (void)residency.Discard(ring.Key, ring.Generation);
                }
                GP::FinishGpuTransaction(
                    t, EditorGpuTransactionPhase::Applied, status, "Typed scalar result accepted.");
                return;
            }
            auto& capture = *w->Capture;
            std::uint64_t publication{};
            if (!t.TestFront)
            {
                const auto& r = Typed(w).Readback;
                if (!r || r->Failed || r->Bytes.size() != capture.SlotCount * 4)
                {
                    Fail(w, "Scalar front readback failed.");
                    return;
                }
                publication = r->Lease ? r->Lease->Publication : 0;
                std::memcpy(capture.AfterValues.data(), r->Bytes.data(), r->Bytes.size());
                w->Result.CpuStageReadbackBytes += r->Bytes.size();
                r->Lease.reset();
            }
            else if (auto front = residency.Front(Typed(w).Key))
                publication = front->Publication;
            const auto status = EditorFeatureDetail::ToEditorCommandStatus(
                GP::PublishPointScalarField(t.Context, w->Entity, capture, w->Label));
            if (status != EditorCommandStatus::Applied)
            {
                GP::FinishGpuTransaction(t, EditorGpuTransactionPhase::Failed, status, "Scalar publication rejected.");
                return;
            }
            const auto a = BuildGeometryAvailability(t.Context.Scene->Raw(), w->Entity);
            const auto watch = GP::ObserveGeometryProperty(a, capture.Output.Domain, capture.Output.Name);
            if (!watch.Revision || !residency.BindRevision(Typed(w).Key, *watch.Revision, publication))
                (void)residency.Discard(Typed(w).Key, Typed(w).Generation);
            GP::FinishGpuTransaction(t,
                EditorGpuTransactionPhase::Applied,
                status,
                "Device scalar accepted; input upload: " + std::to_string(w->Result.GpuInputUploadBytes) +
                    " bytes; residency hits: " + std::to_string(w->Result.GpuInputCacheHits) +
                    "; CPU-stage readback: " + std::to_string(w->Result.CpuStageReadbackBytes) + " bytes.");
        }
        // Builds the transaction on the shared lifecycle; hooks reach it by raw pointer.
        Run Make(const EditorProcessingContext& ctx, Graphics::GpuPropertyResidency* residency)
        {
            auto w = std::make_shared<EditorPointScalarTransaction>();
            auto& t = w->Core;
            t.Context = ctx;
            t.Residency = residency;
            t.Label = "Scalar analysis";
            t.AcceptJobName = "Accept point scalar";
            auto* raw = w.get();
            // Hooks run while a job or caller owns the transaction; recorders take ownership.
            const auto self = [raw]
            {
                return raw->shared_from_this();
            };
            t.Hooks = {.Current =
                           [raw]
                {
                    return (!raw->Publication || raw->Publication->Current()) &&
                        (raw->Publication ||
                            GP::PointScalarFieldCurrent(raw->Core.Context, raw->Entity, *raw->Capture));
                },
                .Poll =
                    [self]
                {
                    return Poll(self());
                },
                .CompleteRun =
                    [self]
                {
                    CompleteRun(self());
                },
                .CompleteAccept =
                    [self]
                {
                    CompleteAccept(self());
                },
                // A restart records the kernel again from the capture: the inputs, workspace and index view stay (the
                // capture is current); the Init acquires a fresh write slot; statistics start over.
                .Restart =
                    [raw](std::string&)
                {
                    raw->Result = raw->Initial;
                    raw->Worker.Stats = {};
                    return true;
                },
                .Prepare =
                    [self](std::string& diagnostic)
                {
                    return Prepare(self(), diagnostic);
                },
                .Step =
                    [raw](StepRunMetric&)
                {
                    return Step(*raw);
                },
                .Uses =
                    [raw](std::vector<RHI::BufferHandle>& buffers)
                {
                    for (const auto* view : {&raw->Input, &raw->Base})
                        if (*view)
                            buffers.push_back((*view)->Buffer);
                },
                // No step runs any more (the core waits for that): the worker's references go with the owner's.
                .Release =
                    [raw]
                {
                    raw->Input.reset();
                    raw->Base.reset();
                    raw->Workspace.reset();
                    raw->IndexView.reset();
                    raw->Worker = {};
                    if (raw->Producer && raw->Producer->Release)
                        raw->Producer->Release();
                },
                .Deliver =
                    [raw](EditorCommandStatus status, std::string message)
                {
                    raw->Result.Phase = raw->Core.Phase;
                    raw->Result.StepRun = raw->Core.StepRun;
                    raw->Result.Status = status;
                    raw->Result.Message = std::move(message);
                    if (auto retain = std::move(raw->Retain))
                        retain(raw->Result);
                    if (auto sink = std::move(raw->Sink))
                        sink(raw->Result);
                }};
            return w;
        }
        // The producer's owner side (after its Init): once its worker step completed, the outcome (its preview
        // publication, or why it failed) is taken once, in the drain its step run comes to rest, so a resting run
        // already shows its result; the Run job then waits for the step run's Commit (the output stays admitted).
        bool PollProducer(const Run& w)
        {
            auto& t = w->Core;
            if (!GP::GpuTransactionStepCompleted(t))
                return false;
            if (!w->ProducerObserved)
            {
                w->ProducerObserved = true;
                w->ProducerFailure = w->Producer->Outcome();
                if (!w->ProducerFailure.empty())
                    return true;
            }
            return !w->ProducerFailure.empty() || t.CommitWanted;
        }
        // The producer's step, on a worker: every page through the transaction's page gate.
        StepOutcome ProducerStep(EditorPointScalarTransaction& w)
        {
            const EditorPointScalarPage page =
                [&w](const std::function<RHI::BufferHandle(RHI::ICommandContext&)>& record,
                    const std::span<std::byte> out)
            {
                return w.Core.Pages->Run(record, out) == WorkerGpuPageStatus::Complete;
            };
            auto failure = w.Producer->Step(page);
            if (failure.empty())
                return StepOutcome::Converged;
            if (!w.Core.Pages->Aborted()) // abandoned: the owner ended the run, there is nothing to report
                w.Core.WorkerFailure = std::move(failure);
            return StepOutcome::Failed;
        }
        EditorPointScalarTransactionSnapshot Snapshot(const Run& w)
        {
            auto s = w->Result;
            s.Phase = w->Core.Phase;
            s.StepRun = w->Core.StepRun;
            return s;
        }
        EditorPointScalarTransactionSnapshot Accept(
            const Run& w, std::function<void(EditorPointScalarTransactionSnapshot)> sink = {})
        {
            if (auto refused = GP::GpuTransactionAcceptRefusal(w->Core, bool(sink)))
            {
                auto result = Snapshot(w);
                result.Status = refused->Status;
                result.Message = std::move(refused->Message);
                return result;
            }
            if (sink)
                w->Sink = GuardEditorProcessingResult(w->Core.Context, std::move(sink));
            (void)GP::BeginGpuTransactionAccept(GP::GpuTransactionOf(w));
            return Snapshot(w);
        }
    }
    extern "C++"
    {
        namespace GeometryProcessingDetail
        {
            bool AdmitPointScalarGpu(const EditorProcessingContext& ctx,
                const PointScalarCapture& c,
                const Graphics::PointScalarGpuParams& p,
                std::string& why)
            {
                const auto fail = [&](const char* message)
                {
                    why = message;
                    return false;
                };
                if (c.Output.ValueKind != Geometry::PropertyValueKind::Float)
                    return fail(
                        "Vulkan scalar analysis supports float outputs only; select the CPU backend for other scalar storage.");
                if (c.HasSubnormalCoordinates)
                    return fail(
                        "Vulkan scalar analysis requires normal or zero coordinates; subnormal components are unsupported.");
                const auto width = Graphics::PointScalarNeighborWidth(p, std::uint32_t(c.LiveCount));
                if (!c.ValidLbvh || !c.LiveCount || c.LiveCount > (1u << 20) || c.SlotCount > (1u << 24) || !width ||
                    width > 4096 || std::uint64_t(width) * c.LiveCount > (1u << 24))
                    return fail("Vulkan scalar analysis exceeds LBVH coordinate, row or neighbor workspace limits.");
                if ((p.Method < 2 && p.K > 63) || std::fpclassify(p.Bandwidth) == FP_SUBNORMAL ||
                    std::fpclassify(p.Scale) == FP_SUBNORMAL)
                    return fail("Vulkan scalar analysis requires k<=63 and normal or zero parameters.");
                if (p.Method == 2 && (!std::isfinite(p.QueryRadius) || p.QueryRadius > 1e18f))
                    return fail("Vulkan scalar query radius must be finite and within 1e18.");
                if (!ctx.Device || !ctx.Device->IsOperational() || !ctx.Device->SupportsShaderFloat64() ||
                    !ctx.JobCommands.Available() || !ctx.SpatialIndices || !ctx.SpatialIndices->GpuQueriesAvailable())
                    return fail(
                        "Vulkan scalar analysis requires an operational float64 device and framed spatial cache/jobs.");
                return true;
            }
            EditorPointScalarTransactionHandle StartPointScalarGpu(const EditorProcessingContext& ctx,
                std::shared_ptr<PointScalarCapture> capture,
                entt::entity entity,
                std::uint32_t stableId,
                GeometryPropertyRef positions,
                const Graphics::PointScalarGpuParams& params,
                std::string label,
                std::string_view jobLabel,
                EditorPointScalarTransactionSnapshot& result,
                std::function<void(EditorPointScalarTransactionSnapshot)> sink,
                const StepRunExecution& execution,
                Graphics::GpuPropertyResidency* testResidency,
                const EditorPointScalarTransactionSnapshot& testResult,
                std::function<void(EditorPointScalarTransactionSnapshot)> retain,
                std::string owner)
            {
                auto* residency = testResidency
                    ? testResidency
                    : (ctx.SpatialIndices ? ctx.SpatialIndices->PropertyResidency() : nullptr);
                const auto refuse = [&](std::string why) -> EditorPointScalarTransactionHandle
                {
                    result.Status = EditorCommandStatus::InvalidProcessingParameters;
                    result.Message = std::move(why);
                    return {};
                };
                if (!residency)
                    return refuse("Scalar analysis needs GPU property residency.");
                auto w = Make(ctx, residency);
                auto& t = w->Core;
                w->Capture = std::move(capture);
                w->Entity = entity;
                w->Positions = std::move(positions);
                w->Retain = std::move(retain);
                w->Params = params;
                w->Label = std::move(label);
                t.Execution = OneShotExecution(execution);
                t.JobLabel = std::string(jobLabel);
                t.Owner = std::move(owner);
                t.Rings[kTyped] = {.Key = MakeGpuPropertyKey(ctx.World, entity, w->Capture->Output), .ReadBack = true};
                t.RingCount = 1;
                t.Identity = {.EntityId = stableId,
                    .Scope = ToEditorJobScope(w->Capture->Output.Domain),
                    .OutputSemantic = GeometryPresentationSlotSemantic::ScalarField,
                    .OutputName = w->Capture->Output.Name,
                    .RequestedDomain = EditorJobDomain::GpuCompute};
                if (residency->HasRing(Typed(w).Key))
                {
                    result.OutputBusy = true;
                    return refuse("Scalar output awaits Accept or Discard.");
                }
                w->Result.LiveCount = w->Capture->LiveCount;
                if (testResidency)
                {
                    w->Result = testResult;
                    w->Result.LiveCount = w->Capture->LiveCount;
                    if (GP::AcquireGpuTransactionBack(
                            t, kTyped, entity, w->Capture->Output, std::uint32_t(w->Capture->SlotCount), 3) !=
                        GpuRingAcquisition::Ready)
                        return refuse("Test scalar ring allocation failed.");
                    Typed(w).Back.reset();
                    if (!residency->Publish(Typed(w).Key))
                        return refuse("Test scalar publication failed.");
                    w->Sink = GuardEditorProcessingResult(ctx, std::move(sink));
                    t.TestFront = true;
                    GP::ReadyGpuTransaction(t);
                    result = Snapshot(w);
                    return w;
                }
                if (auto refused = GP::GpuTransactionStartRefusal(t))
                {
                    result.Status = refused->Status;
                    result.Message = std::move(refused->Message);
                    result.OutputBusy = true;
                    return {};
                }
                std::shared_ptr<const SpatialIndexSnapshot> snapshot;
                bool reused{};
                std::string why;
                if (AcquirePointIndex(*ctx.SpatialIndices,
                        ctx.World,
                        entity,
                        w->Positions,
                        w->Capture->Slots,
                        w->Capture->Points,
                        w->Index,
                        snapshot,
                        reused,
                        why,
                        false) != PointIndexState::Ready)
                    return refuse(why);
                w->Result.IndexReused = reused;
                w->Result.Message = "Device scalar analysis queued.";
                w->Initial = w->Result;
                t.Pages = std::make_shared<WorkerGpuPages>(*ctx.Device); // the kernel's page runs on the step's worker
                w->Sink = GuardEditorProcessingResult(ctx, std::move(sink));
                if (!GP::SubmitGpuTransactionRun(GP::GpuTransactionOf(w), "Device point scalar").IsValid())
                {
                    result = Snapshot(w);
                    result.Status = EditorCommandStatus::GeometryProcessingFailed;
                    result.Message = MeshSupport::QueuedJobRejectedMessage(t.JobLabel);
                    return {};
                }
                result = Snapshot(w);
                return w;
            }
        }
    }
    void JoinEditorPointScalarRun(
        const EditorPointScalarTransactionHandle& run, EditorJobIdentity identity)
    {
        if (run)
            run->Core.Identity = std::move(identity);
    }
    bool SubmitEditorPointScalarRun(const EditorPointScalarTransactionHandle& w,
        EditorJobIdentity identity,
        StepRunExecution execution,
        EditorPointScalarProducer producer,
        std::string debugName,
        std::string& refusal)
    {
        if (!w || !w->Publication || !producer.Prepare || !producer.Step || !producer.Outcome || !producer.Reset)
        {
            refusal = "A scalar producer needs a publication transaction and every callable.";
            return false;
        }
        auto& t = w->Core;
        if (!t.Context.StepRuns)
        {
            // Closed without delivery (the caller reports the refusal); the reserved rings go.
            refusal = t.JobLabel + " needs the step-run service for its run controls; none is composed.";
            t.Delivered = true;
            GP::FailGpuTransaction(t, {});
            return false;
        }
        w->Producer = std::move(producer);
        auto* raw = w.get();
        const auto self = [raw]
        {
            return raw->shared_from_this();
        };
        t.Hooks.Poll = [self]
        {
            return PollProducer(self());
        };
        t.Hooks.CompleteRun = [raw]
        {
            if (!raw->ProducerFailure.empty())
                GP::FailGpuTransaction(raw->Core, raw->ProducerFailure);
            else
                GP::ReadyGpuTransaction(raw->Core);
        };
        t.Hooks.Restart = [raw](std::string& diagnostic)
        {
            if (!raw->ProducerFailure.empty())
            {
                diagnostic = raw->ProducerFailure;
                return false;
            }
            raw->Producer->Reset();
            GP::RestartGpuTransactionRings(raw->Core);
            raw->ProducerObserved = false;
            return true;
        };
        // ARCH-023: the producer's Init on the owner, its one step on the step run's worker.
        t.Hooks.Prepare = [raw](std::string& diagnostic)
        {
            if (raw->Producer->Prepare())
                return GP::GpuTransactionPrepare::Ready;
            diagnostic = raw->Producer->Outcome();
            if (diagnostic.empty())
                diagnostic = "The scalar producer could not start.";
            return GP::GpuTransactionPrepare::Failed;
        };
        t.Hooks.Step = [raw](StepRunMetric&)
        {
            return ProducerStep(*raw);
        };
        t.Hooks.Uses = [raw](std::vector<RHI::BufferHandle>& buffers)
        {
            if (raw->Producer->Uses)
                raw->Producer->Uses(buffers);
        };
        t.Identity = std::move(identity);
        t.Pages = std::make_shared<WorkerGpuPages>(*t.Context.Device);
        // A one-shot (the producer's traversal): the core keeps the caller's start and auto_accept, one step, no live
        // preview.
        t.Execution = execution;
        if (!GP::SubmitGpuTransactionRun(GP::GpuTransactionOf(w), std::move(debugName)).IsValid())
        {
            refusal = GP::MeshSupport::QueuedJobRejectedMessage(t.JobLabel);
            return false;
        }
        return true;
    }
    EditorPointScalarTransactionHandle BeginEditorPointScalarPublication(const EditorProcessingCommands& commands,
        EditorPointScalarPublication publication,
        EditorPointScalarTransactionSnapshot& result,
        std::function<void(EditorPointScalarTransactionSnapshot)> sink)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        auto* residency = context.SpatialIndices ? context.SpatialIndices->PropertyResidency() : nullptr;
        const auto refuse = [&](const char* why) -> EditorPointScalarTransactionHandle
        {
            result = {.Phase = EditorGpuTransactionPhase::Failed,
                .Status = EditorCommandStatus::InvalidProcessingParameters,
                .Message = why};
            return {};
        };
        if (publication.PreviewBuffers < 2u || publication.PreviewBuffers > 3u)
            return refuse("preview_buffers must be 2 or 3.");
        if (!residency || !publication.Current || (!publication.Companion && !publication.Publish) ||
            (publication.Companion && !publication.PublishPair) || !publication.Count ||
            !context.JobCommands.Available() || GeometryPropertyComponentCount(publication.Output.ValueKind) != 1 ||
            !MakeGpuPropertyLayout(publication.Output.ValueKind, publication.Count))
            return refuse("Typed scalar publication requires residency, jobs and a valid output.");
        if (publication.Companion &&
            (publication.Companion->Name == publication.Output.Name ||
                publication.Companion->Domain != publication.Output.Domain ||
                GeometryPropertyComponentCount(publication.Companion->ValueKind) != 1 ||
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
        const auto rollback = [&](const char* why)
        {
            t.Delivered = true;
            GP::FailGpuTransaction(t, {});
            return refuse(why);
        };
        typed.Back = AcquireGpuPropertyOutput(
            *residency, context.World, w->Entity, publication.Output, publication.Count, publication.PreviewBuffers);
        typed.Generation = residency->RingGeneration(typed.Key);
        if (!typed.Back)
            return rollback("Typed scalar output reservation failed.");
        if (presentation.Key != typed.Key)
        {
            const auto view = AcquireGpuPropertyOutput(*residency,
                context.World,
                w->Entity,
                GpuPropertyPresentationRef(publication.Output),
                publication.Count,
                publication.PreviewBuffers);
            presentation.Generation = residency->RingGeneration(presentation.Key);
            if (!view)
                return rollback("Scalar presentation reservation failed.");
        }
        if (publication.Companion)
        {
            companion = {.Key = MakeGpuPropertyKey(context.World, w->Entity, *publication.Companion), .ReadBack = true};
            if (residency->HasRing(companion.Key))
                return rollback("Companion output awaits Accept or Discard.");
            companion.Back = AcquireGpuPropertyOutput(*residency,
                context.World,
                w->Entity,
                *publication.Companion,
                publication.Count,
                publication.PreviewBuffers);
            companion.Generation = residency->RingGeneration(companion.Key);
            if (!companion.Back)
                return rollback("Companion output reservation failed.");
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
        if (!w || !w->Publication || !Current(w))
            return std::nullopt;
        const auto& p = *w->Publication;
        auto& t = w->Core;
        auto& residency = *t.Residency;
        Typed(w).Back.reset(); // Drop the admission lease before selecting a preview back.
        auto typed =
            AcquireGpuPropertyOutput(residency, t.Context.World, w->Entity, p.Output, p.Count, p.PreviewBuffers);
        if (!typed)
            return std::nullopt;
        Typed(w).Generation = residency.RingGeneration(Typed(w).Key);
        auto presentation = p.Output.ValueKind == Geometry::PropertyValueKind::Float
            ? typed
            : AcquireGpuPropertyOutput(residency,
                  t.Context.World,
                  w->Entity,
                  GpuPropertyPresentationRef(p.Output),
                  p.Count,
                  p.PreviewBuffers);
        if (!presentation)
            return std::nullopt;
        if (p.Output.ValueKind != Geometry::PropertyValueKind::Float)
            t.Rings[kPresentation].Generation = residency.RingGeneration(t.Rings[kPresentation].Key);
        std::optional<Graphics::GpuPropertyView> companion;
        if (p.Companion)
        {
            t.Rings[kCompanion].Back.reset();
            companion = AcquireGpuPropertyOutput(
                residency, t.Context.World, w->Entity, *p.Companion, p.Count, p.PreviewBuffers);
            if (!companion)
                return std::nullopt;
            t.Rings[kCompanion].Generation = residency.RingGeneration(t.Rings[kCompanion].Key); // renewed on restart
        }
        return EditorPointScalarBack{*typed, *presentation, companion.value_or(Graphics::GpuPropertyView{})};
    }
    bool PublishEditorPointScalarBack(
        const EditorPointScalarTransactionHandle& w, bool ready, std::optional<std::array<float, 2>> scalarRange)
    {
        if (!w || !w->Publication || !Current(w))
            return false;
        auto& t = w->Core;
        if (!t.Residency->Publish(Typed(w).Key, scalarRange))
            return false;
        if (const auto& ring = t.Rings[kPresentation]; ring.Generation && !t.Residency->Publish(ring.Key, scalarRange))
            return false;
        if (const auto& ring = t.Rings[kCompanion]; ring.Generation && !t.Residency->Publish(ring.Key))
            return false;
        if (ready)
            GP::ReadyGpuTransaction(t);
        return true;
    }
    EditorPointScalarTransactionSnapshot SnapshotEditorPointScalar(
        const EditorProcessingCommands&, const EditorPointScalarTransactionHandle& w)
    {
        if (!w)
            return {.Phase = EditorGpuTransactionPhase::Failed,
                .Status = EditorCommandStatus::InvalidProcessingParameters,
                .Message = "No scalar transaction."};
        auto s = Snapshot(w);
        if (s.Phase == EditorGpuTransactionPhase::ReadyToAccept)
        {
            s.CanAccept = Current(w);
            if (!s.CanAccept)
                s.AcceptRefusalReason = "Scalar input or output changed; discard and run again.";
        }
        // Running, accepting or finished: the lifecycle's own refusal says why Accept is not available now.
        else if (const auto refused = GeometryProcessingDetail::GpuTransactionAcceptRefusal(w->Core))
            s.AcceptRefusalReason = refused->Message;
        return s;
    }
    EditorPointScalarTransactionSnapshot AcceptEditorPointScalar(const EditorProcessingCommands&,
        const EditorPointScalarTransactionHandle& w,
        std::function<void(EditorPointScalarTransactionSnapshot)> sink)
    {
        return w ? Accept(w, std::move(sink))
                 : EditorPointScalarTransactionSnapshot{.Phase = EditorGpuTransactionPhase::Failed,
                       .Status = EditorCommandStatus::InvalidProcessingParameters,
                       .Message = "No scalar transaction."};
    }
    void DiscardEditorPointScalar(const EditorProcessingCommands&, const EditorPointScalarTransactionHandle& w)
    {
        if (w)
            GP::DiscardGpuTransaction(
                w->Core, EditorCommandStatus::NoChange, "Scalar preview discarded; CPU fields retained.");
    }
    EditorPointScalarTransactionHandle MakeEditorPointScalarTransactionForTest(const EditorProcessingCommands& commands,
        const std::uint32_t stableEntityId, GeometryPropertyRef positions, GeometryPropertyRef output,
        std::vector<float> values, Graphics::GpuPropertyResidency* testResidency,
        const EditorPointScalarTransactionSnapshot& diagnostics, const Graphics::PointScalarGpuParams& params,
        EditorPointScalarTransactionSnapshot* refused)
    {
        std::string reason;
        const auto refuse = [&](std::string text) -> EditorPointScalarTransactionHandle
        {
            if (refused)
                *refused = {.Phase = EditorGpuTransactionPhase::Failed,
                    .Status = EditorCommandStatus::InvalidProcessingParameters,
                    .Message = std::move(text)};
            return {};
        };
        const auto& ctx = EditorProcessingCommandsAccess::Resolve(commands);
        if (!ctx.Scene)
            return refuse("Scene is unavailable.");
        const auto entity = EditorFeatureDetail::ResolveStableEntity(ctx.Scene->Raw(), stableEntityId);
        if (!entity)
            return refuse("Target entity is stale or missing.");
        auto capture = std::make_shared<GP::PointScalarCapture>();
        if (!GP::CapturePointScalarField(ctx, *entity, BuildGeometryAvailability(ctx.Scene->Raw(), *entity), positions,
                output, "Weight", true, *capture, reason))
            return refuse(std::move(reason));
        if (testResidency)
        {
            if (values.size() != capture->SlotCount)
                return refuse("Test values must give one value per slot.");
            capture->AfterValues = std::move(values);
        }
        else if (!GP::AdmitPointScalarGpu(ctx, *capture, params, reason))
            return refuse(std::move(reason));
        EditorPointScalarTransactionSnapshot state;
        auto run = GP::StartPointScalarGpu(ctx,
            capture,
            *entity,
            stableEntityId,
            positions,
            params,
            "Estimate density weights",
            "Density weights",
            state,
            {},
            kOneShotStepRunExecution,
            testResidency,
            diagnostics);
        if (!run && refused)
            *refused = std::move(state);
        return run;
    }
}

// The shared two-phase GPU Run/Accept transaction lifecycle (RUNTIME-311); see the header.
// An ordinary translation unit so no method family depends on another family module.
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <entt/entity/registry.hpp>

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Error;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Graphics.GpuPropertyResidency;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.EditorProcessing;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.GpuPropertyBinding;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.StepRuns;
import Extrinsic.Runtime.StepRunExecution;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.TransferQueue;

#include "Editor/internal/Runtime.EditorProcessingAccess.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.JobFailure.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.GpuFront.hpp"
#include "Editor/Operations/Runtime.GpuTransactionLifecycle.hpp"
#include "Services/Runtime.WorkerGpuPages.hpp"

extern "C++"
{
namespace Extrinsic::Runtime::GeometryProcessingDetail
{
    bool EditorProcessingContextWorldCurrent(const EditorProcessingContext&);

    namespace
    {
        // The transaction's outcome for its run: GpuCompute once the device produced a result
        // (`ran`), otherwise unknown (cancelled or failed before one); the message is the diagnostic.
        void ReportOutcome(const GpuTransactionCore& t, const bool ran, std::string diagnostic)
        {
            if (!t.RunToken.IsValid() || !t.Context.JobCommands.ReportOutcome) return;
            t.Context.JobCommands.ReportOutcome(
                t.RunToken, {.ResolvedDomain = ran ? std::optional{EditorJobDomain::GpuCompute} : std::nullopt,
                             .Diagnostic = std::move(diagnostic)});
        }

        // ARCH-023, a worker-stepped run's slots (the typed Uses and the rings' write slots), stamped with the
        // compute fence before they go.
        void StampWorkerSlots(const GpuTransactionCore& t)
        {
            if (!t.Pages)
                return;
            std::vector<RHI::BufferHandle> buffers;
            if (t.Hooks.Uses)
                t.Hooks.Uses(buffers);
            for (std::size_t i = 0; i < t.RingCount; ++i)
                if (const auto& back = t.Rings[i].Back)
                    buffers.push_back(back->Buffer);
            StampGpuTransactionUses(t, buffers);
        }

        // ARCH-023: no worker step of the transaction runs: none was ever possible (no pages or no step run), or no
        // segment is in flight (queued or running). Release is called only as the transaction ends, which ends its
        // step run before the pump could start another segment.
        bool WorkerSettled(const GpuTransactionCore& t)
        {
            if (!t.Pages || !t.StepRun || !t.Context.StepRuns)
                return true;
            const auto run = t.Context.StepRuns->Observe(t.StepRun);
            return !run || !run->WorkerInFlight;
        }

        // The typed Release, once due and once no worker step can read what it drops (the worker's own workspace and
        // index references go with it); otherwise the step run's Finished calls it at its settled end.
        void ReleaseTyped(GpuTransactionCore& t)
        {
            if (!t.ReleaseDue || !WorkerSettled(t))
                return;
            t.ReleaseDue = false;
            if (t.Hooks.Release)
                t.Hooks.Release();
        }

        void Release(GpuTransactionCore& t)
        {
            // A worker-stepped run submits nothing more (a page under way finishes first), so the compute fence
            // covers every page that used its slots; the typed views and workspaces go after that, once no step runs.
            if (t.Pages)
            {
                t.Pages->Seal();
                StampWorkerSlots(t);
            }
            t.ReleaseDue = true;
            ReleaseTyped(t);
            for (std::size_t i = 0; i < t.RingCount; ++i)
            {
                auto& ring = t.Rings[i];
                ring.Back.reset();
                // A framed readback still queued records nothing; a landed one is dropped.
                if (ring.Readback)
                {
                    ring.Readback->Abandoned = true;
                    ring.Readback->Lease.reset();
                }
                if (t.Residency && ring.Generation) (void)t.Residency->Discard(ring.Key, ring.Generation);
            }
        }

        // The transaction's step run (an empty handle without one).
        StepRunDriver StepRunOf(const GpuTransactionCore& t)
        {
            return t.StepRun && t.Context.StepRuns ? t.Context.StepRuns->Driver(t.StepRun) : StepRunDriver{};
        }

        // The transaction's end as its step run sees it, phase for phase: Applied commits the pending Commit, Failed
        // fails it (or the step), Discarded (a discard, cancel or stale input) discards the run. Ignored by a step run
        // that already ended.
        void TellStepRun(const GpuTransactionCore& t, const std::string& message)
        {
            if (!t.StepRun || !t.Context.StepRuns)
                return;
            auto driver = StepRunOf(t);
            const auto run = t.Context.StepRuns->Observe(t.StepRun);
            std::string refusal;
            if (t.Phase == EditorGpuTransactionPhase::Applied)
                driver.CommitFinished(true);
            else if (t.Phase == EditorGpuTransactionPhase::Discarded &&
                t.Context.StepRuns->Control(t.StepRun, StepRunAction::Discard, 1u, refusal))
            {
            }
            else if (run && run->Phase == StepRunPhase::Accepting) // a publication under way refused the discard
                driver.CommitFinished(false, message);
            else if (t.Phase == EditorGpuTransactionPhase::Failed)
                (void)driver.Stepped({}, StepOutcome::Failed);
            driver.Recording(false); // the device work ended; a terminal step run retires
        }

        // Unpublished end of either job: cancelled through the editor job surface, stale, or
        // dropped. Nothing of the run may publish any more.
        void Finalize(GpuTransactionCore& t)
        {
            t.Abandoned = true;
            if (t.Delivered)
            {
                Release(t);
                return;
            }
            FinishGpuTransaction(t, EditorGpuTransactionPhase::Discarded, EditorCommandStatus::StaleEntity,
                                 t.Label + " cancelled or stale; previous output retained.");
        }

        // A batch's automatic Accept follows its step run: never after a Stop (a stopped run waits for Accept or
        // Discard), never while a restart waits for its new generation.
        bool AutoAcceptAllowed(const GpuTransactionCore& t)
        {
            if (!t.StepRun || !t.Context.StepRuns)
                return true;
            const auto run = t.Context.StepRuns->Observe(t.StepRun);
            return run && run->End != StepRunEnd::Stopped && StepRunOf(t).MayCommit();
        }

        // The Run job of one generation. Its readiness records the device work only once the step run computes (a
        // ready, paused or restarting run records nothing), then polls it; its publication folds the result and, for
        // a batch, starts the automatic Accept.
        JobToken SubmitRunJob(const GpuTransactionHandle& t)
        {
            JobDesc job{.DebugName = t->RunJobName,
                .Scope = t->Context.World,
                .Work =
                    [](const JobCancellation&)
                {
                    return JobResultEnvelope::Make(true);
                },
                .IsReadyToApply =
                    [t]
                {
                    // Ended already (a worker step's failure ended it from its step run): nothing more to poll.
                    if (t->Delivered || !GpuTransactionCurrent(*t))
                        return true;
                    // A worker-stepped run: its Init on this thread first, once the step run computes (a ready or
                    // paused run acquires nothing; no segment starts before the Init), then the owner's side (Poll),
                    // which decides when the result may be made ready.
                    if (t->Pages && !t->Prepared)
                    {
                        if (!StepRunOf(*t).MayStart())
                            return t->Hooks.Poll && t->Hooks.Poll();
                        std::string why;
                        switch (t->Hooks.Prepare(why))
                        {
                        case GpuTransactionPrepare::Pending: return false;
                        case GpuTransactionPrepare::Failed:
                            FailGpuTransaction(*t, std::move(why)); // ends the step run too
                            return true;
                        case GpuTransactionPrepare::Ready: t->Prepared = true; break;
                        }
                    }
                    if (!t->OwnerSteps && !t->Pages && !t->Recorded)
                    {
                        auto driver = StepRunOf(*t);
                        if (!t->StepRun || !driver.MayStart())
                            return false;
                        t->Recorded = true;
                        driver.Recording(true); // until the device work ended (Ready) or the transaction did
                    }
                    return !t->Hooks.Poll || t->Hooks.Poll();
                },
                .ValidateBeforeApply =
                    [t]
                {
                    return MeshSupport::ValidateQueuedJob(t->Abandoned, GpuTransactionCurrent(*t));
                },
                .PublishCompletion =
                    [t](KernelEventBus&, const JobResultEnvelope&)
                {
                    t->RunJobLive = false;
                    if (t->Delivered)
                        return false;
                    t->Hooks.CompleteRun();
                    if (t->Phase == EditorGpuTransactionPhase::ReadyToAccept &&
                        (t->OwnerSteps || t->Pages
                                ? t->CommitWanted
                                : t->Execution.AutoAccept != StepRunAutoAccept::Never && AutoAcceptAllowed(*t)))
                    {
                        // Nothing waits for a user here: a refused automatic Accept ends the run.
                        if (auto refused = GpuTransactionAcceptRefusal(*t))
                            FinishGpuTransaction(*t,
                                refused->Status == EditorCommandStatus::StaleEntity
                                    ? EditorGpuTransactionPhase::Discarded
                                    : EditorGpuTransactionPhase::Failed,
                                refused->Status,
                                std::move(refused->Message));
                        else
                            (void)BeginGpuTransactionAccept(t);
                    }
                    // Published only while the transaction goes on; a run that ended here (failed,
                    // no preview, refused automatic Accept) already delivered its terminal result.
                    return t->Phase == EditorGpuTransactionPhase::ReadyToAccept ||
                        t->Phase == EditorGpuTransactionPhase::Accepting;
                },
                .FinalizeUnpublishedOnMainThread =
                    [t]
                {
                    t->RunJobLive = false;
                    Finalize(*t);
                }};
            const JobToken token = t->Context.JobCommands.Submit(std::move(job), t->Identity);
            t->RunJobLive = token.IsValid();
            return token;
        }

        // The step run's restart initializer, called once nothing of the run is in flight: the device result and
        // this run's ring generations go (freed after their completions, as a discard frees them; observation returns
        // to the canonical slot), the typed state of a generation is reset, and the one step records again from the
        // captured inputs, in a Run job of the same run when the last one already published. Refused (nothing
        // changes) once the transaction ended or its Accept started, or when the captured inputs changed.
        bool RestartGpuTransaction(const GpuTransactionHandle& t, std::string& diagnostic)
        {
            if (t->Delivered || t->Abandoned || t->Publishing || t->Phase == EditorGpuTransactionPhase::Accepting)
            {
                diagnostic = t->Label + " cannot restart: it ended or its Accept started.";
                return false;
            }
            if (!GpuTransactionCurrent(*t))
            {
                diagnostic = "The inputs changed since the run; discard the result and start again.";
                return false;
            }
            if (t->OwnerSteps || (t->Pages && t->Phase == EditorGpuTransactionPhase::Running))
            {
                if (t->Phase != EditorGpuTransactionPhase::Running || t->CommitWanted)
                {
                    diagnostic = "The GPU transaction no longer computes.";
                    return false;
                }
                if (!t->Hooks.Restart(diagnostic))
                    return false;
                // A worker-stepped run keeps its leased capacity; its Init runs again for the new generation (the
                // step run starts no segment before it), and its pages may be submitted again.
                if (t->Pages)
                {
                    t->Prepared = false;
                    t->StepCompleted.store(false, std::memory_order_relaxed); // nothing in flight
                    t->Pages->ClearAbort();
                }
                return true;
            }
            if (!t->Hooks.Restart(diagnostic))
                return false;
            t->Gpu.reset();
            StampWorkerSlots(*t);
            if (t->Pages)
            {
                t->Prepared = false;
                t->StepCompleted.store(false, std::memory_order_relaxed);
                t->Pages->ClearAbort();
            }
            for (std::size_t i = 0; i < t->RingCount; ++i)
            {
                auto& ring = t->Rings[i];
                ring.Back.reset();
                ring.Readback.reset();
                if (t->Residency && ring.Generation)
                    (void)t->Residency->Discard(ring.Key, ring.Generation);
                ring.Generation = 0u;
            }
            t->Deferrals = 0u;
            t->Recorded = false;
            t->Phase = EditorGpuTransactionPhase::Running;
            if (t->RunJobLive || SubmitRunJob(t).IsValid())
                return true;
            diagnostic = MeshSupport::QueuedJobRejectedMessage(t->JobLabel);
            FailGpuTransaction(*t, diagnostic); // ends the step run too
            return false;
        }

        // The step run of a queued Run job, a one-shot: one step (its device work), no live preview copy, ready or
        // running at the start, Accept through BeginGpuTransactionAccept (answered when the transaction ends),
        // Discard through DiscardGpuTransaction, Restart when the typed owner resets its generation. Its holder
        // joins the run without naming the output, so output-busy refusals and run progress stay the Run job's.
        bool StartStepRun(const GpuTransactionHandle& t)
        {
            auto& steps = *t->Context.StepRuns;
            const std::weak_ptr<GpuTransactionCore> weak = t;
            const EditorJobIdentity control{.Run = t->RunToken, .RequestedDomain = t->Identity.RequestedDomain};
            StepRunCallbacks callbacks{.Commit =
                                           [weak](std::string& diagnostic)
                {
                    const auto core = weak.lock();
                    if (!core)
                    {
                        diagnostic = "The GPU transaction ended.";
                        return StepRunCommit::Refused;
                    }
                    if ((core->OwnerSteps || core->Pages) && core->Phase == EditorGpuTransactionPhase::Running)
                    {
                        core->CommitWanted = true;
                        return StepRunCommit::Pending;
                    }
                    if (auto refused = GpuTransactionAcceptRefusal(*core))
                    {
                        diagnostic = std::move(refused->Message);
                        return StepRunCommit::Refused;
                    }
                    if (BeginGpuTransactionAccept(core))
                        return StepRunCommit::Pending;
                    diagnostic = core->Label + " Accept could not start; previous output retained.";
                    return StepRunCommit::Refused;
                },
                // A one-shot's step run holds its transaction until the run ends (this callable goes then), so a
                // run started without a caller that keeps the handle (an agent's start) still waits for Accept or
                // Discard; an owner-stepped transaction stays its owner's.
                .Discard =
                    [weak, held = t->OwnerSteps ? GpuTransactionHandle{} : t]
                {
                    const auto core = weak.lock();
                    if (!core)
                        return;
                    // A worker step that failed ended its step run first (its segment returned): only then is its reason
                    // read; a discard while a step runs reads nothing the step writes.
                    if (core->Pages)
                    {
                        const auto run = core->Context.StepRuns->Observe(core->StepRun);
                        if (run && run->Phase == StepRunPhase::Failed && !run->WorkerInFlight &&
                            !core->WorkerFailure.empty())
                        {
                            FailGpuTransaction(*core, core->WorkerFailure);
                            return;
                        }
                    }
                    DiscardGpuTransaction(
                        *core, EditorCommandStatus::StaleEntity, core->Label + " discarded; previous output retained.");
                }};
            // ARCH-023: a worker-stepped run's steps (and live preview) run on its worker. They hold the transaction
            // until the step run drops its callables (on the main thread, once no segment runs). A step that did not
            // fail marks the transaction after its last write (GpuTransactionStepCompleted).
            if (t->Pages)
            {
                callbacks.Step = [t, step = t->Hooks.Step](StepRunMetric& metric)
                {
                    const auto outcome = step(metric);
                    if (outcome != StepOutcome::Failed)
                        t->StepCompleted.store(true, std::memory_order_release);
                    return outcome;
                };
                if (t->Hooks.StepBatch)
                    callbacks.StepBatch = [t, batch = t->Hooks.StepBatch](
                                              StepRunMetric& metric, std::uint32_t allowance, std::uint32_t& completed)
                    {
                        const auto outcome = batch(metric, allowance, completed);
                        if (outcome != StepOutcome::Failed)
                            t->StepCompleted.store(true, std::memory_order_release);
                        return outcome;
                    };
                if (t->Hooks.Preview)
                    callbacks.Preview = [t, preview = t->Hooks.Preview](StepPreview& copy)
                    {
                        return preview(copy);
                    };
                if (t->Hooks.Show)
                    callbacks.Show = [weak](const StepPreview& copy)
                    {
                        const auto core = weak.lock();
                        return core && !GpuTransactionTerminal(*core) ? core->Hooks.Show(copy) : StepRunShow::Never;
                    };
                callbacks.Prepared = [weak]
                {
                    const auto core = weak.lock();
                    return core && core->Prepared;
                };
                // The settled end (no step runs, the holder finished): a release that waited for the worker now
                // returns its workspace and index references, also while a caller keeps the finished handle.
                callbacks.Finished = [weak](const StepRunObservation&)
                {
                    if (const auto core = weak.lock())
                        ReleaseTyped(*core);
                };
            }
            if (t->Hooks.Restart)
                callbacks.Restart = [weak](std::string& diagnostic)
                {
                    const auto core = weak.lock();
                    if (core)
                        return RestartGpuTransaction(core, diagnostic);
                    diagnostic = "The GPU transaction ended.";
                    return false;
                };
            // A batch's automatic Accept is the run's accept-at-end (the core's own Accept is then the run's one
            // Commit).
            const auto execution = !t->OwnerSteps && t->Kind == GpuTransactionKind::OneShot
                ? OneShotExecution(t->Execution)
                : t->Execution;
            std::string refusal;
            t->StepRun = steps.Start({.Label = t->JobLabel, .Owner = "Runtime.GpuTransaction", .Execution = execution},
                std::move(callbacks),
                EditorStepRunJobs(t->Context.JobCommands, control, std::make_shared<JobToken>()),
                refusal);
            return t->StepRun != 0u;
        }
    }

    // ---- the front readback primitive (GpuFront.hpp) ----
    bool BeginGpuFrontReadback(const EditorProcessingContext& ctx, Graphics::GpuPropertyResidency& residency,
                               const Graphics::GpuPropertyKey& key, const std::shared_ptr<GpuFrontReadback>& r)
    {
        const auto front = residency.HasRing(key) ? residency.Front(key) : std::nullopt;
        if (!front || !ctx.Device || !ctx.SpatialIndices) return false;
        r->Done = r->Failed = false;
        r->Lease = front;
        const auto bytes = front->Bytes;
        r->Bytes.assign(std::size_t(bytes), std::byte{0});
        // The recorder and the sink hold the front's lease, so the slot is neither rewritten
        // nor freed until the bytes landed, whatever happens to the owner meanwhile; the
        // recorder also registers the frame it records in.
        const auto record = [r, residency = &residency, device = ctx.Device, buffer = front->Buffer,
                             lease = front->Lease](RHI::ICommandContext& commands) -> RHI::BufferHandle {
            if (r->Abandoned) return {};
            residency->NoteUse(buffer, device->GetGlobalFrameNumber());
            commands.BufferBarrier(buffer, RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite | RHI::MemoryAccess::TransferRead,
                                   RHI::MemoryAccess::TransferRead);
            return buffer;
        };
        // One readback in the property's own bytes, at once on the compute lane; a refusal fails
        // the readback (nothing was submitted).
        const auto token = ctx.Device->SubmitComputeReadback(record, bytes,
            RHI::ReadbackSink::Invoke([r, lease = front->Lease](std::span<const std::byte> data) {
                if (data.size() == r->Bytes.size()) std::memcpy(r->Bytes.data(), data.data(), data.size());
                else r->Failed = true;
                r->Done = true;
            }));
        if (token.IsValid()) residency.AddCompletion(front->Buffer, token, bytes);
        else
            r->Failed = r->Done = true;
        return true;
    }

    bool PollGpuFrontReadback(GpuFrontReadback& r)
    {
        return r.Done;
    }

    // ---- the lifecycle ----
    bool GpuTransactionCurrent(const GpuTransactionCore& t)
    {
        // A detached editor session publishes nothing: every Accept (a panel's, run_control's, a batch's automatic
        // one) and the Run job's readiness pass this one check.
        if (t.Abandoned || (t.Context.AttachmentActive && !t.Context.AttachmentActive()) ||
            !EditorProcessingContextWorldCurrent(t.Context))
            return false;
        if (t.Residency)
            for (std::size_t i = 0; i < t.RingCount; ++i)
                if (t.Rings[i].Generation && t.Residency->RingGeneration(t.Rings[i].Key) != t.Rings[i].Generation)
                    return false;
        return !t.Hooks.Current || t.Hooks.Current();
    }

    bool GpuTransactionTerminal(const GpuTransactionCore& t) noexcept
    {
        return t.Phase == EditorGpuTransactionPhase::Applied || t.Phase == EditorGpuTransactionPhase::Discarded ||
               t.Phase == EditorGpuTransactionPhase::Failed;
    }

    GpuRingAcquisition AcquireGpuTransactionBack(GpuTransactionCore& t, const std::size_t index, const entt::entity entity,
                                                 const GeometryPropertyRef& ref, const std::uint32_t count, const std::uint32_t depth)
    {
        auto& ring = t.Rings[index];
        if (ring.Back) return GpuRingAcquisition::Ready;
        auto& residency = *t.Residency;
        // Another output identity may share this property: never write into a ring this run
        // did not create; wait for it to end, or fail, as the caller decides.
        if (!ring.Generation && residency.HasRing(ring.Key)) return GpuRingAcquisition::Foreign;
        ring.Back = AcquireGpuPropertyOutput(residency, t.Context.World, entity, ref, count, depth);
        if (!ring.Back) return GpuRingAcquisition::Deferred;
        ring.Generation = residency.RingGeneration(ring.Key);
        return GpuRingAcquisition::Ready;
    }

    bool GpuTransactionDeferralsExhausted(GpuTransactionCore& t) noexcept
    {
        return ++t.Deferrals >= kGpuTransactionMaxDeferrals;
    }

    void FinishGpuTransaction(GpuTransactionCore& t, const EditorGpuTransactionPhase phase, const EditorCommandStatus status,
                              std::string message)
    {
        Release(t);
        if (t.Delivered) return; // the result is frozen once delivered
        t.Delivered = true;
        const bool ran = phase == EditorGpuTransactionPhase::Applied || t.Phase == EditorGpuTransactionPhase::ReadyToAccept ||
                         t.Phase == EditorGpuTransactionPhase::Accepting;
        t.Phase = phase;
        // After the typed delivery, whose guarded sink may report the typed result first.
        const std::string diagnostic = message;
        if (t.Hooks.Deliver) t.Hooks.Deliver(status, std::move(message));
        ReportOutcome(t, ran, diagnostic);
        TellStepRun(t, diagnostic);
    }

    void FailGpuTransaction(GpuTransactionCore& t, std::string message)
    {
        FinishGpuTransaction(t, EditorGpuTransactionPhase::Failed, EditorCommandStatus::GeometryProcessingFailed, std::move(message));
    }

    void ReadyGpuTransaction(GpuTransactionCore& t)
    {
        if (t.Delivered) return;
        t.Phase = EditorGpuTransactionPhase::ReadyToAccept;
        ReportOutcome(t, true, t.Label + " result awaits Accept or Discard.");
        // A one-shot's one step completed: the step run ends converged and waits for Accept or Discard. An
        // owner-stepped transaction counted its steps itself and is ready only for its Commit.
        if (t.OwnerSteps || t.Pages) // a worker-stepped run's step run counted its steps itself
            return;
        auto driver = StepRunOf(t);
        (void)driver.Stepped({}, StepOutcome::Converged);
        driver.Recording(false);
    }

    std::uint32_t GpuTransactionStepAllowance(const GpuTransactionCore& t)
    {
        return StepRunOf(t).Allowance();
    }

    void GpuTransactionRecording(const GpuTransactionCore& t, const bool inFlight)
    {
        StepRunOf(t).Recording(inFlight);
    }

    bool GpuTransactionStepped(const GpuTransactionCore& t,
        const std::uint32_t count,
        const double metric,
        const bool converged,
        const bool failed)
    {
        return StepRunOf(t).Stepped({.Values = {metric, 0.0, 0.0, 0.0}},
            failed          ? StepOutcome::Failed
                : converged ? StepOutcome::Converged
                            : StepOutcome::Continue,
            count);
    }

    bool GpuTransactionMayPreview(const GpuTransactionCore& t)
    {
        return StepRunOf(t).MayPreview();
    }

    bool GpuTransactionMayPreviewAt(const GpuTransactionCore& t, const std::uint32_t step)
    {
        return StepRunOf(t).MayPreviewAt(step);
    }

    void GpuTransactionPreviewed(const GpuTransactionCore& t)
    {
        StepRunOf(t).Previewed();
    }

    void RestartGpuTransactionRings(GpuTransactionCore& t)
    {
        StampWorkerSlots(t); // a worker page may have written a write slot
        for (std::size_t i = 0; i < t.RingCount; ++i)
        {
            auto& ring = t.Rings[i];
            ring.Back.reset();
            if (ring.Readback)
            {
                ring.Readback->Abandoned = true;
                ring.Readback->Lease.reset();
                ring.Readback.reset();
            }
            if (t.Residency && ring.Generation)
                (void)t.Residency->Discard(ring.Key, ring.Generation);
            ring.Generation = 0u;
        }
    }

    bool GpuTransactionWorkerBusy(const GpuTransactionCore& t)
    {
        const auto run = t.StepRun && t.Context.StepRuns ? t.Context.StepRuns->Observe(t.StepRun) : std::nullopt;
        return run && run->WorkerInFlight;
    }

    bool GpuTransactionStepCompleted(const GpuTransactionCore& t)
    {
        return t.StepCompleted.load(std::memory_order_acquire);
    }

    void StampGpuTransactionUses(const GpuTransactionCore& t, const std::span<const RHI::BufferHandle> buffers)
    {
        if (!t.Residency || !t.Context.Device || buffers.empty())
            return;
        const auto frame = t.Context.Device->GetGlobalFrameNumber();
        t.Residency->BeginSubmission();
        for (const auto buffer : buffers)
            if (buffer.IsValid())
                t.Residency->NoteUse(buffer, frame);
        t.Residency->EndSubmission(t.Context.Device->GetTransferQueue().ComputeFence());
    }

    std::optional<GpuTransactionRefusal> GpuTransactionStartRefusal(const GpuTransactionCore& t)
    {
        if (auto busy = MeshSupport::ActiveOutputJobRefusal(t.Context, t.Identity, t.JobLabel))
            return GpuTransactionRefusal{EditorCommandStatus::Pending, std::move(busy->Message), true};
        if (!t.Context.StepRuns)
            return GpuTransactionRefusal{EditorCommandStatus::InvalidProcessingParameters,
                t.Label + " needs the step-run service for its run controls; none is composed."};
        if (!t.Residency)
            return GpuTransactionRefusal{EditorCommandStatus::InvalidProcessingParameters, t.Label + " needs the GPU property residency."};
        for (std::size_t i = 0; i < t.RingCount; ++i)
            if (t.Residency->HasRing(t.Rings[i].Key))
                return GpuTransactionRefusal{EditorCommandStatus::InvalidProcessingParameters,
                                             "A GPU result for this output awaits Accept or Discard.", true};
        return std::nullopt;
    }

    JobToken SubmitGpuTransactionRun(const GpuTransactionHandle& t, std::string debugName)
    {
        // A GPU transaction is GPU work by definition; every stage carries the request.
        if (!t->Identity.RequestedDomain) t->Identity.RequestedDomain = EditorJobDomain::GpuCompute;
        t->RunJobName = std::move(debugName);
        const JobToken token = SubmitRunJob(t);
        if (!token.IsValid())
        {
            // The caller reports the rejection as the immediate answer; nothing is delivered.
            t->Delivered = true;
            t->Abandoned = true;
            t->Phase = EditorGpuTransactionPhase::Failed;
            Release(*t);
            return {};
        }
        t->RunToken = token;
        t->Identity.Run = token; // the Accept stage and a restarted Run job join this run
        if (!t->Context.StepRuns || !StartStepRun(t))
        {
            // Without its control the run may not go on (GpuTransactionStartRefusal refuses a start without the
            // service first): closed as a rejected submission (nothing delivered); the queued Run job finalizes as
            // abandoned.
            t->Delivered = true;
            t->Abandoned = true;
            t->Phase = EditorGpuTransactionPhase::Failed;
            Release(*t);
            if (t->Context.JobCommands.Cancel)
                (void)t->Context.JobCommands.Cancel(token);
            return {};
        }
        return token;
    }

    std::optional<GpuTransactionRefusal> GpuTransactionAcceptRefusal(const GpuTransactionCore& t, const bool withSink)
    {
        if (t.Phase == EditorGpuTransactionPhase::Accepting)
            return withSink ? GpuTransactionRefusal{EditorCommandStatus::InvalidProcessingParameters,
                                                    "Accept is already under way; its result goes to the caller that started it."}
                            : GpuTransactionRefusal{EditorCommandStatus::Pending, "Accept is already under way."};
        if (t.Phase != EditorGpuTransactionPhase::ReadyToAccept)
            return GpuTransactionRefusal{EditorCommandStatus::InvalidProcessingParameters, "No GPU result waits for Accept."};
        if (!GpuTransactionCurrent(t))
            return GpuTransactionRefusal{EditorCommandStatus::StaleEntity,
                                         "The inputs changed since the run; discard the result and run again."};
        // Its step run decides too: no Accept while a restart waits for its new generation.
        if (t.StepRun && !StepRunOf(t).MayCommit())
            return GpuTransactionRefusal{
                EditorCommandStatus::InvalidProcessingParameters, "The run is restarting; Accept its new result."};
        return std::nullopt;
    }

    bool BeginGpuTransactionAccept(const GpuTransactionHandle& t)
    {
        if (!t->TestFront)
            for (std::size_t i = 0; i < t->RingCount; ++i)
            {
                auto& ring = t->Rings[i];
                if (!ring.ReadBack) continue;
                ring.Readback = std::make_shared<GpuFrontReadback>();
                if (!t->Residency || !BeginGpuFrontReadback(t->Context, *t->Residency, ring.Key, ring.Readback))
                {
                    FailGpuTransaction(*t, "The GPU result is no longer resident; previous output retained.");
                    return false;
                }
            }
        t->Phase = EditorGpuTransactionPhase::Accepting;
        JobDesc accept{
            .DebugName = t->AcceptJobName, .Scope = t->Context.World,
            .Work = [](const JobCancellation&) { return JobResultEnvelope::Make(true); },
            .IsReadyToApply = [t] {
                if (!GpuTransactionCurrent(*t)) return true;
                bool ready = true;
                for (std::size_t i = 0; i < t->RingCount; ++i)
                    if (const auto& r = t->Rings[i].Readback) ready = PollGpuFrontReadback(*r) && ready;
                return ready;
            },
            .ValidateBeforeApply = [t] { return MeshSupport::ValidateQueuedJob(t->Abandoned, GpuTransactionCurrent(*t)); },
            .PublishCompletion = [t](KernelEventBus&, const JobResultEnvelope&) {
                if (t->Delivered) return false;
                // The controlling step run (the transaction's own, or an owner's bound through Hooks.Publishing)
                // decides at this boundary: a discard or cancel that won before it leaves nothing to publish; from
                // here the publication decides the run's end.
                if (!StepRunOf(*t).Publishing() || (t->Hooks.Publishing && !t->Hooks.Publishing()))
                {
                    DiscardGpuTransaction(*t,
                        EditorCommandStatus::StaleEntity,
                        t->Label + " discarded before its publication; previous output retained.");
                    return false;
                }
                // History observers run synchronously inside the publication; a Discard they
                // issue is ignored (`DiscardGpuTransaction`).
                t->Publishing = true;
                t->Hooks.CompleteAccept();
                t->Publishing = false;
                return t->Phase == EditorGpuTransactionPhase::Applied;
            },
            .FinalizeUnpublishedOnMainThread = [t] { Finalize(*t); }};
        t->AcceptToken = t->Context.JobCommands.Submit(std::move(accept), t->Identity);
        if (!t->AcceptToken.IsValid())
        {
            FailGpuTransaction(*t, MeshSupport::QueuedJobRejectedMessage(t->JobLabel, "Accept"));
            return false;
        }
        // Every Accept (the step run's, a typed one, a batch's automatic one) is the step run's one Commit.
        (void)StepRunOf(*t).Committing();
        if (t->Hooks.Accepting) t->Hooks.Accepting();
        return true;
    }

    void DiscardGpuTransaction(GpuTransactionCore& t, const EditorCommandStatus status, std::string message)
    {
        if (t.Publishing || GpuTransactionTerminal(t)) return;
        // A job still queued finalizes as cancelled on its next drain; the rings go now (freed
        // after their completions), so observation returns to the canonical slot at once.
        t.Abandoned = true;
        FinishGpuTransaction(t, EditorGpuTransactionPhase::Discarded, status, std::move(message));
    }
}
}

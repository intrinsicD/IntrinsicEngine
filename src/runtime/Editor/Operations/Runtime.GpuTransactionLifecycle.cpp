// The shared two-phase GPU Run/Accept transaction lifecycle (RUNTIME-311); see the header.
// An ordinary translation unit so no method family depends on another family module.
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
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

#include "Editor/internal/Runtime.EditorProcessingAccess.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.JobFailure.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.GpuFront.hpp"
#include "Editor/Operations/Runtime.GpuTransactionLifecycle.hpp"

extern "C++"
{
namespace Extrinsic::Runtime::GeometryProcessingDetail
{
    bool EditorProcessingContextWorldCurrent(const EditorProcessingContext&);

    namespace
    {
        void Release(GpuTransactionCore& t)
        {
            if (t.Hooks.Release) t.Hooks.Release();
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
    }

    bool GpuTransactionCurrent(const GpuTransactionCore& t)
    {
        if (t.Abandoned || !EditorProcessingContextWorldCurrent(t.Context)) return false;
        if (t.Residency)
            for (std::size_t i = 0; i < t.RingCount; ++i)
                if (t.Rings[i].Generation && t.Residency->RingGeneration(t.Rings[i].Key) != t.Rings[i].Generation)
                    return false;
        return !t.Hooks.Current || t.Hooks.Current();
    }

    bool GpuTransactionWorkReleasable(const GpuTransactionCore& t) noexcept
    {
        return !t.Gpu || t.Gpu->State == SpatialQueryState::Ready;
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
        t.Phase = phase;
        if (t.Hooks.Deliver) t.Hooks.Deliver(status, std::move(message));
    }

    void FailGpuTransaction(GpuTransactionCore& t, std::string message)
    {
        FinishGpuTransaction(t, EditorGpuTransactionPhase::Failed, EditorCommandStatus::GeometryProcessingFailed, std::move(message));
    }

    void ReadyGpuTransaction(GpuTransactionCore& t) noexcept
    {
        if (!t.Delivered) t.Phase = EditorGpuTransactionPhase::ReadyToAccept;
    }

    std::optional<GpuTransactionRefusal> GpuTransactionStartRefusal(const GpuTransactionCore& t, const std::string_view jobLabel)
    {
        if (auto busy = MeshSupport::ActiveOutputJobRefusal(t.Context, t.Identity, jobLabel))
            return GpuTransactionRefusal{EditorCommandStatus::Pending, std::move(busy->Message)};
        if (!t.Residency)
            return GpuTransactionRefusal{EditorCommandStatus::InvalidProcessingParameters, t.Label + " needs the GPU property residency."};
        for (std::size_t i = 0; i < t.RingCount; ++i)
            if (t.Residency->HasRing(t.Rings[i].Key))
                return GpuTransactionRefusal{EditorCommandStatus::InvalidProcessingParameters,
                                             "A GPU result for this output awaits Accept or Discard."};
        return std::nullopt;
    }

    JobToken SubmitGpuTransactionRun(const GpuTransactionHandle& t, std::string debugName)
    {
        JobDesc job{
            .DebugName = std::move(debugName), .Scope = t->Context.World, .Kind = RuntimeTaskKinds::GeometryProcess,
            .Work = [](const JobCancellation&) { return JobResultEnvelope::Make(true); },
            .IsReadyToApply = [t] { return !GpuTransactionCurrent(*t) || !t->Hooks.Poll || t->Hooks.Poll(); },
            .ValidateBeforeApply = [t] { return MeshSupport::ValidateQueuedJob(t->Abandoned, GpuTransactionCurrent(*t)); },
            .PublishCompletion = [t](KernelEventBus&, const JobResultEnvelope&) {
                if (t->Delivered) return false;
                t->Hooks.CompleteRun();
                if (t->Phase == EditorGpuTransactionPhase::ReadyToAccept && t->AutoAccept)
                {
                    // Nothing waits for a user here: a refused automatic Accept ends the run.
                    if (auto refused = GpuTransactionAcceptRefusal(*t))
                        FinishGpuTransaction(*t, refused->Status == EditorCommandStatus::StaleEntity
                                                     ? EditorGpuTransactionPhase::Discarded : EditorGpuTransactionPhase::Failed,
                                             refused->Status, std::move(refused->Message));
                    else
                        (void)BeginGpuTransactionAccept(t);
                }
                // Published only while the transaction goes on; a run that ended here (failed,
                // no preview, refused automatic Accept) already delivered its terminal result.
                return t->Phase == EditorGpuTransactionPhase::ReadyToAccept || t->Phase == EditorGpuTransactionPhase::Accepting;
            },
            .FinalizeUnpublishedOnMainThread = [t] { Finalize(*t); }};
        const JobToken token = t->Context.JobCommands.Submit(std::move(job), t->Identity);
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
        t->Identity.Run = token; // the Accept stage joins this run
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
            .DebugName = t->AcceptJobName, .Scope = t->Context.World, .Kind = RuntimeTaskKinds::GeometryProcess,
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
            FailGpuTransaction(*t, t->Label + " Accept submission rejected; previous output retained.");
            return false;
        }
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

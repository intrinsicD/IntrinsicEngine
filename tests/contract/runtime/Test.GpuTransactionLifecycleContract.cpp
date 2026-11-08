// RUNTIME-311: the shared two-phase GPU Run/Accept transaction lifecycle, driven through a
// minimal typed transaction. Pins what every method transaction inherits: exactly-once
// delivery, cancellation of a parked Run, the Accept stage joining the run, stale-ring
// detection that never releases a successor's ring, Discard ignored while Accept publishes,
// and the outcome of a refused automatic Accept; and (ARCH-023) the one-shot step run every
// transaction with a Run job is: ready, Step 1, Run, Stop, Restart and Discard through its controls.
#include <array>
#include <atomic>
#include <mutex>
#include <utility>
#include <chrono>
#include <thread>
#include <cstdint>
#include <functional>
#include <span>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <gtest/gtest.h>
#include "MockRHI.hpp"
#include "OneShotLifecycleScript.hpp"
#include "PointDomainFixture.hpp"
#include "SandboxEditorJobHarness.hpp"
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.GpuPropertyResidency;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.EditorProcessing;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.GpuPropertyBinding;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.StepRuns;
import Extrinsic.RHI.Handles; // Runtime.GpuTransactionLifecycle.hpp names RHI::BufferHandle
import Geometry.Properties;
// Test seam: the lifecycle is internal to the editor operations.
#include "Editor/Operations/Runtime.GeometryProcessingOperations.GpuFront.hpp"
#include "Editor/Operations/Runtime.GpuTransactionLifecycle.hpp"
#include "Services/Runtime.WorkerGpuPages.hpp"

namespace R = Extrinsic::Runtime;
namespace G = Extrinsic::Graphics;
namespace GP = Extrinsic::Runtime::GeometryProcessingDetail;

namespace
{
    struct Fake : std::enable_shared_from_this<Fake>
    {
        GP::GpuTransactionCore Core{};
        bool InputsCurrent{true}, DeviceDone{false}, StaleAfterRun{false};
        unsigned Polls{}, Publications{}, Deliveries{};
        R::EditorCommandStatus Status{};
        std::string Message{};
        std::function<void()> DuringPublication{};
    };

    struct Harness
    {
        Extrinsic::ECS::Scene::Registry Scene;
        Extrinsic::Tests::MockDevice Device;
        G::GpuPropertyResidency Residency{Device};
        R::EditorProcessingContext Context;
        Extrinsic::Tests::EditorJobHarness Jobs;
        entt::entity Entity{};
        R::GeometryPropertyRef Output{.Domain = R::GeometryElementDomain::PointCloudPoint, .Name = "out",
                                      .ValueKind = Geometry::PropertyValueKind::Float};

        Harness()
        {
            Entity = Intrinsic::Tests::MakePointDomainSource(Scene, R::GeometryElementDomain::PointCloudPoint);
            Context.Scene = &Scene;
            Context.Device = &Device;
            Device.TransferQueue.AcceptBufferUploads = true;
            Jobs.Attach(Context);
        }

        std::shared_ptr<Fake> Make(const bool autoAccept = false, const bool ring = false)
        {
            auto f = std::make_shared<Fake>();
            auto& t = f->Core;
            t.Context = Context;
            t.Identity = {.EntityId = 7u, .Scope = R::EditorJobScope::PointCloudPoint,
                          .OutputSemantic = R::GeometryPresentationSlotSemantic::ScalarField, .OutputName = "out"};
            t.Label = "Fake transaction";
            t.AcceptJobName = "Fake accept";
            t.Execution.AutoAccept = autoAccept ? R::StepRunAutoAccept::End : R::StepRunAutoAccept::Never;
            t.Execution.Start = R::StepRunStart::Run; // this fixture exercises an already running transaction
            t.TestFront = true;
            if (ring)
            {
                t.Residency = &Residency;
                t.Rings[0] = {.Key = R::MakeGpuPropertyKey(Context.World, Entity, Output)};
                t.RingCount = 1;
            }
            auto* raw = f.get();
            t.Hooks = {
                .Current = [raw] { return raw->InputsCurrent; },
                .Poll = [raw] { ++raw->Polls; return raw->DeviceDone; },
                .CompleteRun = [raw] {
                    GP::ReadyGpuTransaction(raw->Core);
                    if (raw->StaleAfterRun) raw->InputsCurrent = false;
                },
                .CompleteAccept = [raw] {
                    ++raw->Publications;
                    if (raw->DuringPublication) raw->DuringPublication();
                    GP::FinishGpuTransaction(raw->Core, R::EditorGpuTransactionPhase::Applied, R::EditorCommandStatus::Applied, "applied");
                },
                .Release = [] {},
                .Deliver = [raw](R::EditorCommandStatus status, std::string message) {
                    ++raw->Deliveries;
                    raw->Status = status;
                    raw->Message = std::move(message);
                }};
            return f;
        }

        // Drains until `token` parks in AwaitingApply (its readiness gate keeps refusing).
        bool Parked(const R::JobToken token)
        {
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds{5};
            while (std::chrono::steady_clock::now() < until)
            {
                (void)Jobs.Jobs().DrainCompletions(Jobs.Events());
                if (Jobs.Jobs().GetState(token) == R::JobState::AwaitingApply) return true;
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
            }
            return false;
        }

        // Drains until `done`: a transaction waiting for Accept keeps its step run's holder job active (ARCH-023),
        // so "every job terminal" is not the state to wait for then.
        bool DrainUntil(const std::function<bool()>& done)
        {
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds{5};
            while (!done() && std::chrono::steady_clock::now() < until)
            {
                (void)Jobs.Jobs().DrainCompletions(Jobs.Events());
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
            }
            return done();
        }

        std::optional<R::EditorJobRecord> Job(const R::JobToken token)
        {
            for (const auto& job : Context.JobCommands.SnapshotAll())
                if (job.Token == token) return job;
            return std::nullopt;
        }
    };
}

// A Run whose device work never ends parks in AwaitingApply; a cancel through the editor job
// surface ends it exactly once as stale, with nothing published, and nothing polls it again.
TEST(GpuTransactionLifecycle, CancelOfAParkedRunFinalizesOnce)
{
    Harness h;
    auto f = h.Make();
    const auto run = GP::SubmitGpuTransactionRun(GP::GpuTransactionOf(f), "Fake run");
    ASSERT_TRUE(run.IsValid());
    EXPECT_EQ(f->Core.Identity.Run, run) << "later stages join the run";
    ASSERT_TRUE(h.Parked(run));
    EXPECT_GT(f->Polls, 0u);
    EXPECT_EQ(h.Context.JobCommands.Cancel(run), R::EditorJobCancelStatus::Requested);
    ASSERT_TRUE(h.Jobs.DrainUntilTerminal());
    EXPECT_EQ(h.Jobs.Jobs().GetState(run), R::JobState::Cancelled);
    EXPECT_EQ(f->Deliveries, 1u);
    EXPECT_EQ(f->Status, R::EditorCommandStatus::StaleEntity);
    EXPECT_EQ(f->Core.Phase, R::EditorGpuTransactionPhase::Discarded);
    EXPECT_TRUE(f->Core.Abandoned);
    EXPECT_EQ(f->Publications, 0u);
    GP::DiscardGpuTransaction(f->Core, R::EditorCommandStatus::NoChange, "discarded");
    EXPECT_EQ(f->Deliveries, 1u) << "a terminal transaction delivers nothing more";
}

// The Accept stage carries the run; cancelling it while its readback is pending ends the
// transaction once, as stale, without publishing.
TEST(GpuTransactionLifecycle, AcceptJoinsTheRunAndItsCancelFinalizesOnce)
{
    Harness h;
    auto f = h.Make();
    f->DeviceDone = true;
    const auto run = GP::SubmitGpuTransactionRun(GP::GpuTransactionOf(f), "Fake run");
    ASSERT_TRUE(h.DrainUntil(
        [&]
        {
            return h.Jobs.Jobs().GetState(run) == R::JobState::Published;
        }));
    ASSERT_EQ(f->Core.Phase, R::EditorGpuTransactionPhase::ReadyToAccept);
    EXPECT_FALSE(GP::GpuTransactionAcceptRefusal(f->Core).has_value());
    ASSERT_TRUE(GP::BeginGpuTransactionAccept(GP::GpuTransactionOf(f)));
    const auto accept = h.Job(f->Core.AcceptToken);
    ASSERT_TRUE(accept.has_value());
    EXPECT_EQ(accept->Identity.Run, run);
    EXPECT_EQ(accept->Name, "Fake accept");
    const auto pending = GP::GpuTransactionAcceptRefusal(f->Core);
    ASSERT_TRUE(pending.has_value());
    EXPECT_EQ(pending->Status, R::EditorCommandStatus::Pending);
    EXPECT_EQ(GP::GpuTransactionAcceptRefusal(f->Core, true)->Status, R::EditorCommandStatus::InvalidProcessingParameters);
    EXPECT_EQ(h.Context.JobCommands.Cancel(f->Core.AcceptToken), R::EditorJobCancelStatus::Requested);
    ASSERT_TRUE(h.Jobs.DrainUntilTerminal());
    EXPECT_EQ(f->Deliveries, 1u);
    EXPECT_EQ(f->Status, R::EditorCommandStatus::StaleEntity);
    EXPECT_EQ(f->Publications, 0u);
}

// A refused automatic Accept ends the transaction: Discarded with StaleEntity when the inputs
// changed, delivered once, and no Accept job is queued.
TEST(GpuTransactionLifecycle, RefusedAutomaticAcceptOfStaleInputsEndsDiscardedOnce)
{
    Harness h;
    auto f = h.Make(true);
    f->DeviceDone = true;
    f->StaleAfterRun = true;
    const auto run = GP::SubmitGpuTransactionRun(GP::GpuTransactionOf(f), "Fake run");
    ASSERT_TRUE(run.IsValid());
    ASSERT_TRUE(h.Jobs.DrainUntilTerminal());
    EXPECT_NE(h.Jobs.Jobs().GetState(run), R::JobState::Published) << "the run ended here; it did not publish";
    EXPECT_EQ(f->Deliveries, 1u);
    EXPECT_EQ(f->Status, R::EditorCommandStatus::StaleEntity) << f->Message;
    EXPECT_EQ(f->Core.Phase, R::EditorGpuTransactionPhase::Discarded);
    EXPECT_FALSE(f->Core.AcceptToken.IsValid());
    EXPECT_EQ(f->Publications, 0u);
}

// A rejected submission is the caller's immediate answer: the Run closes without delivery;
// a rejected Accept delivers its failure exactly once.
TEST(GpuTransactionLifecycle, RejectedSubmissionsDeliverAtMostOnce)
{
    Harness h;
    const auto submit = h.Context.JobCommands.Submit;
    h.Context.JobCommands.Submit = [](R::JobDesc, R::EditorJobIdentity) { return R::JobToken{}; };
    auto rejected = h.Make();
    EXPECT_FALSE(GP::SubmitGpuTransactionRun(GP::GpuTransactionOf(rejected), "Fake run").IsValid());
    EXPECT_EQ(rejected->Deliveries, 0u);
    EXPECT_EQ(rejected->Core.Phase, R::EditorGpuTransactionPhase::Failed);
    GP::DiscardGpuTransaction(rejected->Core, R::EditorCommandStatus::NoChange, "discarded");
    EXPECT_EQ(rejected->Deliveries, 0u);

    auto ready = h.Make();
    GP::ReadyGpuTransaction(ready->Core);
    EXPECT_FALSE(GP::BeginGpuTransactionAccept(GP::GpuTransactionOf(ready)));
    EXPECT_EQ(ready->Deliveries, 1u);
    EXPECT_EQ(ready->Status, R::EditorCommandStatus::GeometryProcessingFailed);
    EXPECT_EQ(ready->Core.Phase, R::EditorGpuTransactionPhase::Failed);
    h.Context.JobCommands.Submit = submit;
}

// History observers run inside Accept's publication; a Discard they issue is ignored.
TEST(GpuTransactionLifecycle, DiscardDuringPublicationIsIgnored)
{
    Harness h;
    auto f = h.Make();
    f->DuringPublication = [&] { GP::DiscardGpuTransaction(f->Core, R::EditorCommandStatus::NoChange, "discarded"); };
    GP::ReadyGpuTransaction(f->Core);
    ASSERT_TRUE(GP::BeginGpuTransactionAccept(GP::GpuTransactionOf(f)));
    ASSERT_TRUE(h.Jobs.DrainUntilTerminal());
    EXPECT_EQ(f->Publications, 1u);
    EXPECT_EQ(f->Deliveries, 1u);
    EXPECT_EQ(f->Status, R::EditorCommandStatus::Applied);
    EXPECT_EQ(f->Core.Phase, R::EditorGpuTransactionPhase::Applied);
}

// A run is stale once its ring is another generation, and ending it releases only its own
// generation: a successor's ring on the same key survives. A ring another run created is
// never acquired.
TEST(GpuTransactionLifecycle, StaleRingGenerationNeverReleasesTheReplacementRing)
{
    Harness h;
    auto f = h.Make(false, true);
    ASSERT_EQ(GP::AcquireGpuTransactionBack(f->Core, 0, h.Entity, h.Output, 4u, 2u), GP::GpuRingAcquisition::Ready);
    const auto key = f->Core.Rings[0].Key;
    const auto own = f->Core.Rings[0].Generation;
    ASSERT_NE(own, 0u);
    EXPECT_TRUE(GP::GpuTransactionCurrent(f->Core));
    f->Core.Rings[0].Back.reset();
    h.Residency.Discard(key); // another owner released it ...
    ASSERT_TRUE(R::AcquireGpuPropertyOutput(h.Residency, h.Context.World, h.Entity, h.Output, 4u, 2u)); // ... and took the key
    const auto successor = h.Residency.RingGeneration(key);
    ASSERT_NE(successor, own);
    EXPECT_FALSE(GP::GpuTransactionCurrent(f->Core));
    auto other = h.Make(false, true);
    EXPECT_EQ(GP::AcquireGpuTransactionBack(other->Core, 0, h.Entity, h.Output, 4u, 2u), GP::GpuRingAcquisition::Foreign);
    GP::DiscardGpuTransaction(f->Core, R::EditorCommandStatus::NoChange, "discarded");
    EXPECT_EQ(f->Deliveries, 1u);
    EXPECT_TRUE(h.Residency.HasRing(key));
    EXPECT_EQ(h.Residency.RingGeneration(key), successor);
    h.Residency.Discard(key);
}

// RUNTIME-317: every job of a transaction reports GpuCompute as its requested domain; the run
// resolves to GpuCompute once the device produced a result (ready, then applied), with the
// lifecycle's message as its diagnostic, on every job of the run.
TEST(GpuTransactionLifecycle, JobsReportTheGpuRequestAndTheResolvedOutcome)
{
    Harness h;
    auto f = h.Make();
    f->DeviceDone = true;
    const auto run = GP::SubmitGpuTransactionRun(GP::GpuTransactionOf(f), "Fake run");
    auto job = h.Job(run);
    ASSERT_TRUE(job.has_value());
    EXPECT_EQ(job->RequestedJobDomain, R::EditorJobDomain::GpuCompute);
    EXPECT_FALSE(job->ResolvedJobDomain.has_value()) << "unknown while the device works";
    EXPECT_TRUE(job->Diagnostic.empty());
    ASSERT_TRUE(h.DrainUntil(
        [&]
        {
            return h.Jobs.Jobs().GetState(run) == R::JobState::Published;
        }));
    job = h.Job(run);
    ASSERT_TRUE(job.has_value());
    EXPECT_EQ(job->ResolvedJobDomain, R::EditorJobDomain::GpuCompute);
    EXPECT_EQ(job->Diagnostic, "Fake transaction result awaits Accept or Discard.");

    ASSERT_TRUE(GP::BeginGpuTransactionAccept(GP::GpuTransactionOf(f)));
    ASSERT_TRUE(h.Jobs.DrainUntilTerminal());
    ASSERT_EQ(f->Core.Phase, R::EditorGpuTransactionPhase::Applied);
    for (const R::JobToken token : {run, f->Core.AcceptToken})
    {
        job = h.Job(token);
        ASSERT_TRUE(job.has_value());
        EXPECT_EQ(job->RequestedJobDomain, R::EditorJobDomain::GpuCompute);
        EXPECT_EQ(job->ResolvedJobDomain, R::EditorJobDomain::GpuCompute);
        EXPECT_EQ(job->Diagnostic, "applied");
    }
}

// A run cancelled before the device produced anything resolved to no domain; its diagnostic is
// the cancellation the caller received.
TEST(GpuTransactionLifecycle, RunCancelledBeforeAResultResolvesToNoDomain)
{
    Harness h;
    auto f = h.Make();
    const auto run = GP::SubmitGpuTransactionRun(GP::GpuTransactionOf(f), "Fake run");
    ASSERT_TRUE(h.Parked(run));
    EXPECT_EQ(h.Context.JobCommands.Cancel(run), R::EditorJobCancelStatus::Requested);
    ASSERT_TRUE(h.Jobs.DrainUntilTerminal());
    const auto job = h.Job(run);
    ASSERT_TRUE(job.has_value());
    EXPECT_EQ(job->RequestedJobDomain, R::EditorJobDomain::GpuCompute);
    EXPECT_FALSE(job->ResolvedJobDomain.has_value());
    EXPECT_EQ(job->Diagnostic, f->Message);
    EXPECT_FALSE(job->Diagnostic.empty());
}

// ARCH-023: with the step-run service a transaction with a Run job is a step run of one step (its device work), and
// run_control/run_observe control it as the transaction's own Accept and Discard do: run_control's Accept publishes
// once, a typed Accept is the run's one Commit, run_control's Discard and a cancel of the run's jobs end it
// unpublished. Every path delivers once and a second Accept is refused. The Accept stage still joins the Run job.
TEST(GpuTransactionLifecycle, AStepRunControlsTheTransactionAsItsOwnAcceptAndDiscardDo)
{
    enum class Path
    {
        RunControlAccept,
        TypedAccept,
        RunControlDiscard,
        CancelRun
    };
    for (const Path path : {Path::RunControlAccept, Path::TypedAccept, Path::RunControlDiscard, Path::CancelRun})
    {
        Harness h;
        auto& runs = h.Jobs.StepRuns(); // the harness composes the service as the Sandbox does
        ASSERT_EQ(h.Context.StepRuns, &runs);
        auto f = h.Make();
        f->DeviceDone = true;
        const auto run = GP::SubmitGpuTransactionRun(GP::GpuTransactionOf(f), "Fake run");
        ASSERT_TRUE(run.IsValid());
        const auto id = f->Core.StepRun;
        ASSERT_NE(id, 0u) << "the transaction is a step run";
        const auto observe = [&]
        {
            return *runs.Observe(id);
        };
        const auto drainUntil = [&](const std::function<bool()>& done)
        {
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds{5};
            while (!done() && std::chrono::steady_clock::now() < until)
            {
                (void)h.Jobs.Jobs().DrainCompletions(h.Jobs.Events());
                std::this_thread::sleep_for(std::chrono::milliseconds{1});
            }
            return done();
        };
        ASSERT_TRUE(drainUntil(
            [&]
            {
                return f->Core.Phase == R::EditorGpuTransactionPhase::ReadyToAccept &&
                    observe().Phase == R::StepRunPhase::Paused;
            }));
        EXPECT_EQ(observe().Steps, 1u) << "one logical step: the device work";
        EXPECT_EQ(observe().End, R::StepRunEnd::Converged);
        EXPECT_FALSE(observe().SegmentInFlight);
        EXPECT_EQ(f->Core.Identity.Run, run) << "the Accept stage joins the Run job's run";
        std::string refusal;
        switch (path)
        {
        case Path::RunControlAccept:
            ASSERT_TRUE(runs.Control(id, R::StepRunAction::Accept, 1u, refusal)) << refusal;
            EXPECT_FALSE(runs.Control(id, R::StepRunAction::Accept, 1u, refusal)) << "one Accept";
            break;
        case Path::TypedAccept:
            ASSERT_TRUE(GP::BeginGpuTransactionAccept(GP::GpuTransactionOf(f)));
            EXPECT_EQ(observe().Phase, R::StepRunPhase::Accepting) << "a typed Accept is the run's Commit";
            EXPECT_FALSE(runs.Control(id, R::StepRunAction::Accept, 1u, refusal)) << "one Accept";
            break;
        case Path::RunControlDiscard:
            ASSERT_TRUE(runs.Control(id, R::StepRunAction::Discard, 1u, refusal)) << refusal;
            break;
        case Path::CancelRun:
            EXPECT_GT(R::CancelEditorRuns(h.Context.JobCommands, std::span<const R::JobToken>{&run, 1u}).Requested, 0u)
                << "the run's control stage joined the run";
            break;
        }
        const bool accepted = path == Path::RunControlAccept || path == Path::TypedAccept;
        ASSERT_TRUE(drainUntil(
            [&]
            {
                return f->Deliveries == 1u && R::IsTerminal(observe().Phase);
            }));
        for (int i = 0; i < 10; ++i)
            (void)h.Jobs.Jobs().DrainCompletions(h.Jobs.Events());
        EXPECT_EQ(f->Deliveries, 1u);
        EXPECT_EQ(f->Publications, accepted ? 1u : 0u);
        EXPECT_EQ(
            f->Core.Phase, accepted ? R::EditorGpuTransactionPhase::Applied : R::EditorGpuTransactionPhase::Discarded);
        EXPECT_EQ(observe().Phase, accepted ? R::StepRunPhase::Committed : R::StepRunPhase::Discarded);
        EXPECT_FALSE(runs.Control(id, R::StepRunAction::Accept, 1u, refusal)) << "a terminal run accepts nothing";
    }
}

namespace
{
    // A transaction whose Poll acquires its output ring (as the typed owners do), recording each ring generation it
    // writes; Restart is the typed reset hook (counted).
    struct RingRun
    {
        std::shared_ptr<Fake> F{};
        std::vector<std::uint64_t> Generations{};
        unsigned Restarts{0u};
    };
    std::shared_ptr<RingRun> MakeRingRun(Harness& h, const bool autoAccept, const bool restartable)
    {
        auto r = std::make_shared<RingRun>();
        r->F = h.Make(autoAccept, true);
        auto* raw = r.get();
        r->F->Core.Hooks.Poll = [raw, &h]
        {
            auto& f = *raw->F;
            ++f.Polls;
            if (GP::AcquireGpuTransactionBack(f.Core, 0, h.Entity, h.Output, 4u, 2u) != GP::GpuRingAcquisition::Ready)
                return false;
            if (raw->Generations.empty() || raw->Generations.back() != f.Core.Rings[0].Generation)
                raw->Generations.push_back(f.Core.Rings[0].Generation);
            return f.DeviceDone;
        };
        if (restartable)
            r->F->Core.Hooks.Restart = [raw](std::string&)
            {
                ++raw->Restarts;
                return true;
            };
        return r;
    }
}

// ARCH-023: the common one-shot lifecycle on the shared core (OneShotLifecycleScript; the real owners run the same
// script in their GPU smokes). Started ready, nothing is recorded and no ring exists; Step 1 records once; Restart
// after that result drops the device result and the ring generation, queues a Run job that joins the same run and
// records again into a fresh generation once Run asks; Accept publishes that result once.
TEST(GpuTransactionLifecycle, AOneShotRunsTheCommonLifecycleAndRestartsIntoAFreshRingGeneration)
{
    Harness h;
    auto& runs = h.Jobs.StepRuns();
    const auto r = MakeRingRun(h, false, true);
    auto& f = *r->F;
    f.DeviceDone = true;
    f.Core.Execution.Start = R::StepRunStart::Ready;
    const auto run = GP::SubmitGpuTransactionRun(GP::GpuTransactionOf(r->F), "Fake run");
    ASSERT_TRUE(run.IsValid());
    const auto key = f.Core.Rings[0].Key;
    unsigned pollsAtRestart = 0u;
    Intrinsic::Tests::OneShotLifecycleScript script{.OnReadyHeld =
                                                        [&]
        {
            EXPECT_EQ(f.Polls, 0u) << "a ready run records no device work";
            EXPECT_FALSE(h.Residency.HasRing(key));
        },
        .OnFirstResult =
            [&]
        {
            EXPECT_EQ(f.Core.Phase, R::EditorGpuTransactionPhase::ReadyToAccept);
            EXPECT_EQ(r->Generations.size(), 1u);
            EXPECT_TRUE(h.Residency.HasRing(key));
        },
        .OnRestartedReady =
            [&]
        {
            EXPECT_EQ(r->Restarts, 1u);
            EXPECT_EQ(f.Core.Phase, R::EditorGpuTransactionPhase::Running);
            EXPECT_FALSE(h.Residency.HasRing(key)) << "the older generation's ring is gone";
            EXPECT_EQ(f.Publications, 0u);
            pollsAtRestart = f.Polls;
        },
        .OnRestartedResult =
            [&]
        {
            EXPECT_GT(f.Polls, pollsAtRestart);
            ASSERT_EQ(r->Generations.size(), 2u);
            EXPECT_NE(r->Generations[0], r->Generations[1]) << "a fresh ring generation";
            EXPECT_EQ(h.Residency.RingGeneration(key), r->Generations[1]);
        }};
    ASSERT_TRUE(h.DrainUntil(
        [&]
        {
            return script.Advance(runs, f.Core.StepRun);
        }));
    ASSERT_TRUE(h.DrainUntil(
        [&]
        {
            return f.Deliveries == 1u;
        }));
    EXPECT_EQ(f.Publications, 1u) << "one Accept, one publication";
    EXPECT_EQ(f.Core.Phase, R::EditorGpuTransactionPhase::Applied);
    std::vector<R::EditorJobRecord> restarted;
    for (const auto& job : h.Context.JobCommands.SnapshotAll())
        if (job.Name == "Fake run" && job.Token != run)
            restarted.push_back(job);
    ASSERT_EQ(restarted.size(), 1u) << "the restart queued the one step's Run job again";
    EXPECT_EQ(restarted.front().Identity.Run, run) << "the restarted Run job joins the same run";
}

// A Stop while the device work is in flight lets that one step finish at its boundary: the result waits for Accept,
// and a batch's automatic Accept does not happen (a stopped run never accepts by itself).
TEST(GpuTransactionLifecycle, AStopDuringTheDeviceWorkKeepsTheResultForAcceptWithoutTheAutomaticAccept)
{
    Harness h;
    auto& runs = h.Jobs.StepRuns();
    auto f = h.Make(true);
    const auto run = GP::SubmitGpuTransactionRun(GP::GpuTransactionOf(f), "Fake run");
    ASSERT_TRUE(h.Parked(run));
    ASSERT_GT(f->Polls, 0u) << "a running start records at once";
    const auto id = f->Core.StepRun;
    std::string refusal;
    ASSERT_TRUE(runs.Control(id, R::StepRunAction::Stop, 1u, refusal)) << refusal;
    EXPECT_EQ(runs.Observe(id)->Phase, R::StepRunPhase::Running) << "the step in flight reaches its boundary";
    f->DeviceDone = true;
    ASSERT_TRUE(h.DrainUntil(
        [&]
        {
            return f->Core.Phase == R::EditorGpuTransactionPhase::ReadyToAccept &&
                runs.Observe(id)->Phase == R::StepRunPhase::Paused;
        }));
    for (int i = 0; i < 10; ++i)
        (void)h.Jobs.Jobs().DrainCompletions(h.Jobs.Events());
    EXPECT_EQ(f->Core.Phase, R::EditorGpuTransactionPhase::ReadyToAccept) << "no automatic Accept after a Stop";
    EXPECT_FALSE(f->Core.AcceptToken.IsValid());
    EXPECT_EQ(runs.Observe(id)->End, R::StepRunEnd::Stopped);
    EXPECT_EQ(runs.Observe(id)->Steps, 1u);
    ASSERT_TRUE(runs.Control(id, R::StepRunAction::Accept, 1u, refusal)) << refusal;
    ASSERT_TRUE(h.DrainUntil(
        [&]
        {
            return f->Deliveries == 1u && R::IsTerminal(runs.Observe(id)->Phase);
        }));
    EXPECT_EQ(f->Publications, 1u);
    EXPECT_EQ(f->Core.Phase, R::EditorGpuTransactionPhase::Applied);
    EXPECT_EQ(runs.Observe(id)->Phase, R::StepRunPhase::Committed);
}

// A Discard while Accept's readback is pending (the Accept job held back) wins before the publication: nothing is
// published, the run ends Discarded once, and its ring is released.
TEST(GpuTransactionLifecycle, ADiscardWhileAcceptIsPendingPublishesNothing)
{
    Harness h;
    bool release = false;
    const auto submit = h.Context.JobCommands.Submit;
    h.Context.JobCommands.Submit = [submit, &release](R::JobDesc desc, R::EditorJobIdentity identity)
    {
        if (desc.DebugName == "Fake accept")
            desc.IsReadyToApply = [ready = std::move(desc.IsReadyToApply), &release]() mutable
            {
                return release && ready();
            };
        return submit(std::move(desc), std::move(identity));
    };
    auto& runs = h.Jobs.StepRuns();
    const auto r = MakeRingRun(h, false, false);
    auto& f = *r->F;
    f.DeviceDone = true;
    ASSERT_TRUE(GP::SubmitGpuTransactionRun(GP::GpuTransactionOf(r->F), "Fake run").IsValid());
    const auto id = f.Core.StepRun;
    const auto key = f.Core.Rings[0].Key;
    ASSERT_TRUE(h.DrainUntil(
        [&]
        {
            return f.Core.Phase == R::EditorGpuTransactionPhase::ReadyToAccept &&
                runs.Observe(id)->Phase == R::StepRunPhase::Paused;
        }));
    std::string refusal;
    ASSERT_TRUE(runs.Control(id, R::StepRunAction::Accept, 1u, refusal)) << refusal;
    ASSERT_TRUE(h.DrainUntil(
        [&]
        {
            return f.Core.Phase == R::EditorGpuTransactionPhase::Accepting;
        }));
    ASSERT_TRUE(runs.Control(id, R::StepRunAction::Discard, 1u, refusal)) << "before its publication a discard wins";
    release = true;
    ASSERT_TRUE(h.DrainUntil(
        [&]
        {
            return f.Deliveries == 1u && R::IsTerminal(runs.Observe(id)->Phase);
        }));
    for (int i = 0; i < 10; ++i)
        (void)h.Jobs.Jobs().DrainCompletions(h.Jobs.Events());
    EXPECT_EQ(f.Publications, 0u);
    EXPECT_EQ(f.Deliveries, 1u);
    EXPECT_EQ(f.Core.Phase, R::EditorGpuTransactionPhase::Discarded);
    EXPECT_EQ(runs.Observe(id)->Phase, R::StepRunPhase::Discarded);
    EXPECT_FALSE(h.Residency.HasRing(key));
}

// Restart is offered only where the typed owner resets its generation; a requested restart blocks a typed Accept of
// the older result until the new generation's one step ended (a running start computes it at once).
TEST(GpuTransactionLifecycle, RestartNeedsTheTypedResetAndBlocksTheOlderResultsAccept)
{
    Harness h;
    auto& runs = h.Jobs.StepRuns();
    std::string refusal;
    const auto rested = [&](const Fake& f, const std::uint64_t generation)
    {
        const auto o = runs.Observe(f.Core.StepRun);
        return f.Core.Phase == R::EditorGpuTransactionPhase::ReadyToAccept && o && o->Generation == generation &&
            o->Phase == R::StepRunPhase::Paused;
    };
    auto plain = h.Make();
    plain->DeviceDone = true;
    ASSERT_TRUE(GP::SubmitGpuTransactionRun(GP::GpuTransactionOf(plain), "Fake run").IsValid());
    ASSERT_TRUE(h.DrainUntil(
        [&]
        {
            return rested(*plain, 0u);
        }));
    EXPECT_FALSE(runs.Observe(plain->Core.StepRun)->CanRestart);
    EXPECT_FALSE(runs.Control(plain->Core.StepRun, R::StepRunAction::Restart, 1u, refusal));

    const auto r = MakeRingRun(h, false, true);
    auto& f = *r->F;
    f.DeviceDone = true;
    ASSERT_TRUE(GP::SubmitGpuTransactionRun(GP::GpuTransactionOf(r->F), "Fake run").IsValid());
    ASSERT_TRUE(h.DrainUntil(
        [&]
        {
            return rested(f, 0u);
        }));
    ASSERT_TRUE(runs.Control(f.Core.StepRun, R::StepRunAction::Restart, 1u, refusal)) << refusal;
    const auto pending = GP::GpuTransactionAcceptRefusal(f.Core);
    ASSERT_TRUE(pending.has_value()) << "no Accept of the older result while the restart waits";
    EXPECT_EQ(pending->Status, R::EditorCommandStatus::InvalidProcessingParameters);
    ASSERT_TRUE(h.DrainUntil(
        [&]
        {
            return rested(f, 1u);
        }));
    EXPECT_EQ(r->Restarts, 1u);
    EXPECT_EQ(runs.Observe(f.Core.StepRun)->Steps, 1u);
    EXPECT_EQ(r->Generations.size(), 2u);
    EXPECT_FALSE(GP::GpuTransactionAcceptRefusal(f.Core).has_value()) << "the new result may be accepted";
    GP::DiscardGpuTransaction(f.Core, R::EditorCommandStatus::NoChange, "discarded");
    GP::DiscardGpuTransaction(plain->Core, R::EditorCommandStatus::NoChange, "discarded");
    EXPECT_EQ(f.Publications + plain->Publications, 0u);
}

// ARCH-023 P5: the core decides a step run's options from Execution alone. A one-shot is normalized (one step, no live
// preview, its Start and AutoAccept kept); an iterative transaction keeps the method's whole options.
// Predictions: one-shot {cap 5, every 3, 200 ms, Converged, Ready} starts {1, 0, 0, Converged, Ready}; Iterative keeps
// {5, 3, 200, Converged, Ready}. Mutation: reading a shadow flag (or the given cap) instead changes either row.
TEST(GpuTransactionLifecycle, OneShotsNormalizeTheirExecutionAndIterativeRunsKeepIt)
{
    EXPECT_EQ(GP::GpuTransactionCore{}.Execution, R::kOneShotStepRunExecution);
    for (const auto kind : {GP::GpuTransactionKind::OneShot, GP::GpuTransactionKind::Iterative})
    {
        Harness h;
        auto& runs = h.Jobs.StepRuns();
        const auto r = MakeRingRun(h, false, true);
        auto& f = *r->F;
        f.Core.Kind = kind;
        f.Core.Execution = {.IterationCap = 5u,
            .PreviewEvery = 3u,
            .PreviewIntervalMs = 200u,
            .AutoAccept = R::StepRunAutoAccept::Converged,
            .Start = R::StepRunStart::Ready};
        ASSERT_TRUE(GP::SubmitGpuTransactionRun(GP::GpuTransactionOf(r->F), "Fake run").IsValid());
        const auto observed = runs.Observe(f.Core.StepRun);
        ASSERT_TRUE(observed);
        const auto& e = observed->Execution;
        const bool oneShot = kind == GP::GpuTransactionKind::OneShot;
        EXPECT_EQ(e.IterationCap, oneShot ? 1u : 5u);
        EXPECT_EQ(e.PreviewEvery, oneShot ? 0u : 3u);
        EXPECT_EQ(e.PreviewIntervalMs, oneShot ? 0u : 200u);
        EXPECT_EQ(e.AutoAccept, R::StepRunAutoAccept::Converged)
            << "one-shot normalization preserves the configured policy";
        EXPECT_EQ(e.Start, R::StepRunStart::Ready);
        GP::DiscardGpuTransaction(f.Core, R::EditorCommandStatus::NoChange, "discarded");
    }
}

// ARCH-023 (review finding 4): a worker-stepped transaction returns what its steps captured (the worker's workspace and
// index references, here `worker`) once no step can run any more, also while a caller keeps the finished handle. A
// discard during a running step keeps it until that step returned (the worker still reads it), then the step run's
// settled end (Finished) releases it; an accepted run releases it at its end. Its one-shot Poll sees the completed
// step in the drain its step run comes to rest. Predictions: discard: delivered at once, `worker` alive while the step
// is held, null after settlement with `f` kept, never released while the step ran; accept: alive while the result
// waits, null after the delivery. Mutation: calling Hooks.Release at once frees it under the running step; dropping
// the Finished hook leaves the discarded run's `worker` pinned by the handle.
TEST(GpuTransactionLifecycle, AWorkerSteppedRunReturnsItsWorkerLeaseOnceNoStepRunsWhileTheHandleIsKept)
{
    for (const bool discard : {true, false})
    {
        std::shared_ptr<int> worker = std::make_shared<int>(0); // the typed state's worker references
        std::atomic<bool> entered{false}, proceed{!discard}, returned{false}, releasedDuringStep{false};
        Harness h; // after the locals its hooks reference: destroyed first
        auto& runs = h.Jobs.StepRuns();
        auto f = h.Make();
        auto* raw = f.get();
        auto& t = f->Core;
        t.Pages = std::make_shared<R::WorkerGpuPages>(h.Device);
        t.Hooks.Prepare = [](std::string&)
        {
            return GP::GpuTransactionPrepare::Ready;
        };
        t.Hooks.Step = [&](R::StepRunMetric&)
        {
            entered.store(true);
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds{5};
            while (!proceed.load() && std::chrono::steady_clock::now() < until)
                std::this_thread::yield();
            returned.store(true);
            return R::StepOutcome::Converged;
        };
        t.Hooks.Poll = [raw]
        {
            return GP::GpuTransactionStepCompleted(raw->Core);
        };
        t.Hooks.Release = [&]
        {
            releasedDuringStep = releasedDuringStep.load() || (entered.load() && !returned.load());
            worker.reset();
        };
        ASSERT_TRUE(GP::SubmitGpuTransactionRun(GP::GpuTransactionOf(f), "Fake run").IsValid());
        const auto id = t.StepRun;
        std::string refusal;
        if (discard)
        {
            ASSERT_TRUE(h.DrainUntil(
                [&]
                {
                    return entered.load();
                }));
            ASSERT_TRUE(runs.Control(id, R::StepRunAction::Discard, 1u, refusal)) << refusal;
            ASSERT_TRUE(h.DrainUntil(
                [&]
                {
                    return f->Deliveries == 1u;
                }))
                << "the owner pump delivers the discard while the step is still held";
            EXPECT_TRUE(worker) << "the running step still reads what it captured";
            proceed = true;
        }
        else
        {
            ASSERT_TRUE(h.DrainUntil(
                [&]
                {
                    return t.Phase == R::EditorGpuTransactionPhase::ReadyToAccept &&
                        runs.Observe(id)->Phase == R::StepRunPhase::Paused;
                }));
            EXPECT_TRUE(worker) << "a result waiting for Accept keeps its worker state (a restart reuses it)";
            ASSERT_TRUE(runs.Control(id, R::StepRunAction::Accept, 1u, refusal)) << refusal;
        }
        ASSERT_TRUE(h.DrainUntil(
            [&]
            {
                return f->Deliveries == 1u && !worker;
            }))
            << "settled: the worker's references return while the handle is kept";
        EXPECT_FALSE(releasedDuringStep.load());
        EXPECT_EQ(
            f->Core.Phase, discard ? R::EditorGpuTransactionPhase::Discarded : R::EditorGpuTransactionPhase::Applied);
        EXPECT_EQ(f->Publications, discard ? 0u : 1u);
    }
}

// Cancellation closes admission immediately, even if the prior page happened to finish before the next abort check.
TEST(GpuTransactionLifecycle, AbortedPagesSubmitNothingUntilRestartAndSealedPagesNeverRestart)
{
    Extrinsic::Tests::MockDevice device;
    unsigned submitted = 0u;
    device.ComputeReadback = [&](auto, std::uint64_t, auto)
    {
        ++submitted;
        return Extrinsic::RHI::ReadbackToken{};
    };
    R::WorkerGpuPages pages{device};
    const auto record = [](Extrinsic::RHI::ICommandContext&)
    {
        return Extrinsic::RHI::BufferHandle{};
    };
    pages.Abort();
    EXPECT_EQ(pages.Run(record, {}), R::WorkerGpuPageStatus::Aborted);
    EXPECT_FALSE(pages.SubmitCopy(record).IsValid());
    EXPECT_EQ(submitted, 0u);
    pages.ClearAbort();
    EXPECT_EQ(pages.Run(record, {}), R::WorkerGpuPageStatus::Refused);
    EXPECT_EQ(submitted, 1u);
    pages.Seal();
    pages.ClearAbort();
    EXPECT_EQ(pages.Run(record, {}), R::WorkerGpuPageStatus::Aborted);
    EXPECT_FALSE(pages.SubmitCopy(record).IsValid());
    EXPECT_EQ(submitted, 1u);
}

// Initial uploads precede the first accepted page; later pages cannot join unrelated graphics transfers.
TEST(GpuTransactionLifecycle, OnlyTheFirstAcceptedPageOfAGenerationOrdersGraphicsUploads)
{
    Extrinsic::Tests::MockDevice device;
    bool refuse = true;
    device.ComputeReadback = [&](auto, std::uint64_t, auto)
    {
        return Extrinsic::RHI::ReadbackToken{refuse ? 0u : 1u};
    };
    device.TransferQueue.ComputeWait = [](auto, auto)
    {
        return Extrinsic::RHI::ComputeWaitStatus::Complete;
    };
    device.TransferQueue.TakeReadback = [](auto, auto)
    {
        return true;
    };
    R::WorkerGpuPages pages{device};
    const auto record = [](Extrinsic::RHI::ICommandContext&)
    {
        return Extrinsic::RHI::BufferHandle{};
    };
    EXPECT_EQ(pages.Run(record, {}), R::WorkerGpuPageStatus::Refused);
    refuse = false;
    EXPECT_EQ(pages.Run(record, {}), R::WorkerGpuPageStatus::Complete);
    EXPECT_EQ(pages.Run(record, {}), R::WorkerGpuPageStatus::Complete);
    EXPECT_TRUE(pages.SubmitCopy(record).IsValid());
    pages.ClearAbort();
    EXPECT_TRUE(pages.SubmitCopy(record).IsValid());
    EXPECT_EQ(pages.Run(record, {}), R::WorkerGpuPageStatus::Complete);
    pages.ClearAbort(Extrinsic::RHI::ComputeInputOrder::LaneOrderOnly);
    EXPECT_EQ(pages.Run(record, {}), R::WorkerGpuPageStatus::Complete);
    using Order = Extrinsic::RHI::ComputeInputOrder;
    const std::vector<Order> expected{Order::AfterIssuedTransfers,
        Order::AfterIssuedTransfers,
        Order::LaneOrderOnly,
        Order::LaneOrderOnly,
        Order::AfterIssuedTransfers,
        Order::LaneOrderOnly,
        Order::LaneOrderOnly};
    EXPECT_EQ(device.ComputeInputOrders, expected);
}

// Owner identity is explicit and independent of the user-facing job label; runtime callers retain their default.
TEST(GpuTransactionLifecycle, ExplicitOwnerReachesTheStepRunObservation)
{
    for (const auto* owner : {"Runtime.GpuTransaction", "point_spacing", "kernel_density", "density_weights"})
    {
        Harness h;
        auto f = h.Make();
        EXPECT_EQ(f->Core.Owner, "Runtime.GpuTransaction");
        f->Core.Owner = owner;
        f->Core.JobLabel = "User supplied label";
        f->Core.Execution.Start = R::StepRunStart::Ready;
        ASSERT_TRUE(GP::SubmitGpuTransactionRun(GP::GpuTransactionOf(f), "Worker stage label").IsValid());
        const auto observed = h.Jobs.StepRuns().Observe(f->Core.StepRun);
        ASSERT_TRUE(observed);
        EXPECT_EQ(observed->Owner, owner);
        EXPECT_EQ(observed->Label, "User supplied label");
        GP::DiscardGpuTransaction(f->Core, R::EditorCommandStatus::NoChange, "discarded");
    }
}

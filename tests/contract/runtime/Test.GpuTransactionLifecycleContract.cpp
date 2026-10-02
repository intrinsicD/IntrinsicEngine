// RUNTIME-311: the shared two-phase GPU Run/Accept transaction lifecycle, driven through a
// minimal typed transaction. Pins what every method transaction inherits: exactly-once
// delivery, cancellation of a parked Run, the Accept stage joining the run, stale-ring
// detection that never releases a successor's ring, Discard ignored while Accept publishes,
// and the outcome of a refused automatic Accept.
#include <array>
#include <chrono>
#include <thread>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <gtest/gtest.h>
#include "MockRHI.hpp"
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
import Geometry.Properties;
// Test seam: the lifecycle is internal to the editor operations.
#include "Editor/Operations/Runtime.GeometryProcessingOperations.GpuFront.hpp"
#include "Editor/Operations/Runtime.GpuTransactionLifecycle.hpp"

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
            t.AutoAccept = autoAccept;
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
    ASSERT_TRUE(h.Jobs.DrainUntilTerminal());
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
    ASSERT_TRUE(GP::SubmitGpuTransactionRun(GP::GpuTransactionOf(f), "Fake run").IsValid());
    ASSERT_TRUE(h.Jobs.DrainUntilTerminal());
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

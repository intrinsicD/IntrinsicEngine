// RUNTIME-293 (ADR 0030 decision 6) on a null/mock device: Accept of a GPU-authored position
// ring publishes every row through one undoable history entry guarded by the captured
// watches, binds the front as the canonical slot of the new revision, and refuses a stale
// run; undo and redo restore the rows through the ordinary upload path (dirty positions).
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>
#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>
#include "MockRHI.hpp"
#include "PointDomainFixture.hpp"
#include "SandboxEditorJobHarness.hpp"
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Component.Culling.Local;
import Extrinsic.ECS.Component.Culling.World;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.GpuPropertyBinding;
import Extrinsic.Graphics.GpuPropertyResidency;
import Geometry.Properties;
namespace R = Extrinsic::Runtime;
namespace G = Extrinsic::Graphics;
namespace D = Extrinsic::ECS::Components::DirtyTags;
using Domain = R::GeometryElementDomain;
using K = Geometry::PropertyValueKind;
namespace
{
    struct Harness
    {
        Extrinsic::ECS::Scene::Registry Scene;
        R::EditorCommandHistory History;
        entt::entity Entity;
        R::EditorProcessingContext Context;
        Extrinsic::Tests::MockDevice Device;
        G::GpuPropertyResidency Residency{Device};
        Extrinsic::Tests::EditorJobHarness Jobs;
        R::GeometryPropertyRef Positions{Domain::PointCloudPoint, "v:position", K::Vec3};
        Harness()
        {
            Entity = Intrinsic::Tests::MakePointDomainSource(Scene, Domain::PointCloudPoint);
            auto& props = Props();
            auto rows = props.GetOrAdd<glm::vec3>("v:position", glm::vec3{0.f});
            for (std::size_t i = 0; i < props.Size(); ++i) rows[i] = {float(i), 0.5f * float(i), 0.f};
            Device.TransferQueue.AcceptBufferUploads = true;
            Context.Scene = &Scene;
            Context.CommandHistory = &History;
            Jobs.Attach(Context);
        }
        Geometry::PropertySet& Props() { return Intrinsic::Tests::PointDomainProperties(Scene, Entity, Domain::PointCloudPoint); }
        std::vector<glm::vec3> Rows() { return std::as_const(Props()).Get<glm::vec3>("v:position").Vector(); }
        auto Commands() { return R::BindEditorProcessingCommands(Context); }
        auto Id() { return R::SelectionController::ToStableEntityId(Entity); }
        G::GpuPropertyKey Key() const { return R::MakeGpuPropertyKey(Context.World, Entity, Positions); }
        std::optional<Geometry::PropertyRevision> Revision() { return Props().FindPropertyRevision("v:position"); }
        // A run whose ring front waits for Accept (the seam supplies the front's rows).
        R::EditorGpuPositionRunHandle Ready()
        {
            std::string why;
            auto run = R::BeginEditorGpuPositionRun(Commands(), Id(), Positions, Residency, why);
            EXPECT_TRUE(run) << why;
            if (!run) return {};
            EXPECT_TRUE(R::EditorGpuPositionRunFirstBack(run).has_value()) << "Begin acquired the ring's first write slot";
            EXPECT_TRUE(Residency.Publish(Key()));
            return run;
        }
        std::vector<glm::vec3> Shifted(const glm::vec3 shift)
        {
            auto rows = Rows();
            for (auto& p : rows) p += shift;
            return rows;
        }
        bool Dirty() { return Scene.Raw().any_of<D::DirtyVertexPositions>(Entity); }
        // The point-domain fixture's rows, with row `deletedRow` deleted.
        void Delete(const std::size_t deletedRow)
        {
            auto& props = Props();
            props.GetOrAdd<bool>("v:deleted", false)[deletedRow] = true;
        }
        void ClearDirty() { Scene.Raw().remove<D::DirtyVertexPositions, D::GpuDirty>(Entity); }
    };
}

TEST(GpuPositionsAccept, AcceptPublishesEveryRowUndoablyAndBindsTheFrontAsTheCanonicalRevision)
{
    Harness h;
    const auto before = h.Rows();
    const auto run = h.Ready();
    ASSERT_TRUE(run);
    ASSERT_TRUE(h.Residency.HasRing(h.Key()));
    const auto front = h.Shifted({1.f, 2.f, 3.f});
    const auto frontBuffer = h.Residency.Front(h.Key())->Buffer;

    std::optional<R::EditorGpuPositionAcceptResult> delivered;
    const auto accepted = R::AcceptEditorGpuPositionRun(h.Commands(), run, h.Residency, "Move points (GPU)",
        [&](R::EditorGpuPositionAcceptResult r) { delivered = r; }, front);
    ASSERT_EQ(accepted.Status, R::EditorCommandStatus::Pending) << accepted.Message;
    EXPECT_FALSE(delivered) << "\"Applied\" only after the CPU publication";
    EXPECT_EQ(h.Rows(), before) << "nothing is published before the drain";

    ASSERT_TRUE(h.Jobs.DrainUntilTerminal());
    ASSERT_TRUE(delivered);
    EXPECT_EQ(delivered->Status, R::EditorCommandStatus::Applied) << delivered->Message;
    EXPECT_FALSE(delivered->RenderAcknowledged) << "no extraction on this context: the ordinary upload applies";
    EXPECT_EQ(h.Rows(), front);
    EXPECT_TRUE(h.Dirty()) << "the ordinary upload path restores the block";
    // The front is the canonical slot of the published revision; the ring is gone.
    EXPECT_FALSE(h.Residency.HasRing(h.Key()));
    const auto canonical = h.Residency.Front(h.Key());
    ASSERT_TRUE(canonical.has_value());
    EXPECT_EQ(canonical->Buffer, frontBuffer);
    ASSERT_TRUE(h.Revision().has_value());
    EXPECT_EQ(canonical->Revision, *h.Revision());
    EXPECT_FALSE(R::EditorGpuPositionRunCurrent(h.Commands(), run)) << "the run is consumed";
    EXPECT_EQ(R::AcceptEditorGpuPositionRun(h.Commands(), run, h.Residency, "again", {}, front).Status,
              R::EditorCommandStatus::InvalidProcessingParameters);

    // Undo restores every row (one ordinary upload); redo applies the front again.
    h.ClearDirty();
    ASSERT_TRUE(h.History.Undo().Succeeded());
    EXPECT_EQ(h.Rows(), before);
    EXPECT_TRUE(h.Dirty());
    EXPECT_NE(*h.Revision(), canonical->Revision) << "undo moves the CPU revision on";
    h.ClearDirty();
    ASSERT_TRUE(h.History.Redo().Succeeded());
    EXPECT_EQ(h.Rows(), front);
    EXPECT_TRUE(h.Dirty());
}

TEST(GpuPositionsAccept, AStaleRunIsRefusedBeforeAndAtPublication)
{
    Harness h;
    const auto run = h.Ready();
    ASSERT_TRUE(run);
    const auto front = h.Shifted({1.f, 0.f, 0.f});
    // The positions changed after the run started: Accept refuses, the ring stays for Discard.
    h.Props().Get<glm::vec3>("v:position")[0] = {9.f, 9.f, 9.f};
    EXPECT_FALSE(R::EditorGpuPositionRunCurrent(h.Commands(), run));
    const auto refused = R::AcceptEditorGpuPositionRun(h.Commands(), run, h.Residency, "Move", {}, front);
    EXPECT_EQ(refused.Status, R::EditorCommandStatus::StaleEntity) << refused.Message;
    EXPECT_TRUE(h.Residency.HasRing(h.Key()));
    R::DiscardEditorGpuPositionRun(h.Commands(), run, h.Residency);
    EXPECT_FALSE(h.Residency.HasRing(h.Key()));

    // A change between Accept and the publication: the job finalizes as stale.
    Harness g;
    const auto run2 = g.Ready();
    ASSERT_TRUE(run2);
    const auto edited = g.Shifted({0.f, 1.f, 0.f});
    std::optional<R::EditorGpuPositionAcceptResult> delivered;
    ASSERT_EQ(R::AcceptEditorGpuPositionRun(g.Commands(), run2, g.Residency, "Move",
                                            [&](R::EditorGpuPositionAcceptResult r) { delivered = r; }, edited).Status,
              R::EditorCommandStatus::Pending);
    g.Props().Get<glm::vec3>("v:position")[1] = {7.f, 7.f, 7.f};
    const auto afterEdit = g.Rows();
    ASSERT_TRUE(g.Jobs.DrainUntilTerminal());
    ASSERT_TRUE(delivered);
    EXPECT_EQ(delivered->Status, R::EditorCommandStatus::StaleEntity) << delivered->Message;
    EXPECT_EQ(g.Rows(), afterEdit) << "the edit stands; the GPU rows are not published";
    EXPECT_FALSE(g.History.CanUndo());
}

TEST(GpuPositionsAccept, NonFiniteAndUnchangedFrontsDoNotPublish)
{
    Harness h;
    const auto before = h.Rows();
    auto run = h.Ready();
    auto bad = h.Shifted({0.f, 0.f, 0.f});
    bad[2].y = std::numeric_limits<float>::quiet_NaN();
    std::optional<R::EditorGpuPositionAcceptResult> delivered;
    ASSERT_EQ(R::AcceptEditorGpuPositionRun(h.Commands(), run, h.Residency, "Move",
                                            [&](R::EditorGpuPositionAcceptResult r) { delivered = r; }, bad).Status,
              R::EditorCommandStatus::Pending);
    ASSERT_TRUE(h.Jobs.DrainUntilTerminal());
    ASSERT_TRUE(delivered);
    EXPECT_EQ(delivered->Status, R::EditorCommandStatus::GeometryProcessingFailed) << delivered->Message;
    EXPECT_EQ(h.Rows(), before);
    EXPECT_FALSE(h.History.CanUndo());
    EXPECT_FALSE(h.Residency.HasRing(h.Key())) << "a failed Accept ends the run and releases its ring";

    // A front equal to the CPU rows: no history entry; it serves the current revision as is.
    run = h.Ready();
    delivered.reset();
    ASSERT_EQ(R::AcceptEditorGpuPositionRun(h.Commands(), run, h.Residency, "Move",
                                            [&](R::EditorGpuPositionAcceptResult r) { delivered = r; }, before).Status,
              R::EditorCommandStatus::Pending);
    ASSERT_TRUE(h.Jobs.DrainUntilTerminal());
    ASSERT_TRUE(delivered);
    EXPECT_EQ(delivered->Status, R::EditorCommandStatus::NoChange) << delivered->Message;
    EXPECT_FALSE(h.History.CanUndo());
    EXPECT_FALSE(h.Residency.HasRing(h.Key()));
    ASSERT_TRUE(h.Residency.Front(h.Key()).has_value());
    EXPECT_EQ(h.Residency.Front(h.Key())->Revision, *h.Revision());
}

// Review P1: a front published after Accept read its predecessor back is neither what the
// CPU receives nor bound: the readback's publication is published, the ring is discarded.
TEST(GpuPositionsAccept, AFrontPublishedAfterTheReadbackIsNotBoundAsTheAcceptedRevision)
{
    Harness h;
    const auto run = h.Ready();
    ASSERT_TRUE(run);
    const auto acceptedFront = *h.Residency.Front(h.Key());
    const auto readBack = h.Shifted({1.f, 0.f, 0.f});
    std::optional<R::EditorGpuPositionAcceptResult> delivered;
    ASSERT_EQ(R::AcceptEditorGpuPositionRun(h.Commands(), run, h.Residency, "Move",
                                            [&](R::EditorGpuPositionAcceptResult r) { delivered = r; }, readBack).Status,
              R::EditorCommandStatus::Pending);
    // A straggling preview lands while the readback is under way.
    ASSERT_TRUE(h.Residency.AcquireBack(h.Key(), acceptedFront.Layout, 2u));
    ASSERT_TRUE(h.Residency.Publish(h.Key()));
    const auto later = *h.Residency.Front(h.Key());
    ASSERT_NE(later.Publication, acceptedFront.Publication);

    ASSERT_TRUE(h.Jobs.DrainUntilTerminal());
    ASSERT_TRUE(delivered);
    EXPECT_EQ(delivered->Status, R::EditorCommandStatus::Applied) << delivered->Message;
    EXPECT_EQ(h.Rows(), readBack) << "the CPU holds what was read back";
    EXPECT_FALSE(h.Residency.HasRing(h.Key())) << "the later publication is not bound; the ring is discarded";
    EXPECT_FALSE(h.Residency.Front(h.Key()).has_value()) << "no canonical slot claims the new revision";
}

// Review P1: Discard of the run abandons an Accept under way; the readback that lands
// afterwards publishes nothing. The residency's Discard alone is not the contract.
TEST(GpuPositionsAccept, DiscardOfTheRunAbandonsAnAcceptUnderWay)
{
    Harness h;
    const auto before = h.Rows();
    const auto run = h.Ready();
    ASSERT_TRUE(run);
    std::optional<R::EditorGpuPositionAcceptResult> delivered;
    ASSERT_EQ(R::AcceptEditorGpuPositionRun(h.Commands(), run, h.Residency, "Move",
                                            [&](R::EditorGpuPositionAcceptResult r) { delivered = r; },
                                            h.Shifted({0.f, 0.f, 2.f})).Status,
              R::EditorCommandStatus::Pending);
    R::DiscardEditorGpuPositionRun(h.Commands(), run, h.Residency);
    ASSERT_TRUE(delivered);
    EXPECT_EQ(delivered->Status, R::EditorCommandStatus::StaleEntity) << delivered->Message;
    EXPECT_FALSE(h.Residency.HasRing(h.Key()));
    ASSERT_TRUE(h.Jobs.DrainUntilTerminal());
    EXPECT_EQ(h.Rows(), before) << "the discarded result is not published when its job drains";
    EXPECT_FALSE(h.History.CanUndo());
    EXPECT_FALSE(h.Dirty());
    EXPECT_EQ(R::AcceptEditorGpuPositionRun(h.Commands(), run, h.Residency, "again", {}, before).Status,
              R::EditorCommandStatus::InvalidProcessingParameters);

    // Discard of a run that only waits (no Accept) releases the ring likewise.
    const auto waiting = h.Ready();
    ASSERT_TRUE(h.Residency.HasRing(h.Key()));
    R::DiscardEditorGpuPositionRun(h.Commands(), waiting, h.Residency);
    EXPECT_FALSE(h.Residency.HasRing(h.Key()));
    EXPECT_FALSE(R::EditorGpuPositionRunCurrent(h.Commands(), waiting));
}

// Review P2: authored culling bounds move with the accepted rows in the same history entry
// (and back on undo), so an accepted result outside the old bounds is not culled.
TEST(GpuPositionsAccept, AuthoredCullingBoundsFollowTheAcceptedRowsAndTheirUndo)
{
    namespace Culling = Extrinsic::ECS::Components::Culling;
    Harness h;
    auto& raw = h.Scene.Raw();
    const glm::vec3 translation{10.f, 0.f, 0.f};
    raw.emplace<Extrinsic::ECS::Components::Transform::WorldMatrix>(h.Entity).Matrix = glm::translate(glm::mat4{1.f}, translation);
    Culling::Local::Bounds local{};
    local.LocalBoundingSphere = {.Center = {2.f, 1.f, 0.f}, .Radius = 2.5f};
    local.LocalBoundingAABB = {.Min = {0.f, 0.f, 0.f}, .Max = {4.f, 2.f, 0.f}};
    Culling::World::Bounds world{};
    world.WorldBoundingSphere = {.Center = translation + local.LocalBoundingSphere.Center, .Radius = 2.5f};
    raw.emplace<Culling::Local::Bounds>(h.Entity, local);
    raw.emplace<Culling::World::Bounds>(h.Entity, world);

    const auto run = h.Ready();
    const glm::vec3 shift{50.f, 0.f, 0.f}; // far outside the authored bounds
    const auto front = h.Shifted(shift);
    std::optional<R::EditorGpuPositionAcceptResult> delivered;
    ASSERT_EQ(R::AcceptEditorGpuPositionRun(h.Commands(), run, h.Residency, "Move",
                                            [&](R::EditorGpuPositionAcceptResult r) { delivered = r; }, front).Status,
              R::EditorCommandStatus::Pending);
    ASSERT_TRUE(h.Jobs.DrainUntilTerminal());
    ASSERT_TRUE(delivered);
    ASSERT_EQ(delivered->Status, R::EditorCommandStatus::Applied) << delivered->Message;

    glm::vec3 minimum = front[0], maximum = front[0];
    for (const auto& p : front) { minimum = glm::min(minimum, p); maximum = glm::max(maximum, p); }
    const auto& accepted = raw.get<Culling::World::Bounds>(h.Entity);
    const glm::vec3 expectedCenter = translation + 0.5f * (minimum + maximum);
    EXPECT_LT(glm::distance(accepted.WorldBoundingSphere.Center, expectedCenter), 1e-3f);
    EXPECT_NEAR(accepted.WorldBoundingSphere.Radius, 0.5f * glm::length(maximum - minimum), 1e-3f);
    const auto& acceptedLocal = raw.get<Culling::Local::Bounds>(h.Entity);
    EXPECT_EQ(acceptedLocal.LocalBoundingAABB.Min, minimum);
    EXPECT_EQ(acceptedLocal.LocalBoundingAABB.Max, maximum);

    ASSERT_TRUE(h.History.Undo().Succeeded());
    EXPECT_EQ(raw.get<Culling::World::Bounds>(h.Entity).WorldBoundingSphere.Center, world.WorldBoundingSphere.Center);
    EXPECT_EQ(raw.get<Culling::World::Bounds>(h.Entity).WorldBoundingSphere.Radius, world.WorldBoundingSphere.Radius);
    EXPECT_EQ(raw.get<Culling::Local::Bounds>(h.Entity).LocalBoundingAABB.Max, local.LocalBoundingAABB.Max);
    ASSERT_TRUE(h.History.Redo().Succeeded());
    EXPECT_LT(glm::distance(raw.get<Culling::World::Bounds>(h.Entity).WorldBoundingSphere.Center, expectedCenter), 1e-3f);
}

// Re-review P2: only the local bounds are history state; the world bounds follow the
// entity's world matrix at every mutation, so a transform edit between Accept and undo is
// never replayed from history.
TEST(GpuPositionsAccept, UndoRecomputesWorldBoundsUnderTheCurrentTransformNotTheAcceptedOne)
{
    namespace Culling = Extrinsic::ECS::Components::Culling;
    Harness h;
    auto& raw = h.Scene.Raw();
    const glm::vec3 first{10.f, 0.f, 0.f}, moved{0.f, 20.f, 0.f};
    auto& matrix = raw.emplace<Extrinsic::ECS::Components::Transform::WorldMatrix>(h.Entity);
    matrix.Matrix = glm::translate(glm::mat4{1.f}, first);
    Culling::Local::Bounds local{};
    local.LocalBoundingSphere = {.Center = {2.f, 1.f, 0.f}, .Radius = 2.5f};
    local.LocalBoundingAABB = {.Min = {0.f, 0.f, 0.f}, .Max = {4.f, 2.f, 0.f}};
    raw.emplace<Culling::Local::Bounds>(h.Entity, local);
    Culling::World::Bounds world{};
    world.WorldBoundingSphere = {.Center = first + local.LocalBoundingSphere.Center, .Radius = 2.5f};
    raw.emplace<Culling::World::Bounds>(h.Entity, world);

    const auto run = h.Ready();
    const auto front = h.Shifted({50.f, 0.f, 0.f});
    std::optional<R::EditorGpuPositionAcceptResult> delivered;
    ASSERT_EQ(R::AcceptEditorGpuPositionRun(h.Commands(), run, h.Residency, "Move",
                                            [&](R::EditorGpuPositionAcceptResult r) { delivered = r; }, front).Status,
              R::EditorCommandStatus::Pending);
    ASSERT_TRUE(h.Jobs.DrainUntilTerminal());
    ASSERT_TRUE(delivered && delivered->Status == R::EditorCommandStatus::Applied) << delivered->Message;

    // The entity (or its parent) moves after Accept.
    matrix.Matrix = glm::translate(glm::mat4{1.f}, moved);
    ASSERT_TRUE(h.History.Undo().Succeeded());
    const auto& undone = raw.get<Culling::World::Bounds>(h.Entity);
    EXPECT_LT(glm::distance(undone.WorldBoundingSphere.Center, moved + local.LocalBoundingSphere.Center), 1e-3f)
        << "undo restores the local bounds and derives the world bounds from the current transform";
    EXPECT_EQ(raw.get<Culling::Local::Bounds>(h.Entity).LocalBoundingAABB.Max, local.LocalBoundingAABB.Max);
    ASSERT_TRUE(h.History.Redo().Succeeded());
    glm::vec3 minimum = front[0], maximum = front[0];
    for (const auto& p : front) { minimum = glm::min(minimum, p); maximum = glm::max(maximum, p); }
    EXPECT_LT(glm::distance(raw.get<Culling::World::Bounds>(h.Entity).WorldBoundingSphere.Center,
                            moved + 0.5f * (minimum + maximum)), 1e-3f)
        << "redo derives the accepted local bounds under the current transform";
}

// Re-review P2: deleted rows do not shape the bounds, and rows that admit no finite bounds
// are refused before anything is written (not published with stale bounds).
TEST(GpuPositionsAccept, BoundsIgnoreDeletedRowsAndAnOverflowingExtentRefusesTheAccept)
{
    namespace Culling = Extrinsic::ECS::Components::Culling;
    Harness h;
    auto& raw = h.Scene.Raw();
    Culling::World::Bounds world{};
    world.WorldBoundingSphere = {.Center = {0.f, 0.f, 0.f}, .Radius = 1.f};
    raw.emplace<Culling::World::Bounds>(h.Entity, world);
    raw.emplace<Culling::Local::Bounds>(h.Entity);
    h.Delete(4);
    const auto before = h.Rows();

    auto run = h.Ready();
    auto front = h.Shifted({1.f, 0.f, 0.f});
    front[4] = {1000.f, 1000.f, 1000.f}; // a deleted row far away
    std::optional<R::EditorGpuPositionAcceptResult> delivered;
    ASSERT_EQ(R::AcceptEditorGpuPositionRun(h.Commands(), run, h.Residency, "Move",
                                            [&](R::EditorGpuPositionAcceptResult r) { delivered = r; }, front).Status,
              R::EditorCommandStatus::Pending);
    ASSERT_TRUE(h.Jobs.DrainUntilTerminal());
    ASSERT_TRUE(delivered && delivered->Status == R::EditorCommandStatus::Applied) << delivered->Message;
    glm::vec3 minimum = front[0], maximum = front[0];
    for (std::size_t i = 0; i < front.size(); ++i)
        if (i != 4u) { minimum = glm::min(minimum, front[i]); maximum = glm::max(maximum, front[i]); }
    const auto& local = raw.get<Culling::Local::Bounds>(h.Entity);
    EXPECT_EQ(local.LocalBoundingAABB.Max, maximum) << "the deleted row does not widen the bounds";
    EXPECT_EQ(local.LocalBoundingAABB.Min, minimum);
    EXPECT_LT(raw.get<Culling::World::Bounds>(h.Entity).WorldBoundingSphere.Radius, 10.f);
    ASSERT_TRUE(h.History.Undo().Succeeded());
    EXPECT_EQ(h.Rows(), before);

    // Finite coordinates whose extent overflows: no bounds can describe them.
    h.ClearDirty(); // the undo above uploaded through the ordinary path
    run = h.Ready();
    auto overflowing = before;
    overflowing[0] = {-1.0e20f, 0.f, 0.f};
    overflowing[1] = {1.0e20f, 0.f, 0.f};
    delivered.reset();
    const auto boundsBefore = raw.get<Culling::World::Bounds>(h.Entity).WorldBoundingSphere;
    ASSERT_EQ(R::AcceptEditorGpuPositionRun(h.Commands(), run, h.Residency, "Move",
                                            [&](R::EditorGpuPositionAcceptResult r) { delivered = r; }, overflowing).Status,
              R::EditorCommandStatus::Pending);
    ASSERT_TRUE(h.Jobs.DrainUntilTerminal());
    ASSERT_TRUE(delivered);
    EXPECT_EQ(delivered->Status, R::EditorCommandStatus::GeometryProcessingFailed) << delivered->Message;
    EXPECT_NE(delivered->Message.find("culling bounds"), std::string::npos) << delivered->Message;
    EXPECT_FALSE(h.Residency.HasRing(h.Key())) << "a failed Accept ends the run and releases its ring";
    EXPECT_EQ(h.Rows(), before) << "nothing was written";
    EXPECT_EQ(raw.get<Culling::World::Bounds>(h.Entity).WorldBoundingSphere.Center, boundsBefore.Center);
    EXPECT_EQ(raw.get<Culling::World::Bounds>(h.Entity).WorldBoundingSphere.Radius, boundsBefore.Radius);
    EXPECT_FALSE(h.History.CanUndo());
    EXPECT_FALSE(h.Dirty());
}

// Re-review P2: Discard is idempotent for terminal runs and releases only the ring the run
// acquired, so a later run on the same property keeps its ring.
TEST(GpuPositionsAccept, DiscardOfATerminalRunLeavesALaterRunsRingAlone)
{
    Harness h;
    const auto a = h.Ready();
    ASSERT_TRUE(a);
    std::string why;
    EXPECT_FALSE(R::BeginEditorGpuPositionRun(h.Commands(), h.Id(), h.Positions, h.Residency, why))
        << "a second run cannot begin while a ring waits";
    EXPECT_NE(why.find("awaits Accept or Discard"), std::string::npos) << why;
    std::optional<R::EditorGpuPositionAcceptResult> delivered;
    ASSERT_EQ(R::AcceptEditorGpuPositionRun(h.Commands(), a, h.Residency, "Move",
                                            [&](R::EditorGpuPositionAcceptResult r) { delivered = r; },
                                            h.Shifted({1.f, 0.f, 0.f})).Status,
              R::EditorCommandStatus::Pending);
    ASSERT_TRUE(h.Jobs.DrainUntilTerminal());
    ASSERT_TRUE(delivered && delivered->Status == R::EditorCommandStatus::Applied) << delivered->Message;
    EXPECT_FALSE(h.Residency.HasRing(h.Key())) << "the accepted ring became the canonical slot";

    const auto b = h.Ready(); // a later run creates its own ring on the same property
    ASSERT_TRUE(b);
    const auto ringOfB = h.Residency.RingGeneration(h.Key());
    ASSERT_NE(ringOfB, 0u);
    R::DiscardEditorGpuPositionRun(h.Commands(), a, h.Residency);
    R::DiscardEditorGpuPositionRun(h.Commands(), a, h.Residency);
    EXPECT_TRUE(h.Residency.HasRing(h.Key())) << "discarding the terminal run A must not touch B's ring";
    EXPECT_EQ(h.Residency.RingGeneration(h.Key()), ringOfB);
    EXPECT_TRUE(R::EditorGpuPositionRunCurrent(h.Commands(), b));
    // A discarded (non-terminal) run releases its own ring exactly once; a repeat is a no-op.
    R::DiscardEditorGpuPositionRun(h.Commands(), b, h.Residency);
    EXPECT_FALSE(h.Residency.HasRing(h.Key()));
    const auto c = h.Ready();
    ASSERT_TRUE(c);
    R::DiscardEditorGpuPositionRun(h.Commands(), b, h.Residency);
    EXPECT_TRUE(h.Residency.HasRing(h.Key())) << "a repeated Discard of B must not release C's ring";
    R::DiscardEditorGpuPositionRun(h.Commands(), c, h.Residency);
    EXPECT_FALSE(h.Residency.HasRing(h.Key()));
}

// Third check P2: a Begin whose slot allocation the device refuses leaves nothing behind,
// so the next Begin on the same positions succeeds.
TEST(GpuPositionsAccept, ABeginRefusedByTheDeviceLeavesNoRingBehind)
{
    Harness h;
    std::string why;
    h.Device.FailNextBufferCreate = true;
    EXPECT_FALSE(R::BeginEditorGpuPositionRun(h.Commands(), h.Id(), h.Positions, h.Residency, why));
    EXPECT_NE(why.find("refused a write slot"), std::string::npos) << why;
    EXPECT_FALSE(h.Residency.HasRing(h.Key()));
    why.clear();
    const auto run = R::BeginEditorGpuPositionRun(h.Commands(), h.Id(), h.Positions, h.Residency, why);
    ASSERT_TRUE(run) << why;
    EXPECT_TRUE(h.Residency.HasRing(h.Key()));
    R::DiscardEditorGpuPositionRun(h.Commands(), run, h.Residency);
}

// Third check P2: authored local bounds without a world component are recomputed too (they
// are not "absent"), so later propagation never derives world bounds from stale local ones.
TEST(GpuPositionsAccept, LocalOnlyAuthoredBoundsFollowTheAcceptedRowsAndTheirUndo)
{
    namespace Culling = Extrinsic::ECS::Components::Culling;
    Harness h;
    auto& raw = h.Scene.Raw();
    Culling::Local::Bounds local{};
    local.LocalBoundingSphere = {.Center = {2.f, 1.f, 0.f}, .Radius = 2.5f};
    local.LocalBoundingAABB = {.Min = {0.f, 0.f, 0.f}, .Max = {4.f, 2.f, 0.f}};
    raw.emplace<Culling::Local::Bounds>(h.Entity, local);
    ASSERT_FALSE(raw.any_of<Culling::World::Bounds>(h.Entity));

    const auto run = h.Ready();
    const auto front = h.Shifted({50.f, 0.f, 0.f});
    std::optional<R::EditorGpuPositionAcceptResult> delivered;
    ASSERT_EQ(R::AcceptEditorGpuPositionRun(h.Commands(), run, h.Residency, "Move",
                                            [&](R::EditorGpuPositionAcceptResult r) { delivered = r; }, front).Status,
              R::EditorCommandStatus::Pending);
    ASSERT_TRUE(h.Jobs.DrainUntilTerminal());
    ASSERT_TRUE(delivered && delivered->Status == R::EditorCommandStatus::Applied) << delivered->Message;
    glm::vec3 minimum = front[0], maximum = front[0];
    for (const auto& p : front) { minimum = glm::min(minimum, p); maximum = glm::max(maximum, p); }
    EXPECT_EQ(raw.get<Culling::Local::Bounds>(h.Entity).LocalBoundingAABB.Min, minimum) << "local-only bounds are recomputed";
    EXPECT_EQ(raw.get<Culling::Local::Bounds>(h.Entity).LocalBoundingAABB.Max, maximum);
    ASSERT_TRUE(raw.any_of<Culling::World::Bounds>(h.Entity)) << "the world bounds are derived alongside";
    EXPECT_LT(glm::distance(raw.get<Culling::World::Bounds>(h.Entity).WorldBoundingSphere.Center, 0.5f * (minimum + maximum)), 1e-3f);
    ASSERT_TRUE(h.History.Undo().Succeeded());
    EXPECT_EQ(raw.get<Culling::Local::Bounds>(h.Entity).LocalBoundingAABB.Max, local.LocalBoundingAABB.Max);
    EXPECT_EQ(raw.get<Culling::Local::Bounds>(h.Entity).LocalBoundingSphere.Radius, local.LocalBoundingSphere.Radius);
}

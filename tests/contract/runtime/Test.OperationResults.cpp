// RUNTIME-348: the session's store of declared operations' results and the operation runtime's
// delivery into it (ADR 0031 Decisions 3 and 7).
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>
#include <gtest/gtest.h>
#include "PointDomainFixture.hpp"
#include "SandboxEditorJobHarness.hpp"

import Extrinsic.Runtime.PointFieldOperations;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.ECS.Scene.Registry;

namespace R = Extrinsic::Runtime;
using D = R::GeometryElementDomain;

namespace
{
    struct First
    {
        std::string Message;
    };
    struct Second
    {
        std::string Message;
    };

    // A point cloud of eight live samples with a kernel density config on it.
    struct Fixture
    {
        Extrinsic::ECS::Scene::Registry Scene;
        entt::entity Entity{};
        R::KernelDensityConfig Config{};
        R::EditorCommandHistory History;
        R::OperationResultStore Store;
        bool Active{true};
        R::EditorProcessingContext Context{};
        Fixture()
        {
            Entity = Intrinsic::Tests::MakePointDomainSource(Scene, D::MeshVertex);
            auto samples =
                Intrinsic::Tests::PointDomainProperties(Scene, Entity, D::MeshVertex).GetOrAdd<glm::vec3>("samples");
            for (std::size_t i = 0; i < samples.Size(); ++i)
                samples[i] = {float(i) * 0.25f, float(i % 3), 0};
            Config = {.StableEntityId = R::SelectionController::ToStableEntityId(Entity),
                .Positions = {.Domain = D::MeshVertex,
                    .Name = "samples",
                    .ValueKind = Geometry::PropertyValueKind::Vec3},
                .Density = {D::MeshVertex, "density", Geometry::PropertyValueKind::Float},
                .KNeighbors = 2};
            Context.Scene = &Scene;
            Context.CommandHistory = &History;
            Context.OperationResults = &Store;
            Context.AttachmentActive = [this]
            {
                return Active;
            };
        }
        auto Commands()
        {
            return R::BindEditorProcessingCommands(Context);
        }
        [[nodiscard]] std::shared_ptr<const R::EditorKernelDensityResult> Last() const
        {
            return Store.Copy().Last<R::EditorKernelDensityResult>();
        }
    };
}

TEST(OperationResults, StoreKeepsTheNewestRunPerResultType)
{
    R::OperationResultStore store;
    const auto a = store.BeginRun(), b = store.BeginRun();
    EXPECT_LT(a, b);
    EXPECT_TRUE(store.Store(b, First{"b"}));
    EXPECT_FALSE(store.Store(a, First{"a"})) << "an older run never replaces a newer one";
    EXPECT_EQ(store.Last<First>()->Message, "b");
    EXPECT_TRUE(store.Store(b, First{"b again"})) << "the same run replaces its earlier answer";
    EXPECT_TRUE(store.Store(a, Second{"other"})) << "operations are ordered separately";
    EXPECT_FALSE(store.Last<std::string>());
    const auto copy = store.Copy();
    EXPECT_TRUE(store.Store(store.BeginRun(), First{"c"}));
    EXPECT_EQ(copy.Last<First>()->Message, "b again") << "a frame's copy does not change";
    EXPECT_EQ(store.Last<First>()->Message, "c");
    EXPECT_EQ(store.Last<Second>()->Message, "other");
}

TEST(OperationResults, DismissClearsTheBarrierButNotTheCounter)
{
    R::OperationResultStore store;
    const auto old = store.BeginRun(), newer = store.BeginRun();
    ASSERT_TRUE(store.Store(newer, First{"newer"}));
    store.Dismiss(&R::OperationResultKey<First>::Tag);
    EXPECT_FALSE(store.Last<First>());
    EXPECT_TRUE(store.Store(old, First{"late"})) << "after a dismiss any run's delivery fills the slot";
    EXPECT_GT(store.BeginRun(), newer);
    store.Clear();
    EXPECT_FALSE(store.Last<First>());
    EXPECT_GT(store.BeginRun(), newer + 1u) << "a reset does not reuse ids";
}

TEST(OperationResults, RetainStoresOnlyWhileTheAttachmentIsActive)
{
    R::OperationResultStore store;
    bool active = false;
    const std::function<bool()> attachment = [&]
    {
        return active;
    };
    R::RetainOperationResult(&store, attachment, 1u, First{"detached"});
    R::RetainOperationResult(&store, {}, 1u, First{"no attachment"});
    R::RetainOperationResult<First>(nullptr, attachment, 1u, First{"no store"});
    EXPECT_FALSE(store.Last<First>());
    active = true;
    R::RetainOperationResult(&store, attachment, 1u, First{"attached"});
    EXPECT_EQ(store.Last<First>()->Message, "attached");
}

TEST(OperationResults, OlderRunNeverReplacesNewer)
{
    Fixture f;
    Extrinsic::Tests::EditorJobHarness jobs;
    jobs.Attach(f.Context);
    std::optional<R::EditorKernelDensityResult> first;
    const auto queued = R::ApplyEditorKernelDensityCommand(f.Commands(),
        f.Config,
        [&](auto r)
        {
            first = r;
        });
    ASSERT_EQ(queued.Status, R::EditorCommandStatus::Pending) << queued.Message;
    auto missing = f.Config;
    missing.StableEntityId = std::numeric_limits<std::uint32_t>::max();
    const auto refused = R::ApplyEditorKernelDensityCommand(f.Commands(),
        missing,
        [](auto)
        {
            FAIL();
        });
    ASSERT_EQ(refused.Status, R::EditorCommandStatus::InvalidProcessingParameters);
    ASSERT_TRUE(f.Last());
    EXPECT_EQ(f.Last()->Message, refused.Message) << "a genuine immediate failure is the newest result";
    ASSERT_TRUE(jobs.DrainUntilTerminal());
    ASSERT_TRUE(first);
    EXPECT_TRUE(first->Succeeded()) << first->Message;
    EXPECT_EQ(f.Last()->Message, refused.Message) << "the older run's late result does not replace it";
}

TEST(OperationResults, DuplicateRefusalLeavesTheActiveRunsSlot)
{
    Fixture f;
    Extrinsic::Tests::EditorJobHarness jobs;
    jobs.Attach(f.Context);
    unsigned firstCalls = 0;
    const auto queued = R::ApplyEditorKernelDensityCommand(f.Commands(),
        f.Config,
        [&](auto)
        {
            ++firstCalls;
        });
    ASSERT_EQ(queued.Status, R::EditorCommandStatus::Pending);
    ASSERT_TRUE(f.Last());
    EXPECT_EQ(f.Last()->Message, queued.Message) << "the queued answer is stored";
    const auto duplicate = R::ApplyEditorKernelDensityCommand(f.Commands(),
        f.Config,
        [](auto)
        {
            FAIL();
        });
    ASSERT_EQ(duplicate.Status, R::EditorCommandStatus::Pending);
    EXPECT_NE(duplicate.Message.find("already has an active"), std::string::npos) << duplicate.Message;
    EXPECT_EQ(f.Last()->Message, queued.Message) << "the duplicate refusal never occupies the slot";
    ASSERT_TRUE(jobs.DrainUntilTerminal());
    EXPECT_EQ(firstCalls, 1u);
    ASSERT_TRUE(f.Last());
    EXPECT_EQ(f.Last()->Status, R::EditorCommandStatus::Applied) << "any caller's run reaches the slot";
}

TEST(OperationResults, InlineFallbackDeliversOnce)
{
    Fixture f;
    unsigned calls = 0;
    std::optional<R::EditorKernelDensityResult> delivered;
    const auto result = R::ApplyEditorKernelDensityCommand(f.Commands(),
        f.Config,
        [&](auto r)
        {
            ++calls;
            delivered = r;
        });
    ASSERT_TRUE(result.Succeeded()) << result.Message;
    EXPECT_EQ(calls, 1u);
    ASSERT_TRUE(delivered);
    EXPECT_EQ(delivered->Status, result.Status);
    EXPECT_EQ(delivered->Message, result.Message);
    ASSERT_TRUE(f.Last());
    EXPECT_EQ(f.Last()->Status, result.Status);
}

TEST(OperationResults, DetachedSessionStoresNothing)
{
    Fixture f;
    Extrinsic::Tests::EditorJobHarness jobs;
    jobs.Attach(f.Context);
    unsigned calls = 0;
    ASSERT_EQ(R::ApplyEditorKernelDensityCommand(f.Commands(),
                  f.Config,
                  [&](auto)
                  {
                      ++calls;
                  })
                  .Status,
        R::EditorCommandStatus::Pending);
    f.Store.Clear();
    f.Active = false;
    ASSERT_TRUE(jobs.DrainUntilTerminal());
    EXPECT_EQ(calls, 0u) << "a detached workspace receives nothing";
    EXPECT_FALSE(f.Last()) << "and stores nothing";
    f.Active = true;
    ASSERT_EQ(R::ApplyEditorKernelDensityCommand(f.Commands(), f.Config, {}).Status, R::EditorCommandStatus::Pending);
    EXPECT_TRUE(f.Last()) << "a run of the active attachment stores again";
}

// The store is written before the sink runs, so a sink that detaches or reads the slot sees its own result.
TEST(OperationResults, TheStoreIsWrittenBeforeTheSinkRuns)
{
    for (const bool queued : {false, true})
    {
        SCOPED_TRACE(queued);
        Fixture f;
        Extrinsic::Tests::EditorJobHarness jobs;
        if (queued)
            jobs.Attach(f.Context);
        bool seen = false;
        (void)R::ApplyEditorKernelDensityCommand(f.Commands(),
            f.Config,
            [&](const R::EditorKernelDensityResult& r)
            {
                ASSERT_TRUE(f.Last());
                EXPECT_EQ(f.Last()->Status, r.Status);
                EXPECT_EQ(f.Last()->Message, r.Message);
                seen = true;
            });
        if (queued)
            ASSERT_TRUE(jobs.DrainUntilTerminal());
        EXPECT_TRUE(seen);
    }
}

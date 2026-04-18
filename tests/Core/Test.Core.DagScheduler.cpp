#include <gtest/gtest.h>
#include <array>

import Extrinsic.Core.DagScheduler;

using namespace Extrinsic::Core::Dag;

namespace
{
    struct ProducerCtx
    {
        std::array<TaskId, 3> ids{};
        std::array<ResourceId, 1> resources{};
    };

    Extrinsic::Core::Result EmitLinear(void* producerCtx, void* emitCtx, EmitPendingTaskFn emit)
    {
        auto* ctx = static_cast<ProducerCtx*>(producerCtx);

        const ResourceAccess rw{.resource = ctx->resources[0], .mode = ResourceAccessMode::ReadWrite};

        PendingTaskDesc t0{.id = ctx->ids[0], .domain = QueueDomain::Cpu, .estimatedCost = 2};
        if (!emit(emitCtx, t0))
            return Extrinsic::Core::Err(Extrinsic::Core::ErrorCode::InvalidState);

        std::array<TaskId, 1> dep1{ctx->ids[0]};
        PendingTaskDesc t1{
            .id = ctx->ids[1],
            .domain = QueueDomain::Gpu,
            .estimatedCost = 3,
            .dependsOn = std::span<const TaskId>(dep1.data(), dep1.size()),
            .resources = std::span<const ResourceAccess>(&rw, 1)
        };
        if (!emit(emitCtx, t1))
            return Extrinsic::Core::Err(Extrinsic::Core::ErrorCode::InvalidState);

        std::array<TaskId, 1> dep2{ctx->ids[1]};
        PendingTaskDesc t2{
            .id = ctx->ids[2],
            .domain = QueueDomain::Streaming,
            .estimatedCost = 5,
            .dependsOn = std::span<const TaskId>(dep2.data(), dep2.size())
        };
        if (!emit(emitCtx, t2))
            return Extrinsic::Core::Err(Extrinsic::Core::ErrorCode::InvalidState);

        return Extrinsic::Core::Ok();
    }
}

TEST(CoreDagScheduler, BuildsTopologicalPlanAndStats)
{
    auto scheduler = CreateDagScheduler();
    ASSERT_NE(scheduler, nullptr);

    ProducerCtx ctx{
        .ids = {TaskId{1, 1}, TaskId{2, 1}, TaskId{3, 1}},
        .resources = {ResourceId{9, 1}}
    };

    auto producer = scheduler->RegisterProducer(
        ProducerInfo{.name = "unit", .subsystemId = 7, .preferredDomain = QueueDomain::Cpu},
        &ctx,
        &EmitLinear);
    ASSERT_TRUE(producer.has_value());

    ASSERT_TRUE(scheduler->QueryAllPending().has_value());

    BuildConfig cfg{};
    cfg.queueBudgetCpu = 2;
    cfg.queueBudgetGpu = 2;
    cfg.queueBudgetStreaming = 2;

    auto plan = scheduler->BuildSchedule(cfg);
    ASSERT_TRUE(plan.has_value());
    ASSERT_EQ(plan->orderedTasks.size(), 3u);
    EXPECT_EQ(plan->orderedTasks[0].id, ctx.ids[0]);
    EXPECT_EQ(plan->orderedTasks[1].id, ctx.ids[1]);
    EXPECT_EQ(plan->orderedTasks[2].id, ctx.ids[2]);

    const auto stats = scheduler->GetLastStats();
    EXPECT_EQ(stats.producerCount, 1u);
    EXPECT_EQ(stats.taskCount, 3u);
    EXPECT_EQ(stats.edgeCount, 2u);
    EXPECT_GE(stats.criticalPathCost, 10u);
}

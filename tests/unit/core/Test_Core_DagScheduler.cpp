#include <gtest/gtest.h>
#include <array>
#include <string>
#include <span>

import Extrinsic.Core.Dag.Scheduler;
import Extrinsic.Core.Error;

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

    Extrinsic::Core::Result EmitWithMissingDependency(void* producerCtx, void* emitCtx, EmitPendingTaskFn emit)
    {
        auto* ctx = static_cast<ProducerCtx*>(producerCtx);
        std::array<TaskId, 1> missingDep{TaskId{99, 1}};
        PendingTaskDesc t0{
            .id = ctx->ids[0],
            .domain = QueueDomain::Cpu,
            .estimatedCost = 1,
            .dependsOn = std::span<const TaskId>(missingDep.data(), missingDep.size())
        };
        if (!emit(emitCtx, t0))
            return Extrinsic::Core::Err(Extrinsic::Core::ErrorCode::InvalidState);
        return Extrinsic::Core::Ok();
    }

    Extrinsic::Core::Result EmitDuplicateIds(void* producerCtx, void* emitCtx, EmitPendingTaskFn emit)
    {
        auto* ctx = static_cast<ProducerCtx*>(producerCtx);

        PendingTaskDesc t0{.id = ctx->ids[0], .domain = QueueDomain::Cpu, .estimatedCost = 1};
        if (!emit(emitCtx, t0))
            return Extrinsic::Core::Err(Extrinsic::Core::ErrorCode::InvalidState);

        PendingTaskDesc t1{.id = ctx->ids[0], .domain = QueueDomain::Gpu, .estimatedCost = 1};
        if (!emit(emitCtx, t1))
            return Extrinsic::Core::Err(Extrinsic::Core::ErrorCode::InvalidState);

        return Extrinsic::Core::Ok();
    }

    Extrinsic::Core::Result EmitExplicitCycle(void*, void* emitCtx, EmitPendingTaskFn emit)
    {
        const TaskId taskA{200, 1};
        const TaskId taskB{201, 1};
        const std::array<TaskId, 1> depsA{taskB};
        const std::array<TaskId, 1> depsB{taskA};

        PendingTaskDesc a{
            .id = taskA,
            .debugName = "CycleA",
            .domain = QueueDomain::Cpu,
            .dependsOn = std::span<const TaskId>(depsA),
        };
        if (!emit(emitCtx, a))
            return Extrinsic::Core::Err(Extrinsic::Core::ErrorCode::InvalidState);

        PendingTaskDesc b{
            .id = taskB,
            .debugName = "CycleB",
            .domain = QueueDomain::Cpu,
            .dependsOn = std::span<const TaskId>(depsB),
        };
        if (!emit(emitCtx, b))
            return Extrinsic::Core::Err(Extrinsic::Core::ErrorCode::InvalidState);

        return Extrinsic::Core::Ok();
    }

    Extrinsic::Core::Result EmitMixedExplicitAndHazardCycle(void* producerCtx, void* emitCtx, EmitPendingTaskFn emit)
    {
        auto* ctx = static_cast<ProducerCtx*>(producerCtx);
        const std::array<TaskId, 1> depA{ctx->ids[1]};
        const std::array<ResourceAccess, 1> writeAccess{ResourceAccess{.resource = ctx->resources[0], .mode = ResourceAccessMode::Write}};
        const std::array<ResourceAccess, 1> readAccess{ResourceAccess{.resource = ctx->resources[0], .mode = ResourceAccessMode::Read}};

        PendingTaskDesc a{
            .id = ctx->ids[0],
            .debugName = "WriterA",
            .domain = QueueDomain::Cpu,
            .dependsOn = std::span<const TaskId>(depA),
            .resources = std::span<const ResourceAccess>(writeAccess),
        };
        if (!emit(emitCtx, a))
            return Extrinsic::Core::Err(Extrinsic::Core::ErrorCode::InvalidState);

        PendingTaskDesc b{
            .id = ctx->ids[1],
            .debugName = "ReaderB",
            .domain = QueueDomain::Cpu,
            .resources = std::span<const ResourceAccess>(readAccess),
        };
        if (!emit(emitCtx, b))
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
    ASSERT_EQ(plan->size(), 3u);
    EXPECT_EQ((*plan)[0].id, ctx.ids[0]);
    EXPECT_EQ((*plan)[1].id, ctx.ids[1]);
    EXPECT_EQ((*plan)[2].id, ctx.ids[2]);

    const auto stats = scheduler->GetLastStats();
    EXPECT_EQ(stats.producerCount, 1u);
    EXPECT_EQ(stats.taskCount, 3u);
    EXPECT_EQ(stats.edgeCount, 2u);
    EXPECT_GE(stats.criticalPathCost, 10u);
}

TEST(CoreDagScheduler, RejectsMissingDependencyIds)
{
    auto scheduler = CreateDagScheduler();
    ASSERT_NE(scheduler, nullptr);

    ProducerCtx ctx{
        .ids = {TaskId{1, 1}, TaskId{2, 1}, TaskId{3, 1}},
        .resources = {ResourceId{9, 1}}
    };

    auto producer = scheduler->RegisterProducer(
        ProducerInfo{.name = "unit_missing_dep", .subsystemId = 7, .preferredDomain = QueueDomain::Cpu},
        &ctx,
        &EmitWithMissingDependency);
    ASSERT_TRUE(producer.has_value());
    ASSERT_TRUE(scheduler->QueryAllPending().has_value());

    BuildConfig cfg{};
    auto plan = scheduler->BuildSchedule(cfg);
    ASSERT_FALSE(plan.has_value());
    EXPECT_EQ(plan.error(), Extrinsic::Core::ErrorCode::InvalidArgument);
}

TEST(CoreDagScheduler, RejectsDuplicateTaskIds)
{
    auto scheduler = CreateDagScheduler();
    ASSERT_NE(scheduler, nullptr);

    ProducerCtx ctx{
        .ids = {TaskId{1, 1}, TaskId{2, 1}, TaskId{3, 1}},
        .resources = {ResourceId{9, 1}}
    };

    auto producer = scheduler->RegisterProducer(
        ProducerInfo{.name = "unit_duplicate_id", .subsystemId = 7, .preferredDomain = QueueDomain::Cpu},
        &ctx,
        &EmitDuplicateIds);
    ASSERT_TRUE(producer.has_value());
    ASSERT_TRUE(scheduler->QueryAllPending().has_value());

    BuildConfig cfg{};
    auto plan = scheduler->BuildSchedule(cfg);
    ASSERT_FALSE(plan.has_value());
    EXPECT_EQ(plan.error(), Extrinsic::Core::ErrorCode::InvalidArgument);
}


TEST(CoreDagScheduler, RegisterProducerRejectsNullQuery)
{
    auto scheduler = CreateDagScheduler();
    ASSERT_NE(scheduler, nullptr);

    auto producer = scheduler->RegisterProducer(
        ProducerInfo{.name = "null_query", .subsystemId = 1, .preferredDomain = QueueDomain::Cpu},
        nullptr,
        nullptr);
    ASSERT_FALSE(producer.has_value());
    EXPECT_EQ(producer.error(), Extrinsic::Core::ErrorCode::InvalidArgument);
}

TEST(CoreDagScheduler, UnregisterMissingProducerReturnsNotFound)
{
    auto scheduler = CreateDagScheduler();
    ASSERT_NE(scheduler, nullptr);

    auto result = scheduler->UnregisterProducer(ProducerId{999, 1});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Extrinsic::Core::ErrorCode::ResourceNotFound);
}

TEST(CoreDagScheduler, ResetEpochClearsCachedTasks)
{
    auto scheduler = CreateDagScheduler();
    ASSERT_NE(scheduler, nullptr);

    ProducerCtx ctx{
        .ids = {TaskId{10, 1}, TaskId{11, 1}, TaskId{12, 1}},
        .resources = {ResourceId{99, 1}}
    };

    auto producer = scheduler->RegisterProducer(
        ProducerInfo{.name = "unit_reset", .subsystemId = 8, .preferredDomain = QueueDomain::Cpu},
        &ctx,
        &EmitLinear);
    ASSERT_TRUE(producer.has_value());
    ASSERT_TRUE(scheduler->QueryAllPending().has_value());

    auto firstPlan = scheduler->BuildSchedule(BuildConfig{});
    ASSERT_TRUE(firstPlan.has_value());
    ASSERT_EQ(firstPlan->size(), 3u);

    scheduler->ResetEpoch();

    auto secondPlan = scheduler->BuildSchedule(BuildConfig{});
    ASSERT_TRUE(secondPlan.has_value());
    EXPECT_TRUE(secondPlan->empty());

    const auto stats = scheduler->GetLastStats();
    EXPECT_EQ(stats.taskCount, 0u);
}

TEST(CoreDagScheduler, CycleDiagnosticIncludesTaskNamesForExplicitCycle)
{
    auto scheduler = CreateDagScheduler();
    ASSERT_NE(scheduler, nullptr);

    auto producer = scheduler->RegisterProducer(
        ProducerInfo{.name = "unit_cycle_explicit", .subsystemId = 9, .preferredDomain = QueueDomain::Cpu},
        nullptr,
        &EmitExplicitCycle);
    ASSERT_TRUE(producer.has_value());
    ASSERT_TRUE(scheduler->QueryAllPending().has_value());

    const auto plan = scheduler->BuildSchedule(BuildConfig{});
    ASSERT_FALSE(plan.has_value());
    EXPECT_EQ(plan.error(), Extrinsic::Core::ErrorCode::InvalidState);

    const auto stats = scheduler->GetLastStats();
    EXPECT_NE(stats.lastDiagnostic.find("CycleA"), std::string::npos);
    EXPECT_NE(stats.lastDiagnostic.find("CycleB"), std::string::npos);
    EXPECT_NE(stats.lastDiagnostic.find("explicit"), std::string::npos);
}

TEST(CoreDagScheduler, CycleDiagnosticIncludesHazardReason)
{
    auto scheduler = CreateDagScheduler();
    ASSERT_NE(scheduler, nullptr);

    ProducerCtx ctx{
        .ids = {TaskId{210, 1}, TaskId{211, 1}, TaskId{212, 1}},
        .resources = {ResourceId{31, 1}},
    };

    auto producer = scheduler->RegisterProducer(
        ProducerInfo{.name = "unit_cycle_mixed", .subsystemId = 10, .preferredDomain = QueueDomain::Cpu},
        &ctx,
        &EmitMixedExplicitAndHazardCycle);
    ASSERT_TRUE(producer.has_value());
    ASSERT_TRUE(scheduler->QueryAllPending().has_value());

    const auto plan = scheduler->BuildSchedule(BuildConfig{});
    ASSERT_FALSE(plan.has_value());
    EXPECT_EQ(plan.error(), Extrinsic::Core::ErrorCode::InvalidState);

    const auto stats = scheduler->GetLastStats();
    EXPECT_NE(stats.lastDiagnostic.find("WriterA"), std::string::npos);
    EXPECT_NE(stats.lastDiagnostic.find("ReaderB"), std::string::npos);
    EXPECT_NE(stats.lastDiagnostic.find("RAW"), std::string::npos);
    EXPECT_NE(stats.lastDiagnostic.find("explicit"), std::string::npos);
}

// -----------------------------------------------------------------------------
// HLFET scheduling behavior: priority > level > insertion order.
// These tests drive the scheduler through DomainTaskGraph for directness;
// DagScheduler uses the same BuildPlanFromTasks core.
// -----------------------------------------------------------------------------

TEST(CoreDagScheduler, PriorityOverridesInsertionOrder)
{
    auto graph = CreateDomainTaskGraph(QueueDomain::Cpu);
    ASSERT_NE(graph, nullptr);

    // Two independent tasks. Low priority is submitted first; Critical must
    // still come out first per HLFET.
    PendingTaskDesc low{
        .id = TaskId{1, 1}, .domain = QueueDomain::Cpu,
        .priority = TaskPriority::Low,
    };
    PendingTaskDesc critical{
        .id = TaskId{2, 1}, .domain = QueueDomain::Cpu,
        .priority = TaskPriority::Critical,
    };

    ASSERT_TRUE(graph->Submit(low).has_value());
    ASSERT_TRUE(graph->Submit(critical).has_value());

    auto plan = graph->BuildPlan(BuildConfig{});
    ASSERT_TRUE(plan.has_value());
    ASSERT_EQ(plan->size(), 2u);
    EXPECT_EQ((*plan)[0].id, critical.id);
    EXPECT_EQ((*plan)[1].id, low.id);
}

TEST(CoreDagScheduler, LongerCriticalPathBreaksPriorityTie)
{
    auto graph = CreateDomainTaskGraph(QueueDomain::Cpu);
    ASSERT_NE(graph, nullptr);

    // Two roots at equal priority: one heads a 3-node chain, the other is
    // a singleton. The root of the longer chain must be scheduled first
    // (minimizes makespan under critical-path scheduling).
    //
    //   t0 -> t1 -> t2   (chain root t0; level = 3)
    //   t3                (singleton; level = 1)
    const PendingTaskDesc t0{
        .id = TaskId{10, 1}, .domain = QueueDomain::Cpu,
        .priority = TaskPriority::Normal, .estimatedCost = 1,
    };
    const TaskId t0DepArr[1] = {t0.id};
    const PendingTaskDesc t1{
        .id = TaskId{11, 1}, .domain = QueueDomain::Cpu,
        .priority = TaskPriority::Normal, .estimatedCost = 1,
        .dependsOn = std::span<const TaskId>(t0DepArr, 1),
    };
    const TaskId t1DepArr[1] = {t1.id};
    const PendingTaskDesc t2{
        .id = TaskId{12, 1}, .domain = QueueDomain::Cpu,
        .priority = TaskPriority::Normal, .estimatedCost = 1,
        .dependsOn = std::span<const TaskId>(t1DepArr, 1),
    };
    const PendingTaskDesc t3{
        .id = TaskId{13, 1}, .domain = QueueDomain::Cpu,
        .priority = TaskPriority::Normal, .estimatedCost = 1,
    };

    // Submit t3 FIRST so insertion order would otherwise favor it.
    ASSERT_TRUE(graph->Submit(t3).has_value());
    ASSERT_TRUE(graph->Submit(t0).has_value());
    ASSERT_TRUE(graph->Submit(t1).has_value());
    ASSERT_TRUE(graph->Submit(t2).has_value());

    auto plan = graph->BuildPlan(BuildConfig{});
    ASSERT_TRUE(plan.has_value());
    ASSERT_EQ(plan->size(), 4u);
    EXPECT_EQ((*plan)[0].id, t0.id) << "Critical-path root must precede the singleton.";
    EXPECT_EQ((*plan)[1].id, t1.id) << "Chain continues while it still has longer remaining path.";
}

TEST(CoreDagScheduler, StableOrderingOnFullTies)
{
    auto graph = CreateDomainTaskGraph(QueueDomain::Cpu);
    ASSERT_NE(graph, nullptr);

    // Two independent tasks at identical priority and identical level.
    // The lower-insertion (earlier-submitted) one must come out first.
    const PendingTaskDesc a{
        .id = TaskId{100, 1}, .domain = QueueDomain::Cpu,
        .priority = TaskPriority::Normal, .estimatedCost = 1,
    };
    const PendingTaskDesc b{
        .id = TaskId{101, 1}, .domain = QueueDomain::Cpu,
        .priority = TaskPriority::Normal, .estimatedCost = 1,
    };

    ASSERT_TRUE(graph->Submit(a).has_value());
    ASSERT_TRUE(graph->Submit(b).has_value());

    auto plan = graph->BuildPlan(BuildConfig{});
    ASSERT_TRUE(plan.has_value());
    ASSERT_EQ(plan->size(), 2u);
    EXPECT_EQ((*plan)[0].id, a.id);
    EXPECT_EQ((*plan)[1].id, b.id);
}

TEST(CoreDagScheduler, CycleReturnsInvalidState)
{
    auto graph = CreateDomainTaskGraph(QueueDomain::Cpu);
    ASSERT_NE(graph, nullptr);

    // Two-task cycle: t0 -> t1 -> t0. No zero-indegree node exists; Kahn
    // cannot produce a full topological order, so BuildPlan must reject
    // with InvalidState.
    const TaskId id0{200, 1};
    const TaskId id1{201, 1};
    const TaskId dep0[1] = {id1};  // t0 depends on t1
    const TaskId dep1[1] = {id0};  // t1 depends on t0

    const PendingTaskDesc t0{
        .id = id0, .domain = QueueDomain::Cpu,
        .dependsOn = std::span<const TaskId>(dep0, 1),
    };
    const PendingTaskDesc t1{
        .id = id1, .domain = QueueDomain::Cpu,
        .dependsOn = std::span<const TaskId>(dep1, 1),
    };
    ASSERT_TRUE(graph->Submit(t0).has_value());
    ASSERT_TRUE(graph->Submit(t1).has_value());

    auto plan = graph->BuildPlan(BuildConfig{});
    ASSERT_FALSE(plan.has_value());
    EXPECT_EQ(plan.error(), Extrinsic::Core::ErrorCode::InvalidState);
}

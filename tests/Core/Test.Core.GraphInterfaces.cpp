#include <gtest/gtest.h>

#include <span>

import Extrinsic.Core.Dag.Scheduler;
import Extrinsic.Core.Error;

using namespace Extrinsic::Core::Dag;

namespace
{
    constexpr TaskId T(const uint32_t i) { return TaskId{i, 1}; }
}

TEST(CoreGraphInterfaces, CpuGraphAcceptsCpuTasks)
{
    auto graph = CreateCpuTaskGraph();
    ASSERT_NE(graph, nullptr);

    PendingTaskDesc task{.id = T(1), .domain = QueueDomain::Cpu};
    EXPECT_TRUE(graph->Submit(task).has_value());

    BuildConfig cfg{};
    cfg.queueBudgetCpu = 2;
    auto plan = graph->BuildPlan(cfg);
    ASSERT_TRUE(plan.has_value());
    ASSERT_EQ(plan->orderedTasks.size(), 1u);
    EXPECT_EQ(plan->orderedTasks[0].domain, QueueDomain::Cpu);
}

TEST(CoreGraphInterfaces, GpuGraphRejectsWrongDomain)
{
    auto graph = CreateGpuFrameGraph();
    ASSERT_NE(graph, nullptr);

    PendingTaskDesc wrong{.id = T(2), .domain = QueueDomain::Cpu};
    auto r = graph->Submit(wrong);
    ASSERT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), Extrinsic::Core::ErrorCode::InvalidArgument);
}

TEST(CoreGraphInterfaces, StreamingGraphBuildsTopoPlan)
{
    auto graph = CreateAsyncStreamingGraph();
    ASSERT_NE(graph, nullptr);

    PendingTaskDesc a{.id = T(3), .domain = QueueDomain::Streaming};
    TaskId depArr[1] = {T(3)};
    PendingTaskDesc b{.id = T(4), .domain = QueueDomain::Streaming,
                      .dependsOn = std::span<const TaskId>(depArr, 1)};

    ASSERT_TRUE(graph->Submit(a).has_value());
    ASSERT_TRUE(graph->Submit(b).has_value());

    BuildConfig cfg{};
    cfg.queueBudgetStreaming = 2;
    auto plan = graph->BuildPlan(cfg);
    ASSERT_TRUE(plan.has_value());
    ASSERT_EQ(plan->orderedTasks.size(), 2u);
    EXPECT_EQ(plan->orderedTasks[0].id, T(3));
    EXPECT_EQ(plan->orderedTasks[1].id, T(4));
}


TEST(CoreGraphInterfaces, GraphResetClearsSubmittedTasks)
{
    auto graph = CreateCpuTaskGraph();
    ASSERT_NE(graph, nullptr);

    PendingTaskDesc task{.id = T(100), .domain = QueueDomain::Cpu};
    ASSERT_TRUE(graph->Submit(task).has_value());

    auto first = graph->BuildPlan(BuildConfig{});
    ASSERT_TRUE(first.has_value());
    ASSERT_EQ(first->orderedTasks.size(), 1u);

    graph->Reset();

    auto second = graph->BuildPlan(BuildConfig{});
    ASSERT_TRUE(second.has_value());
    EXPECT_TRUE(second->orderedTasks.empty());
}

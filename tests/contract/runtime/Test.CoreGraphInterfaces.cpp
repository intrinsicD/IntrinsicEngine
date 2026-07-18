#include <gtest/gtest.h>

#include <span>

import Extrinsic.Core.Dag.Scheduler;
import Extrinsic.Core.Error;

using namespace Extrinsic::Core::Dag;

namespace
{
    constexpr TaskId T(const uint32_t i) { return TaskId{i, 1}; }
}

TEST(CoreGraphInterfaces, TaskPlanGraphAcceptsNeutralTasks)
{
    auto graph = CreateTaskPlanGraph();
    ASSERT_NE(graph, nullptr);

    const PendingTaskDesc task{.id = T(1), .kind = TaskKind{42u}};
    EXPECT_TRUE(graph->Submit(task).has_value());

    const auto plan = graph->BuildPlan();
    ASSERT_TRUE(plan.has_value());
    ASSERT_EQ(plan->size(), 1u);
    EXPECT_EQ((*plan)[0].id, T(1));
    EXPECT_EQ((*plan)[0].topoOrder, 0u);
    EXPECT_EQ((*plan)[0].batch, 0u);
}

TEST(CoreGraphInterfaces, TaskPlanGraphRejectsInvalidTaskId)
{
    auto graph = CreateTaskPlanGraph();
    ASSERT_NE(graph, nullptr);

    const auto result = graph->Submit(PendingTaskDesc{});
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Extrinsic::Core::ErrorCode::InvalidArgument);
}

TEST(CoreGraphInterfaces, TaskPlanGraphBuildsTopologicalPlan)
{
    auto graph = CreateTaskPlanGraph();
    ASSERT_NE(graph, nullptr);

    const PendingTaskDesc root{.id = T(3)};
    const TaskId dependencies[1] = {root.id};
    const PendingTaskDesc child{
        .id = T(4),
        .dependsOn = std::span<const TaskId>(dependencies, 1),
    };

    ASSERT_TRUE(graph->Submit(root).has_value());
    ASSERT_TRUE(graph->Submit(child).has_value());

    const auto plan = graph->BuildPlan();
    ASSERT_TRUE(plan.has_value());
    ASSERT_EQ(plan->size(), 2u);
    EXPECT_EQ((*plan)[0].id, root.id);
    EXPECT_EQ((*plan)[0].batch, 0u);
    EXPECT_EQ((*plan)[1].id, child.id);
    EXPECT_EQ((*plan)[1].batch, 1u);
}

TEST(CoreGraphInterfaces, TaskPlanGraphResetClearsSubmittedTasks)
{
    auto graph = CreateTaskPlanGraph();
    ASSERT_NE(graph, nullptr);

    ASSERT_TRUE(graph->Submit(PendingTaskDesc{.id = T(100)}).has_value());

    const auto first = graph->BuildPlan();
    ASSERT_TRUE(first.has_value());
    ASSERT_EQ(first->size(), 1u);

    graph->Reset();

    const auto second = graph->BuildPlan();
    ASSERT_TRUE(second.has_value());
    EXPECT_TRUE(second->empty());
}

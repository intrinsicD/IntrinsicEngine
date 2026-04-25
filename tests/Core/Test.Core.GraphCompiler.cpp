#include <gtest/gtest.h>

#include <array>
#include <span>
#include <vector>

import Extrinsic.Core.Dag.Scheduler;
import Extrinsic.Core.Error;

using namespace Extrinsic::Core::Dag;

namespace
{
    [[nodiscard]] PendingTaskDesc MakeCpuTask(TaskId id)
    {
        return PendingTaskDesc{.id = id, .domain = QueueDomain::Cpu};
    }

    [[nodiscard]] std::vector<PlanTask> BuildOrFail(DomainTaskGraph& graph)
    {
        auto plan = graph.BuildPlan(BuildConfig{});
        EXPECT_TRUE(plan.has_value());
        if (!plan.has_value())
            return {};
        return *plan;
    }
}

TEST(CoreGraphCompiler, EmptyGraphCompiles)
{
    auto graph = CreateDomainTaskGraph(QueueDomain::Cpu);
    ASSERT_NE(graph, nullptr);

    const auto plan = BuildOrFail(*graph);
    EXPECT_TRUE(plan.empty());
}

TEST(CoreGraphCompiler, SingleNodeGraphCompiles)
{
    auto graph = CreateDomainTaskGraph(QueueDomain::Cpu);
    ASSERT_NE(graph, nullptr);

    ASSERT_TRUE(graph->Submit(MakeCpuTask(TaskId{1, 1})).has_value());
    const auto plan = BuildOrFail(*graph);
    ASSERT_EQ(plan.size(), 1u);
    EXPECT_EQ(plan[0].id, TaskId{1, 1});
    EXPECT_EQ(plan[0].batch, 0u);
}

TEST(CoreGraphCompiler, IndependentNodesHaveNoOrderingEdges)
{
    auto graph = CreateDomainTaskGraph(QueueDomain::Cpu);
    ASSERT_NE(graph, nullptr);

    ASSERT_TRUE(graph->Submit(MakeCpuTask(TaskId{1, 1})).has_value());
    ASSERT_TRUE(graph->Submit(MakeCpuTask(TaskId{2, 1})).has_value());

    const auto plan = BuildOrFail(*graph);
    ASSERT_EQ(plan.size(), 2u);
    EXPECT_EQ(plan[0].batch, 0u);
    EXPECT_EQ(plan[1].batch, 0u);
}

TEST(CoreGraphCompiler, ExplicitDependenciesProduceTopologicalLayers)
{
    auto graph = CreateDomainTaskGraph(QueueDomain::Cpu);
    ASSERT_NE(graph, nullptr);

    const TaskId a{10, 1};
    const TaskId b{11, 1};
    const TaskId c{12, 1};
    const TaskId d{13, 1};

    const std::array<TaskId, 1> depB{a};
    const std::array<TaskId, 1> depC{a};
    const std::array<TaskId, 2> depD{b, c};

    ASSERT_TRUE(graph->Submit(PendingTaskDesc{.id = a, .domain = QueueDomain::Cpu}).has_value());
    ASSERT_TRUE(graph->Submit(PendingTaskDesc{.id = b, .domain = QueueDomain::Cpu, .dependsOn = std::span<const TaskId>(depB)}).has_value());
    ASSERT_TRUE(graph->Submit(PendingTaskDesc{.id = c, .domain = QueueDomain::Cpu, .dependsOn = std::span<const TaskId>(depC)}).has_value());
    ASSERT_TRUE(graph->Submit(PendingTaskDesc{.id = d, .domain = QueueDomain::Cpu, .dependsOn = std::span<const TaskId>(depD)}).has_value());

    const auto plan = BuildOrFail(*graph);
    ASSERT_EQ(plan.size(), 4u);

    std::array<uint32_t, 4> batches{};
    for (const auto& task : plan)
    {
        if (task.id == a) batches[0] = task.batch;
        if (task.id == b) batches[1] = task.batch;
        if (task.id == c) batches[2] = task.batch;
        if (task.id == d) batches[3] = task.batch;
    }

    EXPECT_EQ(batches[0], 0u);
    EXPECT_EQ(batches[1], 1u);
    EXPECT_EQ(batches[2], 1u);
    EXPECT_EQ(batches[3], 2u);
}

TEST(CoreGraphCompiler, MissingDependencyReturnsInvalidArgument)
{
    auto graph = CreateDomainTaskGraph(QueueDomain::Cpu);
    ASSERT_NE(graph, nullptr);

    const TaskId dep[1] = {TaskId{99, 1}};
    ASSERT_TRUE(graph->Submit(PendingTaskDesc{
        .id = TaskId{1, 1},
        .domain = QueueDomain::Cpu,
        .dependsOn = std::span<const TaskId>(dep, 1),
    }).has_value());

    const auto plan = graph->BuildPlan(BuildConfig{});
    ASSERT_FALSE(plan.has_value());
    EXPECT_EQ(plan.error(), Extrinsic::Core::ErrorCode::InvalidArgument);
}

TEST(CoreGraphCompiler, DuplicateTaskIdReturnsInvalidArgument)
{
    auto graph = CreateDomainTaskGraph(QueueDomain::Cpu);
    ASSERT_NE(graph, nullptr);

    ASSERT_TRUE(graph->Submit(MakeCpuTask(TaskId{1, 1})).has_value());
    ASSERT_TRUE(graph->Submit(MakeCpuTask(TaskId{1, 1})).has_value());

    const auto plan = graph->BuildPlan(BuildConfig{});
    ASSERT_FALSE(plan.has_value());
    EXPECT_EQ(plan.error(), Extrinsic::Core::ErrorCode::InvalidArgument);
}

TEST(CoreGraphCompiler, DeterministicOverRepeatedCompiles)
{
    auto graph = CreateDomainTaskGraph(QueueDomain::Cpu);
    ASSERT_NE(graph, nullptr);

    const std::array<TaskId, 5> ids{TaskId{1, 1}, TaskId{2, 1}, TaskId{3, 1}, TaskId{4, 1}, TaskId{5, 1}};
    const std::array<TaskId, 1> dep2{ids[0]};
    const std::array<TaskId, 1> dep3{ids[0]};
    const std::array<TaskId, 2> dep4{ids[1], ids[2]};

    ASSERT_TRUE(graph->Submit(PendingTaskDesc{.id = ids[0], .domain = QueueDomain::Cpu}).has_value());
    ASSERT_TRUE(graph->Submit(PendingTaskDesc{.id = ids[1], .domain = QueueDomain::Cpu, .dependsOn = std::span<const TaskId>(dep2)}).has_value());
    ASSERT_TRUE(graph->Submit(PendingTaskDesc{.id = ids[2], .domain = QueueDomain::Cpu, .dependsOn = std::span<const TaskId>(dep3)}).has_value());
    ASSERT_TRUE(graph->Submit(PendingTaskDesc{.id = ids[3], .domain = QueueDomain::Cpu, .dependsOn = std::span<const TaskId>(dep4)}).has_value());
    ASSERT_TRUE(graph->Submit(PendingTaskDesc{.id = ids[4], .domain = QueueDomain::Cpu}).has_value());

    std::vector<PlanTask> baseline;
    for (int i = 0; i < 100; ++i)
    {
        const auto current = BuildOrFail(*graph);
        if (i == 0)
        {
            baseline = current;
            continue;
        }

        ASSERT_EQ(current.size(), baseline.size());
        for (size_t k = 0; k < current.size(); ++k)
        {
            EXPECT_EQ(current[k].id, baseline[k].id);
            EXPECT_EQ(current[k].batch, baseline[k].batch);
            EXPECT_EQ(current[k].topoOrder, baseline[k].topoOrder);
        }
    }
}

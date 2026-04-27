#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <string>
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

    [[nodiscard]] uint32_t FindBatchFor(const std::vector<PlanTask>& plan, TaskId id)
    {
        for (const auto& task : plan)
        {
            if (task.id == id)
                return task.batch;
        }
        return std::numeric_limits<uint32_t>::max();
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
    EXPECT_EQ(plan[0].id, (TaskId{1, 1}));
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

TEST(CoreGraphCompiler, LargeCycleDiagnosticIsBounded)
{
    auto graph = CreateDomainTaskGraph(QueueDomain::Cpu);
    ASSERT_NE(graph, nullptr);

    constexpr std::uint32_t kTaskCount = 1024u;
    std::vector<TaskId> ids;
    ids.reserve(kTaskCount);
    for (std::uint32_t i = 0; i < kTaskCount; ++i)
        ids.push_back(TaskId{i, 1});

    for (std::uint32_t i = 0; i < kTaskCount; ++i)
    {
        const TaskId dep = (i == 0u) ? ids.back() : ids[i - 1u];
        const std::array<TaskId, 1> deps{dep};
        const std::string name = "Task_" + std::to_string(i);
        ASSERT_TRUE(graph->Submit(PendingTaskDesc{
            .id = ids[i],
            .debugName = name,
            .domain = QueueDomain::Cpu,
            .dependsOn = std::span<const TaskId>(deps),
        }).has_value());
    }

    const auto plan = graph->BuildPlan(BuildConfig{});
    ASSERT_FALSE(plan.has_value());
    EXPECT_EQ(plan.error(), Extrinsic::Core::ErrorCode::InvalidState);

    const auto stats = graph->GetLastStats();
    EXPECT_NE(stats.lastDiagnostic.find("Cycle detected"), std::string::npos);
    EXPECT_NE(stats.lastDiagnostic.find("Task_0"), std::string::npos);
    EXPECT_NE(stats.lastDiagnostic.find("Task_1"), std::string::npos);
    EXPECT_NE(stats.lastDiagnostic.find("truncated"), std::string::npos);
    EXPECT_LT(stats.lastDiagnostic.size(), 4096u);
}

TEST(CoreGraphCompiler, CycleDiagnosticIncludesEdgeReason)
{
    auto graph = CreateDomainTaskGraph(QueueDomain::Cpu);
    ASSERT_NE(graph, nullptr);

    const TaskId a{300, 1};
    const TaskId b{301, 1};

    const std::array<TaskId, 1> depA{b};
    const std::array<TaskId, 1> depB{a};

    ASSERT_TRUE(graph->Submit(PendingTaskDesc{
        .id = a,
        .debugName = "A",
        .domain = QueueDomain::Cpu,
        .dependsOn = std::span<const TaskId>(depA),
    }).has_value());

    ASSERT_TRUE(graph->Submit(PendingTaskDesc{
        .id = b,
        .debugName = "B",
        .domain = QueueDomain::Cpu,
        .dependsOn = std::span<const TaskId>(depB),
    }).has_value());

    const auto plan = graph->BuildPlan(BuildConfig{});
    ASSERT_FALSE(plan.has_value());
    EXPECT_EQ(plan.error(), Extrinsic::Core::ErrorCode::InvalidState);

    const auto stats = graph->GetLastStats();
    EXPECT_NE(stats.lastDiagnostic.find("Cycle detected"), std::string::npos);
    EXPECT_NE(stats.lastDiagnostic.find("A"), std::string::npos);
    EXPECT_NE(stats.lastDiagnostic.find("B"), std::string::npos);
    EXPECT_NE(stats.lastDiagnostic.find("explicit"), std::string::npos);
}

TEST(CoreGraphCompiler, ResourceHazardRawOrdersWriterBeforeReader)
{
    auto graph = CreateDomainTaskGraph(QueueDomain::Cpu);
    ASSERT_NE(graph, nullptr);

    const TaskId writer{100, 1};
    const TaskId reader{101, 1};
    const ResourceId resource{7, 1};
    const std::array<ResourceAccess, 1> writeAccess{ResourceAccess{.resource = resource, .mode = ResourceAccessMode::Write}};
    const std::array<ResourceAccess, 1> readAccess{ResourceAccess{.resource = resource, .mode = ResourceAccessMode::Read}};

    ASSERT_TRUE(graph->Submit(PendingTaskDesc{.id = writer, .domain = QueueDomain::Cpu, .resources = writeAccess}).has_value());
    ASSERT_TRUE(graph->Submit(PendingTaskDesc{.id = reader, .domain = QueueDomain::Cpu, .resources = readAccess}).has_value());

    const auto plan = BuildOrFail(*graph);
    EXPECT_LT(FindBatchFor(plan, writer), FindBatchFor(plan, reader));
}

TEST(CoreGraphCompiler, ResourceHazardWarOrdersReaderBeforeWriter)
{
    auto graph = CreateDomainTaskGraph(QueueDomain::Cpu);
    ASSERT_NE(graph, nullptr);

    const TaskId reader{110, 1};
    const TaskId writer{111, 1};
    const ResourceId resource{8, 1};
    const std::array<ResourceAccess, 1> readAccess{ResourceAccess{.resource = resource, .mode = ResourceAccessMode::Read}};
    const std::array<ResourceAccess, 1> writeAccess{ResourceAccess{.resource = resource, .mode = ResourceAccessMode::Write}};

    ASSERT_TRUE(graph->Submit(PendingTaskDesc{.id = reader, .domain = QueueDomain::Cpu, .resources = readAccess}).has_value());
    ASSERT_TRUE(graph->Submit(PendingTaskDesc{.id = writer, .domain = QueueDomain::Cpu, .resources = writeAccess}).has_value());

    const auto plan = BuildOrFail(*graph);
    EXPECT_LT(FindBatchFor(plan, reader), FindBatchFor(plan, writer));
}

TEST(CoreGraphCompiler, ResourceHazardRarKeepsReadersParallel)
{
    auto graph = CreateDomainTaskGraph(QueueDomain::Cpu);
    ASSERT_NE(graph, nullptr);

    const TaskId readerA{120, 1};
    const TaskId readerB{121, 1};
    const ResourceId resource{9, 1};
    const std::array<ResourceAccess, 1> readAccess{ResourceAccess{.resource = resource, .mode = ResourceAccessMode::Read}};

    ASSERT_TRUE(graph->Submit(PendingTaskDesc{.id = readerA, .domain = QueueDomain::Cpu, .resources = readAccess}).has_value());
    ASSERT_TRUE(graph->Submit(PendingTaskDesc{.id = readerB, .domain = QueueDomain::Cpu, .resources = readAccess}).has_value());

    const auto plan = BuildOrFail(*graph);
    EXPECT_EQ(FindBatchFor(plan, readerA), FindBatchFor(plan, readerB));
}

TEST(CoreGraphCompiler, ResourceHazardWawOrdersWriters)
{
    auto graph = CreateDomainTaskGraph(QueueDomain::Cpu);
    ASSERT_NE(graph, nullptr);

    const TaskId writerA{122, 1};
    const TaskId writerB{123, 1};
    const ResourceId resource{10, 1};
    const std::array<ResourceAccess, 1> writeAccess{ResourceAccess{.resource = resource, .mode = ResourceAccessMode::Write}};

    ASSERT_TRUE(graph->Submit(PendingTaskDesc{.id = writerA, .domain = QueueDomain::Cpu, .resources = writeAccess}).has_value());
    ASSERT_TRUE(graph->Submit(PendingTaskDesc{.id = writerB, .domain = QueueDomain::Cpu, .resources = writeAccess}).has_value());

    const auto plan = BuildOrFail(*graph);
    EXPECT_LT(FindBatchFor(plan, writerA), FindBatchFor(plan, writerB));
}

TEST(CoreGraphCompiler, MultipleReadersSerializeBeforeWriter)
{
    auto graph = CreateDomainTaskGraph(QueueDomain::Cpu);
    ASSERT_NE(graph, nullptr);

    const TaskId readerA{124, 1};
    const TaskId readerB{125, 1};
    const TaskId writer{126, 1};
    const ResourceId resource{11, 1};
    const std::array<ResourceAccess, 1> readAccess{ResourceAccess{.resource = resource, .mode = ResourceAccessMode::Read}};
    const std::array<ResourceAccess, 1> writeAccess{ResourceAccess{.resource = resource, .mode = ResourceAccessMode::Write}};

    ASSERT_TRUE(graph->Submit(PendingTaskDesc{.id = readerA, .domain = QueueDomain::Cpu, .resources = readAccess}).has_value());
    ASSERT_TRUE(graph->Submit(PendingTaskDesc{.id = readerB, .domain = QueueDomain::Cpu, .resources = readAccess}).has_value());
    ASSERT_TRUE(graph->Submit(PendingTaskDesc{.id = writer, .domain = QueueDomain::Cpu, .resources = writeAccess}).has_value());

    const auto plan = BuildOrFail(*graph);
    const auto writerBatch = FindBatchFor(plan, writer);
    EXPECT_LT(FindBatchFor(plan, readerA), writerBatch);
    EXPECT_LT(FindBatchFor(plan, readerB), writerBatch);
}

TEST(CoreGraphCompiler, WeakReadDoesNotDelayFollowingWriter)
{
    auto graph = CreateDomainTaskGraph(QueueDomain::Cpu);
    ASSERT_NE(graph, nullptr);

    const TaskId weakReader{127, 1};
    const TaskId writer{128, 1};
    const ResourceId resource{12, 1};
    const std::array<ResourceAccess, 1> weakReadAccess{
        ResourceAccess{.resource = resource, .mode = ResourceAccessMode::WeakRead}};
    const std::array<ResourceAccess, 1> writeAccess{
        ResourceAccess{.resource = resource, .mode = ResourceAccessMode::Write}};

    ASSERT_TRUE(graph->Submit(PendingTaskDesc{.id = weakReader, .domain = QueueDomain::Cpu, .resources = weakReadAccess}).has_value());
    ASSERT_TRUE(graph->Submit(PendingTaskDesc{.id = writer, .domain = QueueDomain::Cpu, .resources = writeAccess}).has_value());

    const auto plan = BuildOrFail(*graph);
    EXPECT_EQ(FindBatchFor(plan, weakReader), FindBatchFor(plan, writer));
}

TEST(CoreGraphCompiler, WriteThenWeakReadSerializesWriterBeforeReader)
{
    auto graph = CreateDomainTaskGraph(QueueDomain::Cpu);
    ASSERT_NE(graph, nullptr);

    const TaskId writer{129, 1};
    const TaskId weakReader{130, 1};
    const ResourceId resource{13, 1};
    const std::array<ResourceAccess, 1> writeAccess{
        ResourceAccess{.resource = resource, .mode = ResourceAccessMode::Write}};
    const std::array<ResourceAccess, 1> weakReadAccess{
        ResourceAccess{.resource = resource, .mode = ResourceAccessMode::WeakRead}};

    ASSERT_TRUE(graph->Submit(PendingTaskDesc{.id = writer, .domain = QueueDomain::Cpu, .resources = writeAccess}).has_value());
    ASSERT_TRUE(graph->Submit(PendingTaskDesc{.id = weakReader, .domain = QueueDomain::Cpu, .resources = weakReadAccess}).has_value());

    const auto plan = BuildOrFail(*graph);
    EXPECT_LT(FindBatchFor(plan, writer), FindBatchFor(plan, weakReader));
}

TEST(CoreGraphCompiler, DuplicateResourceAccessesDoNotDuplicateEdges)
{
    const TaskId writer{131, 1};
    const TaskId reader{132, 1};
    const ResourceId resource{14, 1};
    const std::array<ResourceAccess, 2> duplicateWrites{
        ResourceAccess{.resource = resource, .mode = ResourceAccessMode::Write},
        ResourceAccess{.resource = resource, .mode = ResourceAccessMode::Write},
    };
    const std::array<ResourceAccess, 2> duplicateReads{
        ResourceAccess{.resource = resource, .mode = ResourceAccessMode::Read},
        ResourceAccess{.resource = resource, .mode = ResourceAccessMode::Read},
    };

    struct DuplicateAccessProducerCtx
    {
        TaskId writer{};
        TaskId reader{};
        std::array<ResourceAccess, 2> writes{};
        std::array<ResourceAccess, 2> reads{};
    } ctx{
        .writer = writer,
        .reader = reader,
        .writes = duplicateWrites,
        .reads = duplicateReads,
    };

    const auto emitDuplicateAccesses = [](void* producerCtx, void* emitCtx, EmitPendingTaskFn emit) -> Extrinsic::Core::Result
    {
        auto* localCtx = static_cast<DuplicateAccessProducerCtx*>(producerCtx);
        PendingTaskDesc a{.id = localCtx->writer, .domain = QueueDomain::Cpu, .resources = localCtx->writes};
        PendingTaskDesc b{.id = localCtx->reader, .domain = QueueDomain::Cpu, .resources = localCtx->reads};
        if (!emit(emitCtx, a) || !emit(emitCtx, b))
            return Extrinsic::Core::Err(Extrinsic::Core::ErrorCode::InvalidState);
        return Extrinsic::Core::Ok();
    };

    auto scheduler = CreateDagScheduler();
    ASSERT_NE(scheduler, nullptr);
    const auto producer = scheduler->RegisterProducer(
        ProducerInfo{.name = "duplicate_access_hazards", .subsystemId = 1, .preferredDomain = QueueDomain::Cpu},
        &ctx,
        emitDuplicateAccesses);
    ASSERT_TRUE(producer.has_value());
    ASSERT_TRUE(scheduler->QueryAllPending().has_value());

    const auto plan = scheduler->BuildSchedule(BuildConfig{});
    ASSERT_TRUE(plan.has_value());
    EXPECT_EQ(scheduler->GetLastStats().edgeCount, 1u);
}

TEST(CoreGraphCompiler, ResourceHazardsContributeToEdgeStats)
{
    const TaskId writer{130, 1};
    const TaskId reader{131, 1};
    const ResourceId resource{10, 1};
    const std::array<ResourceAccess, 1> writeAccess{ResourceAccess{.resource = resource, .mode = ResourceAccessMode::Write}};
    const std::array<ResourceAccess, 1> readAccess{ResourceAccess{.resource = resource, .mode = ResourceAccessMode::Read}};

    struct HazardProducerCtx
    {
        TaskId writer{};
        TaskId reader{};
        std::array<ResourceAccess, 1> write{};
        std::array<ResourceAccess, 1> read{};
    } ctx{
        .writer = writer,
        .reader = reader,
        .write = writeAccess,
        .read = readAccess,
    };

    const auto emitHazard = [](void* producerCtx, void* emitCtx, EmitPendingTaskFn emit) -> Extrinsic::Core::Result
    {
        auto* localCtx = static_cast<HazardProducerCtx*>(producerCtx);
        PendingTaskDesc a{.id = localCtx->writer, .domain = QueueDomain::Cpu, .resources = localCtx->write};
        PendingTaskDesc b{.id = localCtx->reader, .domain = QueueDomain::Cpu, .resources = localCtx->read};
        if (!emit(emitCtx, a) || !emit(emitCtx, b))
            return Extrinsic::Core::Err(Extrinsic::Core::ErrorCode::InvalidState);
        return Extrinsic::Core::Ok();
    };

    auto scheduler = CreateDagScheduler();
    ASSERT_NE(scheduler, nullptr);
    const auto producer = scheduler->RegisterProducer(
        ProducerInfo{.name = "hazard_stats", .subsystemId = 1, .preferredDomain = QueueDomain::Cpu},
        &ctx,
        emitHazard);
    ASSERT_TRUE(producer.has_value());
    ASSERT_TRUE(scheduler->QueryAllPending().has_value());

    const auto plan = scheduler->BuildSchedule(BuildConfig{});
    ASSERT_TRUE(plan.has_value());
    EXPECT_GE(scheduler->GetLastStats().edgeCount, 1u);
}

TEST(CoreGraphCompiler, DomainTaskGraphReportsExplicitAndHazardEdgeStats)
{
    auto graph = CreateDomainTaskGraph(QueueDomain::Cpu);
    ASSERT_NE(graph, nullptr);

    const TaskId a{200, 1};
    const TaskId b{201, 1};
    const TaskId c{202, 1};
    const ResourceId resource{20, 1};
    const std::array<TaskId, 1> depB{a};
    const std::array<ResourceAccess, 1> writeAccess{
        ResourceAccess{.resource = resource, .mode = ResourceAccessMode::Write}};
    const std::array<ResourceAccess, 1> readAccess{
        ResourceAccess{.resource = resource, .mode = ResourceAccessMode::Read}};

    ASSERT_TRUE(graph->Submit(PendingTaskDesc{.id = a, .domain = QueueDomain::Cpu, .resources = writeAccess}).has_value());
    ASSERT_TRUE(graph->Submit(PendingTaskDesc{
        .id = b,
        .domain = QueueDomain::Cpu,
        .dependsOn = std::span<const TaskId>(depB),
    }).has_value());
    ASSERT_TRUE(graph->Submit(PendingTaskDesc{.id = c, .domain = QueueDomain::Cpu, .resources = readAccess}).has_value());

    const auto plan = graph->BuildPlan(BuildConfig{});
    ASSERT_TRUE(plan.has_value());

    const auto stats = graph->GetLastStats();
    EXPECT_EQ(stats.taskCount, 3u);
    EXPECT_EQ(stats.explicitEdgeCount, 1u);
    EXPECT_EQ(stats.hazardEdgeCount, 1u);
    EXPECT_EQ(stats.edgeCount, stats.explicitEdgeCount + stats.hazardEdgeCount);
    EXPECT_GE(stats.layerCount, 2u);
}

TEST(CoreGraphCompiler, DomainTaskGraphLaneAssignmentRespectsQueueBudgets)
{
    constexpr uint32_t kTaskCount = 6;
    const BuildConfig config{
        .queueBudgetCpu = 2,
        .queueBudgetGpu = 3,
        .queueBudgetStreaming = 4,
    };

    auto assertLaneCycling = [&](const QueueDomain domain, const uint32_t expectedLaneModulus)
    {
        auto graph = CreateDomainTaskGraph(domain);
        ASSERT_NE(graph, nullptr);

        for (uint32_t i = 0; i < kTaskCount; ++i)
        {
            ASSERT_TRUE(graph->Submit(PendingTaskDesc{
                .id = TaskId{1000u + static_cast<uint32_t>(domain) * 100u + i, 1},
                .domain = domain,
            }).has_value());
        }

        const auto plan = graph->BuildPlan(config);
        ASSERT_TRUE(plan.has_value());
        ASSERT_EQ(plan->size(), kTaskCount);

        for (uint32_t i = 0; i < kTaskCount; ++i)
            EXPECT_LT((*plan)[i].lane, expectedLaneModulus);
    };

    assertLaneCycling(QueueDomain::Cpu, config.queueBudgetCpu);
    assertLaneCycling(QueueDomain::Gpu, config.queueBudgetGpu);
    assertLaneCycling(QueueDomain::Streaming, config.queueBudgetStreaming);
}

TEST(CoreGraphCompiler, ResourceHazardStress_10000_NodesCompilesDeterministically)
{
    constexpr std::uint32_t kTaskCount = 10'000u;
    constexpr ResourceId kResource{7, 1};

    auto graph = CreateDomainTaskGraph(QueueDomain::Cpu);
    ASSERT_NE(graph, nullptr);

    std::vector<ResourceAccess> accesses{};
    accesses.reserve(kTaskCount);
    for (std::uint32_t i = 0; i < kTaskCount; ++i)
    {
        accesses.push_back(ResourceAccess{
            .resource = kResource,
            .mode = (i % 2u == 0u) ? ResourceAccessMode::Write : ResourceAccessMode::Read,
        });

        ASSERT_TRUE(graph->Submit(PendingTaskDesc{
            .id = TaskId{i, 1},
            .domain = QueueDomain::Cpu,
            .resources = std::span<const ResourceAccess>(accesses.data() + i, 1u),
        }).has_value());
    }

    const auto firstPlan = BuildOrFail(*graph);
    EXPECT_EQ(firstPlan.size(), kTaskCount);
    EXPECT_GT(firstPlan.back().batch, 0u);

    std::vector<uint32_t> expectedOrder;
    expectedOrder.reserve(kTaskCount);
    for (const auto& task : firstPlan)
        expectedOrder.push_back(task.id.Index);

    std::vector<uint32_t> expectedBatches;
    expectedBatches.reserve(kTaskCount);
    for (const auto& task : firstPlan)
        expectedBatches.push_back(task.batch);

    for (int repeat = 0; repeat < 10; ++repeat)
    {
        const auto plan = BuildOrFail(*graph);
        ASSERT_EQ(plan.size(), kTaskCount);
        for (std::size_t i = 0; i < kTaskCount; ++i)
        {
            EXPECT_EQ(plan[i].id.Index, expectedOrder[i]);
            EXPECT_EQ(plan[i].batch, expectedBatches[i]);
        }
    }
}

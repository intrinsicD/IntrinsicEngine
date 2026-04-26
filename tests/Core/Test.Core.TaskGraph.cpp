#include <gtest/gtest.h>

#include <cstddef>
#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

import Extrinsic.Core.Dag.TaskGraph;
import Extrinsic.Core.Dag.Scheduler;
import Extrinsic.Core.Error;
import Extrinsic.Core.FrameGraph;
import Extrinsic.Core.Hash;

using namespace Extrinsic::Core;
using namespace Extrinsic::Core::Dag;
using namespace Extrinsic::Core::Hash;

namespace
{
    struct Position {};
    struct Velocity {};
    struct Acceleration {};
    struct Temperature {};

    std::ptrdiff_t IndexOf(const std::vector<std::string>& log, const std::string& name)
    {
        auto it = std::find(log.begin(), log.end(), name);
        return (it != log.end()) ? std::distance(log.begin(), it) : -1;
    }

    void ExpectOrder(const std::vector<std::string>& log,
                     const std::string& before, const std::string& after)
    {
        const auto a = IndexOf(log, before);
        const auto b = IndexOf(log, after);
        ASSERT_GE(a, 0) << before << " not found in log";
        ASSERT_GE(b, 0) << after << " not found in log";
        EXPECT_LT(a, b) << before << " should execute before " << after;
    }
}

TEST(CoreTaskGraph, CpuExecuteHonorsResourceAndLabelDependencies)
{
    TaskGraph graph(QueueDomain::Cpu);
    std::vector<std::string> log;

    graph.AddPass("Input",
        [](TaskGraphBuilder& b) { b.Write<Position>(); },
        [&]() { log.emplace_back("Input"); });

    graph.AddPass("Physics",
        [](TaskGraphBuilder& b) { b.Read<Position>(); b.Write<Velocity>(); },
        [&]() { log.emplace_back("Physics"); });

    graph.AddPass("Sync",
        [](TaskGraphBuilder& b) { b.Read<Velocity>(); b.Signal("PhysicsDone"_id); },
        [&]() { log.emplace_back("Sync"); });

    graph.AddPass("Render",
        [](TaskGraphBuilder& b) { b.WaitFor("PhysicsDone"_id); b.Read<Velocity>(); },
        [&]() { log.emplace_back("Render"); });

    ASSERT_TRUE(graph.Compile().has_value()) << "Compile failed";
    EXPECT_EQ(graph.PassCount(), 4u);
    EXPECT_EQ(graph.PassName(0), "Input");
    EXPECT_EQ(graph.PassName(3), "Render");

    const auto plan = graph.BuildPlan();
    ASSERT_TRUE(plan.has_value()) << "BuildPlan failed";
    ASSERT_EQ(plan->size(), 4u);
    for (uint32_t i = 0; i < plan->size(); ++i)
    {
        EXPECT_EQ((*plan)[i].id, (TaskId{i, 1}));
        EXPECT_EQ((*plan)[i].domain, QueueDomain::Cpu);
        EXPECT_EQ((*plan)[i].topoOrder, i);
    }

    ASSERT_TRUE(graph.Execute().has_value()) << "Execute failed";
    ASSERT_EQ(log.size(), 4u);
    ExpectOrder(log, "Input", "Physics");
    ExpectOrder(log, "Physics", "Sync");
    ExpectOrder(log, "Sync", "Render");
}

TEST(CoreTaskGraph, CpuIndependentPassesStayInInsertionOrder)
{
    TaskGraph graph(QueueDomain::Cpu);
    std::vector<uint32_t> visits;
    constexpr uint32_t kCount = 256;
    visits.reserve(kCount);

    for (uint32_t i = 0; i < kCount; ++i)
    {
        const std::string name = "Pass_" + std::to_string(i);
        graph.AddPass(name,
            [](TaskGraphBuilder&) {},
            [&, i]() { visits.push_back(i); });
    }

    ASSERT_TRUE(graph.Compile().has_value()) << "Compile failed";

    const auto plan = graph.BuildPlan();
    ASSERT_TRUE(plan.has_value()) << "BuildPlan failed";
    ASSERT_EQ(plan->size(), kCount);

    for (uint32_t i = 0; i < kCount; ++i)
    {
        EXPECT_EQ((*plan)[i].id, (TaskId{i, 1}));
        EXPECT_EQ((*plan)[i].domain, QueueDomain::Cpu);
        EXPECT_EQ((*plan)[i].topoOrder, i);
        EXPECT_EQ((*plan)[i].batch, 0u);
    }

    ASSERT_TRUE(graph.Execute().has_value()) << "Execute failed";
    ASSERT_EQ(visits.size(), kCount);
    for (uint32_t i = 0; i < kCount; ++i)
        EXPECT_EQ(visits[i], i) << "Execution order drifted at index " << i;
}

TEST(CoreTaskGraph, WaitDependsOnAllPriorSignalers)
{
    TaskGraph graph(QueueDomain::Cpu);
    std::vector<std::string> log;

    graph.AddPass("SignalA",
        [](TaskGraphBuilder& b) { b.Signal("Gate"_id); },
        [&]() { log.emplace_back("SignalA"); });

    graph.AddPass("SignalB",
        [](TaskGraphBuilder& b) { b.Signal("Gate"_id); },
        [&]() { log.emplace_back("SignalB"); });

    graph.AddPass("Waiter",
        [](TaskGraphBuilder& b) { b.WaitFor("Gate"_id); },
        [&]() { log.emplace_back("Waiter"); });

    ASSERT_TRUE(graph.Compile().has_value()) << "Compile failed";
    ASSERT_TRUE(graph.Execute().has_value()) << "Execute failed";
    ASSERT_EQ(log.size(), 3u);
    ExpectOrder(log, "SignalA", "Waiter");
    ExpectOrder(log, "SignalB", "Waiter");
}

TEST(CoreTaskGraph, WaitWithoutPriorSignalFailsCompile)
{
    TaskGraph graph(QueueDomain::Cpu);

    graph.AddPass("Waiter",
        [](TaskGraphBuilder& b) { b.WaitFor("NeverSignaled"_id); },
        []() {});

    graph.AddPass("LateSignal",
        [](TaskGraphBuilder& b) { b.Signal("NeverSignaled"_id); },
        []() {});

    const auto compile = graph.Compile();
    ASSERT_FALSE(compile.has_value());
    EXPECT_EQ(compile.error(), ErrorCode::InvalidState);
}

TEST(CoreTaskGraph, IndependentLabelsDoNotInterfere)
{
    TaskGraph graph(QueueDomain::Cpu);
    std::vector<std::string> log;

    graph.AddPass("SignalAlpha",
        [](TaskGraphBuilder& b) { b.Signal("Alpha"_id); },
        [&]() { log.emplace_back("SignalAlpha"); });

    graph.AddPass("SignalBeta",
        [](TaskGraphBuilder& b) { b.Signal("Beta"_id); },
        [&]() { log.emplace_back("SignalBeta"); });

    graph.AddPass("WaitAlpha",
        [](TaskGraphBuilder& b) { b.WaitFor("Alpha"_id); },
        [&]() { log.emplace_back("WaitAlpha"); });

    graph.AddPass("WaitBeta",
        [](TaskGraphBuilder& b) { b.WaitFor("Beta"_id); },
        [&]() { log.emplace_back("WaitBeta"); });

    ASSERT_TRUE(graph.Compile().has_value()) << "Compile failed";
    ASSERT_TRUE(graph.Execute().has_value()) << "Execute failed";
    ASSERT_EQ(log.size(), 4u);
    ExpectOrder(log, "SignalAlpha", "WaitAlpha");
    ExpectOrder(log, "SignalBeta", "WaitBeta");
}

TEST(CoreTaskGraph, GpuDomainBuildPlanIsStableAndExecuteIsRejected)
{
    TaskGraph graph(QueueDomain::Gpu);

    graph.AddPass("Upload",
        [](TaskGraphBuilder& b) { b.Write<Position>(); },
        []() {});

    graph.AddPass("Draw",
        [](TaskGraphBuilder& b) { b.Read<Position>(); b.Write<Temperature>(); },
        []() {});

    ASSERT_TRUE(graph.Compile().has_value()) << "Compile failed";

    const auto plan = graph.BuildPlan();
    ASSERT_TRUE(plan.has_value()) << "BuildPlan failed";
    ASSERT_EQ(plan->size(), 2u);
    EXPECT_EQ((*plan)[0].id, (TaskId{0, 1}));
    EXPECT_EQ((*plan)[1].id, (TaskId{1, 1}));
    EXPECT_EQ((*plan)[0].domain, QueueDomain::Gpu);
    EXPECT_EQ((*plan)[1].domain, QueueDomain::Gpu);
    EXPECT_EQ((*plan)[0].topoOrder, 0u);
    EXPECT_EQ((*plan)[1].topoOrder, 1u);
    EXPECT_EQ((*plan)[0].batch, 0u);
    EXPECT_EQ((*plan)[1].batch, 1u);

    auto exec = graph.Execute();
    ASSERT_FALSE(exec.has_value());
    EXPECT_EQ(exec.error(), ErrorCode::InvalidState);
}

TEST(CoreTaskGraph, StreamingDomainBuildPlanAndResetReuse)
{
    TaskGraph graph(QueueDomain::Streaming);

    graph.AddPass("Fetch",
        [](TaskGraphBuilder& b) { b.Write<Acceleration>(); },
        []() {});

    graph.AddPass("Decode",
        [](TaskGraphBuilder& b) { b.Read<Acceleration>(); b.Write<Temperature>(); },
        []() {});

    ASSERT_TRUE(graph.Compile().has_value()) << "Compile failed";
    ASSERT_EQ(graph.PassCount(), 2u);
    EXPECT_EQ(graph.PassName(0), "Fetch");
    EXPECT_EQ(graph.PassName(1), "Decode");

    const auto firstPlan = graph.BuildPlan();
    ASSERT_TRUE(firstPlan.has_value()) << "BuildPlan failed";
    ASSERT_EQ(firstPlan->size(), 2u);
    EXPECT_EQ((*firstPlan)[0].domain, QueueDomain::Streaming);
    EXPECT_EQ((*firstPlan)[1].domain, QueueDomain::Streaming);
    EXPECT_EQ((*firstPlan)[0].batch, 0u);
    EXPECT_EQ((*firstPlan)[1].batch, 1u);

    graph.Reset();
    EXPECT_EQ(graph.PassCount(), 0u);
    EXPECT_TRUE(graph.PassName(0).empty());

    graph.AddPass("Upload",
        [](TaskGraphBuilder& b) { b.Write<Position>(); },
        []() {});

    ASSERT_TRUE(graph.Compile().has_value()) << "Compile after reset failed";
    const auto secondPlan = graph.BuildPlan();
    ASSERT_TRUE(secondPlan.has_value()) << "BuildPlan after reset failed";
    ASSERT_EQ(secondPlan->size(), 1u);
    EXPECT_EQ((*secondPlan)[0].id, (TaskId{0, 1}));
    EXPECT_EQ((*secondPlan)[0].domain, QueueDomain::Streaming);
}

TEST(CoreTaskGraph, BuildPlanBatchesMatchExecutionLayers)
{
    TaskGraph graph(QueueDomain::Cpu);

    graph.AddPass("WritePos",
        [](TaskGraphBuilder& b) { b.Write<Position>(); },
        []() {});
    graph.AddPass("ReadPosWriteVel",
        [](TaskGraphBuilder& b) { b.Read<Position>(); b.Write<Velocity>(); },
        []() {});
    graph.AddPass("ReadVel",
        [](TaskGraphBuilder& b) { b.Read<Velocity>(); },
        []() {});

    ASSERT_TRUE(graph.Compile().has_value()) << "Compile failed";
    const auto plan = graph.BuildPlan();
    ASSERT_TRUE(plan.has_value()) << "BuildPlan failed";

    const auto& layers = graph.GetExecutionLayers();
    ASSERT_FALSE(layers.empty());

    for (const auto& task : *plan)
    {
        ASSERT_LT(task.batch, layers.size());
        const auto& layer = layers[task.batch];
        EXPECT_NE(std::find(layer.begin(), layer.end(), task.id.Index), layer.end());
    }
}

TEST(CoreTaskGraph, TakePassExecuteMovesOnlyTargetClosure)
{
    TaskGraph graph(QueueDomain::Streaming);
    std::vector<std::string> log;

    graph.AddPass("PassA", []([[maybe_unused]] TaskGraphBuilder& b) {}, [&]() { log.emplace_back("A"); });
    graph.AddPass("PassB", []([[maybe_unused]] TaskGraphBuilder& b) {}, [&]() { log.emplace_back("B"); });

    ASSERT_TRUE(graph.Compile().has_value()) << "Compile failed";

    auto moved = graph.TakePassExecute(0u);
    ASSERT_TRUE(static_cast<bool>(moved));
    moved();

    graph.ExecutePass(0u);
    graph.ExecutePass(1u);

    ASSERT_EQ(log.size(), 2u);
    EXPECT_EQ(log[0], "A");
    EXPECT_EQ(log[1], "B");
}

TEST(CoreTaskGraph, ResetClearsStatsResourcesAndLabelsAcrossEpochs)
{
    TaskGraph graph(QueueDomain::Cpu);
    std::vector<std::string> log;

    graph.AddPass("Writer",
        [](TaskGraphBuilder& b) { b.WriteResource("Shared"_id); b.Signal("Done"_id); },
        [&]() { log.emplace_back("Writer"); });
    graph.AddPass("Reader",
        [](TaskGraphBuilder& b) { b.ReadResource("Shared"_id); b.WaitFor("Done"_id); },
        [&]() { log.emplace_back("Reader"); });

    ASSERT_TRUE(graph.Compile().has_value()) << "Compile failed";
    ASSERT_TRUE(graph.Execute().has_value()) << "Execute failed";
    const auto firstStats = graph.GetScheduleStats();
    EXPECT_GT(firstStats.edgeCount, 0u);

    graph.Reset();
    EXPECT_EQ(graph.PassCount(), 0u);
    EXPECT_EQ(graph.GetScheduleStats().edgeCount, 0u);

    graph.AddPass("OnlyPass",
        []([[maybe_unused]] TaskGraphBuilder& b) {},
        [&]() { log.emplace_back("OnlyPass"); });

    ASSERT_TRUE(graph.Compile().has_value()) << "Compile after reset failed";
    ASSERT_TRUE(graph.Execute().has_value()) << "Execute after reset failed";

    EXPECT_EQ(graph.PassCount(), 1u);
    EXPECT_EQ(graph.GetScheduleStats().taskCount, 1u);
    ASSERT_EQ(log.size(), 3u);
    EXPECT_EQ(log[2], "OnlyPass");
}

TEST(CoreFrameGraph, ResetRebuildsTheCpuGraphCleanly)
{
    FrameGraph graph;
    std::vector<std::string> log;

    graph.AddPass("First",
        [](FrameGraphBuilder& b) { b.Write<Position>(); },
        [&]() { log.emplace_back("First"); });

    ASSERT_TRUE(graph.Compile().has_value()) << "Compile failed";
    ASSERT_TRUE(graph.Execute().has_value()) << "Execute failed";
    EXPECT_EQ(graph.PassCount(), 1u);
    EXPECT_EQ(graph.PassName(0), "First");
    ASSERT_EQ(log.size(), 1u);

    graph.Reset();
    EXPECT_EQ(graph.PassCount(), 0u);
    EXPECT_TRUE(graph.PassName(0).empty());

    graph.AddPass("Second",
        [](FrameGraphBuilder& b) { b.Write<Velocity>(); },
        [&]() { log.emplace_back("Second"); });

    ASSERT_TRUE(graph.Compile().has_value()) << "Compile after reset failed";
    ASSERT_TRUE(graph.Execute().has_value()) << "Execute after reset failed";
    EXPECT_EQ(graph.PassCount(), 1u);
    EXPECT_EQ(graph.PassName(0), "Second");
    ASSERT_EQ(log.size(), 2u);
    ExpectOrder(log, "First", "Second");
}

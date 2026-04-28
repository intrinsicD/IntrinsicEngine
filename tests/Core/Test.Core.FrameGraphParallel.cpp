#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

import Extrinsic.Core.Dag.Scheduler;
import Extrinsic.Core.FrameGraph;
import Extrinsic.Core.Hash;
import Extrinsic.Core.Tasks;

using namespace Extrinsic::Core;
using namespace Extrinsic::Core::Dag;
using namespace Extrinsic::Core::Hash;

namespace
{
    struct Position {};
    struct Velocity {};

    void UpdateMax(std::atomic<int>& current, std::atomic<int>& maxActive)
    {
        const auto observed = current.load(std::memory_order_relaxed);
        int expected = maxActive.load(std::memory_order_relaxed);
        while (observed > expected &&
               !maxActive.compare_exchange_weak(expected, observed, std::memory_order_acq_rel))
        {
        }
    }

    void DelayWithActive(std::atomic<int>& active,
                         std::atomic<int>& maxActive,
                         std::chrono::milliseconds delay)
    {
        const int now = active.fetch_add(1, std::memory_order_acq_rel) + 1;
        const int previous = maxActive.load(std::memory_order_relaxed);
        if (now > previous)
            UpdateMax(active, maxActive);
        std::this_thread::sleep_for(delay);
        active.fetch_sub(1, std::memory_order_acq_rel);
    }
}

TEST(FrameGraphParallel, DefaultOverloadStillWorks)
{
    FrameGraph graph;
    std::vector<std::string> log;

    graph.AddPass("A", [&]() { log.push_back("A"); });
    graph.AddPass("B", [&]() { log.push_back("B"); });

    ASSERT_TRUE(graph.Compile().has_value()) << "Compile failed";
    ASSERT_TRUE(graph.Execute().has_value()) << "Execute failed";
    EXPECT_EQ(log.size(), 2u);
    EXPECT_EQ(log[0], "A");
    EXPECT_EQ(log[1], "B");
}

TEST(FrameGraphParallel, TypedReadReadCanRunInParallel)
{
    FrameGraph graph;
    Tasks::Scheduler::Initialize(4);

    std::atomic<int> active{0};
    std::atomic<int> maxActive{0};

    graph.AddPass("Reader1",
                  FrameGraphPassOptions{},
                  [](FrameGraphBuilder& b) { b.Read<Position>(); },
                  [&]()
                  {
                      DelayWithActive(active, maxActive, std::chrono::milliseconds(25));
                  });

    graph.AddPass("Reader2",
                  FrameGraphPassOptions{},
                  [](FrameGraphBuilder& b) { b.Read<Position>(); },
                  [&]()
                  {
                      DelayWithActive(active, maxActive, std::chrono::milliseconds(25));
                  });

    graph.AddPass("Reader3",
                  FrameGraphPassOptions{},
                  [](FrameGraphBuilder& b) { b.Read<Position>(); },
                  [&]()
                  {
                      DelayWithActive(active, maxActive, std::chrono::milliseconds(25));
                  });

    ASSERT_TRUE(graph.Compile().has_value()) << "Compile failed";
    ASSERT_TRUE(graph.Execute().has_value()) << "Execute failed";

    EXPECT_GE(maxActive.load(std::memory_order_relaxed), 2);
    EXPECT_GE(active.load(std::memory_order_relaxed), 0);

    Tasks::Scheduler::Shutdown();
}

TEST(FrameGraphParallel, PriorityAffectsReadyNodeOrdering)
{
    FrameGraph graph;

    std::vector<std::string> log;

    FrameGraphPassOptions low;
    low.Priority = TaskPriority::Low;

    FrameGraphPassOptions critical;
    critical.Priority = TaskPriority::Critical;

    FrameGraphPassOptions high;
    high.Priority = TaskPriority::High;

    graph.AddPass("Low", low, [](FrameGraphBuilder&) {}, [&]() { log.push_back("Low"); });
    graph.AddPass("High", high, [](FrameGraphBuilder&) {}, [&]() { log.push_back("High"); });
    graph.AddPass("Critical", critical, [](FrameGraphBuilder&) {}, [&]() { log.push_back("Critical"); });

    const auto plan = graph.BuildPlan();
    ASSERT_TRUE(plan.has_value()) << "BuildPlan failed";
    ASSERT_EQ(plan->size(), 3u);

    // All independent; explicit priority should dominate schedule order.
    EXPECT_EQ((*plan)[0].topoOrder, 0u);
    EXPECT_EQ((*plan)[1].topoOrder, 1u);
    EXPECT_EQ((*plan)[2].topoOrder, 2u);
    EXPECT_EQ(graph.PassName((*plan)[0].id.Index), "Critical");

    ASSERT_TRUE(graph.Execute().has_value()) << "Execute failed";
    ASSERT_EQ(log.size(), 3u);
    EXPECT_EQ(log[0], "Critical");
}

TEST(FrameGraphParallel, EstimatedCostAffectsCriticalPathTieBreaker)
{
    FrameGraph graph;
    std::vector<std::string> log;

    FrameGraphPassOptions cheap;
    cheap.EstimatedCost = 1;

    FrameGraphPassOptions expensive;
    expensive.EstimatedCost = 20;

    graph.AddPass("Cheap", cheap,
                  [](FrameGraphBuilder&) {},
                  [&]() { log.push_back("Cheap"); });
    graph.AddPass("Expensive", expensive,
                  [](FrameGraphBuilder&) {},
                  [&]() { log.push_back("Expensive"); });

    const auto plan = graph.BuildPlan();
    ASSERT_TRUE(plan.has_value()) << "BuildPlan failed";
    ASSERT_EQ(plan->size(), 2u);

    // With equal priorities and no explicit dependencies, high estimated-cost node should run first.
    EXPECT_EQ((*plan)[0].id.Index, 1u);

    ASSERT_TRUE(graph.Execute().has_value()) << "Execute failed";
    ASSERT_EQ(log.size(), 2u);
    EXPECT_EQ(log[0], "Expensive");
}

TEST(FrameGraphParallel, TypedWriteReadMustSerialize)
{
    FrameGraph graph;

    std::vector<std::string> log;

    graph.AddPass("Writer",
                  [](FrameGraphBuilder& b) { b.Write<Position>(); },
                  [&]() { log.push_back("Writer"); });

    graph.AddPass("Reader",
                  [](FrameGraphBuilder& b) { b.Read<Position>(); },
                  [&]() { log.push_back("Reader"); });

    ASSERT_TRUE(graph.Compile().has_value()) << "Compile failed";
    ASSERT_TRUE(graph.Execute().has_value()) << "Execute failed";

    ASSERT_EQ(log.size(), 2u);
    EXPECT_EQ(log[0], "Writer");
    EXPECT_EQ(log[1], "Reader");
}

TEST(FrameGraphParallel, TypedReadWriteMustSerialize)
{
    FrameGraph graph;

    std::vector<std::string> log;

    graph.AddPass("Reader",
                  [](FrameGraphBuilder& b) { b.Read<Velocity>(); },
                  [&]() { log.push_back("Reader"); });

    graph.AddPass("Writer",
                  [](FrameGraphBuilder& b) { b.Write<Velocity>(); },
                  [&]() { log.push_back("Writer"); });

    ASSERT_TRUE(graph.Compile().has_value()) << "Compile failed";
    ASSERT_TRUE(graph.Execute().has_value()) << "Execute failed";

    ASSERT_EQ(log.size(), 2u);
    EXPECT_EQ(log[0], "Reader");
    EXPECT_EQ(log[1], "Writer");
}

TEST(FrameGraphParallel, StructuralTokenSerializesStructureChanges)
{
    FrameGraph graph;
    std::vector<std::string> log;

    graph.AddPass("StructuralReaderA",
                  [](FrameGraphBuilder& b) { b.StructuralRead(); b.Read<Position>(); },
                  [&]() { log.push_back("StructuralReaderA"); });

    graph.AddPass("StructuralReaderB",
                  [](FrameGraphBuilder& b) { b.StructuralRead(); b.Read<Velocity>(); },
                  [&]() { log.push_back("StructuralReaderB"); });

    graph.AddPass("StructuralWriter",
                  [](FrameGraphBuilder& b) { b.StructuralWrite(); b.Write<Position>(); },
                  [&]() { log.push_back("StructuralWriter"); });

    graph.AddPass("LaterReader",
                  [](FrameGraphBuilder& b) { b.StructuralRead(); },
                  [&]() { log.push_back("LaterReader"); });

    ASSERT_TRUE(graph.Compile().has_value()) << "Compile failed";
    ASSERT_TRUE(graph.Execute().has_value()) << "Execute failed";

    ASSERT_EQ(log.size(), 4u);
    auto indexOf = [](const std::vector<std::string>& v, const std::string& name)
    {
        const auto it = std::find(v.begin(), v.end(), name);
        EXPECT_NE(it, v.end()) << "Missing pass: " << name;
        return (it == v.end()) ? -1 : static_cast<int>(std::distance(v.begin(), it));
    };

    const auto a = indexOf(log, "StructuralReaderA");
    const auto b = indexOf(log, "StructuralReaderB");
    const auto w = indexOf(log, "StructuralWriter");
    const auto r = indexOf(log, "LaterReader");

    ASSERT_GE(w, 0);
    ASSERT_GE(r, 0);
    EXPECT_LT(std::min(a, b), w);
    EXPECT_LT(w, r);
}

TEST(FrameGraphParallel, CommitTokenOrdersCommitAndExtraction)
{
    FrameGraph graph;
    std::vector<std::string> log;

    graph.AddPass("Commit",
                  [](FrameGraphBuilder& b) { b.CommitWorld(); },
                  [&]() { log.push_back("Commit"); });

    graph.AddPass("RenderExtract",
                  [](FrameGraphBuilder& b)
                  {
                      b.ReadResource(SceneCommitToken);
                      b.WriteResource(RenderExtractionToken);
                  },
                  [&]() { log.push_back("RenderExtract"); });

    graph.AddPass("ConsumeExtraction",
                  [](FrameGraphBuilder& b) { b.ReadResource(RenderExtractionToken); },
                  [&]() { log.push_back("ConsumeExtraction"); });

    ASSERT_TRUE(graph.Compile().has_value()) << "Compile failed";
    ASSERT_TRUE(graph.Execute().has_value()) << "Execute failed";

    ASSERT_EQ(log.size(), 3u);
    EXPECT_EQ(log[0], "Commit");
    EXPECT_EQ(log[1], "RenderExtract");
    EXPECT_EQ(log[2], "ConsumeExtraction");
}

TEST(FrameGraphParallel, MainThreadOnlyPassRunsOnCallerThread)
{
    FrameGraph graph;
    Tasks::Scheduler::Initialize(4);

    const auto caller = std::this_thread::get_id();
    std::vector<std::thread::id> observed;
    std::mutex observedMutex;
    std::atomic<bool> mainPassOnCaller = false;

    FrameGraphPassOptions mainOptions;
    mainOptions.MainThreadOnly = true;

    graph.AddPass("MainOnly",
                  mainOptions,
                  [](FrameGraphBuilder&){},
                  [&]()
                  {
                      const auto current = std::this_thread::get_id();
                      std::scoped_lock lock(observedMutex);
                      observed.push_back(current);
                      if (current == caller)
                          mainPassOnCaller.store(true, std::memory_order_release);
                  });

    graph.AddPass("Worker",
                  [](FrameGraphBuilder& b) { b.Read<Position>(); },
                  [&]()
                  {
                      std::scoped_lock lock(observedMutex);
                      observed.push_back(std::this_thread::get_id());
                  });

    ASSERT_TRUE(graph.Compile().has_value()) << "Compile failed";
    ASSERT_TRUE(graph.Execute().has_value()) << "Execute failed";

    ASSERT_EQ(observed.size(), 2u);
    EXPECT_TRUE(mainPassOnCaller.load(std::memory_order_acquire));

    Tasks::Scheduler::Shutdown();
}

TEST(FrameGraphParallel, GraphLocalCompletionDoesNotWaitForUnrelatedSchedulerWork)
{
    FrameGraph graph;
    Tasks::Scheduler::Initialize(4);

    std::atomic<bool> blockerStarted{false};
    std::atomic<bool> blockerFinished{false};

    Tasks::Scheduler::Dispatch([&]()
    {
        blockerStarted.store(true, std::memory_order_release);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        blockerFinished.store(true, std::memory_order_release);
    });

    while (!blockerStarted.load(std::memory_order_acquire))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    std::atomic<int> active{0};
    std::atomic<int> maxActive{0};
    for (int i = 0; i < 4; ++i)
    {
        graph.AddPass("Worker" + std::to_string(i),
                      [](FrameGraphBuilder& b) { b.Write<Position>(); },
                      [&]()
                      {
                          DelayWithActive(active, maxActive, std::chrono::milliseconds(10));
                      });
    }

    std::atomic<bool> graphDone{false};
    std::thread graphThread([&]()
    {
        ASSERT_TRUE(graph.Compile().has_value()) << "Compile failed";
        auto execute = graph.Execute();
        EXPECT_TRUE(execute.has_value()) << "Execute failed";
        graphDone.store(true, std::memory_order_release);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    EXPECT_TRUE(graphDone.load(std::memory_order_acquire));
    EXPECT_FALSE(blockerFinished.load(std::memory_order_acquire));

    graphThread.join();
    EXPECT_GT(maxActive.load(std::memory_order_relaxed), 0);

    const auto waitStart = std::chrono::steady_clock::now();
    while (!blockerFinished.load(std::memory_order_acquire) &&
           (std::chrono::steady_clock::now() - waitStart) < std::chrono::milliseconds(1000))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(blockerFinished.load(std::memory_order_acquire));

    Tasks::Scheduler::Shutdown();
}

TEST(FrameGraphParallel, DeterministicSingleThreadFallbackOrder)
{
    Tasks::Scheduler::Initialize(1);

    constexpr int kRuns = 32;
    std::vector<std::string> baseline;

    for (int run = 0; run < kRuns; ++run)
    {
        FrameGraph graph;
        std::vector<std::string> executionOrder;

        graph.AddPass("WritePosition",
                      [](FrameGraphBuilder& b) { b.Write<Position>(); },
                      [&]() { executionOrder.push_back("WritePosition"); });

        graph.AddPass("ReadPosition",
                      [](FrameGraphBuilder& b) { b.Read<Position>(); },
                      [&]() { executionOrder.push_back("ReadPosition"); });

        graph.AddPass("StructuralCommit",
                      [](FrameGraphBuilder& b) { b.StructuralWrite(); b.CommitWorld(); },
                      [&]() { executionOrder.push_back("StructuralCommit"); });

        graph.AddPass("Extract",
                      [](FrameGraphBuilder& b)
                      {
                          b.ReadResource(SceneCommitToken);
                          b.WriteResource(RenderExtractionToken);
                      },
                      [&]() { executionOrder.push_back("Extract"); });

        ASSERT_TRUE(graph.Compile().has_value()) << "Compile failed on run " << run;
        ASSERT_TRUE(graph.Execute().has_value()) << "Execute failed on run " << run;

        if (run == 0)
        {
            baseline = executionOrder;
        }
        else
        {
            EXPECT_EQ(executionOrder, baseline) << "Non-deterministic fallback order on run " << run;
        }
    }

    Tasks::Scheduler::Shutdown();
}

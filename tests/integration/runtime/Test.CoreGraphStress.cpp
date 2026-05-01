#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

import Extrinsic.Core.Dag.Scheduler;
import Extrinsic.Core.Tasks;
import Extrinsic.Core.Dag.TaskGraph;

using namespace Extrinsic::Core;
using namespace Extrinsic::Core::Dag;

namespace
{
    [[nodiscard]] std::optional<std::uint64_t> ReadCurrentRssKb()
    {
#if defined(__linux__)
        std::ifstream status("/proc/self/status");
        if (!status.is_open())
            return std::nullopt;

        std::string key;
        while (status >> key)
        {
            if (key == "VmRSS:")
            {
                std::uint64_t kb{};
                std::string unit;
                status >> kb >> unit;
                return kb;
            }
        }
#endif

        return std::nullopt;
    }

    void BusyWaitWithCounter(std::atomic<int>& active,
                             std::atomic<int>& maxActive,
                             std::chrono::microseconds delay)
    {
        const int now = active.fetch_add(1, std::memory_order_acq_rel) + 1;
        const int observedMax = maxActive.load(std::memory_order_relaxed);
        if (now > observedMax)
            maxActive.store(now, std::memory_order_release);

        std::this_thread::sleep_for(delay);
        active.fetch_sub(1, std::memory_order_acq_rel);
    }
}

TEST(CoreTaskGraphStress, OneThousandPassMixedHazardGraphCompilesFast)
{
    TaskGraph graph(QueueDomain::Cpu);
    constexpr std::uint32_t kPasses = 1000u;

    for (std::uint32_t i = 0u; i < kPasses; ++i)
    {
        const ResourceId current{static_cast<uint32_t>(i % 17u), 1u};
        const ResourceId prior{static_cast<uint32_t>((i + 1u) % 17u), 1u};
        const ResourceId next{static_cast<uint32_t>((i + 2u) % 17u), 1u};
        const std::string name = std::string("Pass") + std::to_string(i);

        if ((i % 3u) == 0u)
        {
            graph.AddPass(name,
                          [current](TaskGraphBuilder& b) { b.WriteResource(current); },
                          []() {});
        }
        else if ((i % 3u) == 1u)
        {
            graph.AddPass(name,
                          [current, next](TaskGraphBuilder& b)
                          {
                              b.ReadResource(current);
                              b.WriteResource(next);
                          },
                          []() {});
        }
        else
        {
            graph.AddPass(name,
                          [prior, next](TaskGraphBuilder& b)
                          {
                              b.ReadResource(prior);
                              b.ReadResource(next);
                          },
                          []() {});
        }
    }

    const auto t0 = std::chrono::steady_clock::now();
    ASSERT_TRUE(graph.Compile().has_value()) << "Compile failed";
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0);

    const auto plan = graph.BuildPlan();
    ASSERT_TRUE(plan.has_value()) << "BuildPlan failed";

    ASSERT_EQ(plan->size(), kPasses);
    EXPECT_LT(elapsed.count(), 1500);
}

TEST(CoreTaskGraphStress, TenThousandIndependentPassesCompileDeterministically)
{
    TaskGraph graph(QueueDomain::Cpu);
    constexpr std::uint32_t kPasses = 10'000u;

    for (std::uint32_t i = 0u; i < kPasses; ++i)
    {
        const std::string name = std::string("Independent") + std::to_string(i);
        graph.AddPass(name, []() {});
    }

    const auto t0 = std::chrono::steady_clock::now();
    auto first = graph.BuildPlan();
    ASSERT_TRUE(first.has_value()) << "BuildPlan failed";

    const auto firstElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0);
    const auto second = graph.BuildPlan();
    ASSERT_TRUE(second.has_value()) << "Second BuildPlan failed";

    EXPECT_EQ(first->size(), kPasses);
    EXPECT_EQ(second->size(), kPasses);
    EXPECT_LT(firstElapsed.count(), 1500);

    for (std::uint32_t i = 0u; i < kPasses; ++i)
    {
        EXPECT_EQ((*first)[i].id.Index, i);
        EXPECT_EQ((*first)[i].topoOrder, i);
        EXPECT_EQ((*first)[i].batch, 0u);
        EXPECT_EQ((*second)[i].id.Index, i);
        EXPECT_EQ((*second)[i].topoOrder, i);
        EXPECT_EQ((*first)[i].topoOrder, (*second)[i].topoOrder);
        EXPECT_EQ((*first)[i].id, (*second)[i].id);
    }
}

TEST(CoreTaskGraphStress, WideGraphExecutesAcrossWorkerThreads)
{
    Tasks::Scheduler::Initialize(4);
    TaskGraph graph(QueueDomain::Cpu);

    std::atomic<int> active{0};
    std::atomic<int> maxActive{0};
    for (uint32_t i = 0u; i < 128u; ++i)
    {
        graph.AddPass(std::string("WorkerPass") + std::to_string(i),
                      [](TaskGraphBuilder&){},
                      [&active, &maxActive]()
                      {
                          BusyWaitWithCounter(active, maxActive, std::chrono::microseconds{250});
                      });
    }

    ASSERT_TRUE(graph.Compile().has_value()) << "Compile failed";
    ASSERT_TRUE(graph.Execute().has_value()) << "Execute failed";

    EXPECT_GE(maxActive.load(std::memory_order_relaxed), 2);
    EXPECT_EQ(active.load(std::memory_order_relaxed), 0);
    Tasks::Scheduler::Shutdown();
}

TEST(CoreTaskGraphStress, DeepGraphExecutionRespectsExactDependencyChain)
{
    TaskGraph graph(QueueDomain::Cpu);
    constexpr std::uint32_t kPasses = 500u;
    std::vector<std::uint32_t> executionOrder{};
    executionOrder.reserve(kPasses);

    for (std::uint32_t i = 0u; i < kPasses; ++i)
    {
        const ResourceId currentResource{i, 1u};
        const ResourceId previousResource{(i == 0u) ? 0u : (i - 1u), 1u};
        const std::string name = std::string("Chain") + std::to_string(i);

        if (i == 0u)
        {
            graph.AddPass(name,
                          [currentResource](TaskGraphBuilder& b) { b.WriteResource(currentResource); },
                          [i, &executionOrder]()
                          {
                              executionOrder.push_back(i);
                          });
        }
        else
        {
            graph.AddPass(name,
                          [previousResource, currentResource](TaskGraphBuilder& b)
                          {
                              b.ReadResource(previousResource);
                              b.WriteResource(currentResource);
                          },
                          [i, &executionOrder]()
                          {
                              executionOrder.push_back(i);
                          });
        }
    }

    const auto compile = graph.Compile();
    ASSERT_TRUE(compile.has_value()) << "Compile failed";
    const auto plan = graph.BuildPlan();
    ASSERT_TRUE(plan.has_value()) << "BuildPlan failed";

    ASSERT_TRUE(graph.Execute().has_value()) << "Execute failed";

    ASSERT_EQ(executionOrder.size(), kPasses);
    for (std::uint32_t i = 0u; i < kPasses; ++i)
        EXPECT_EQ(executionOrder[i], i);

    for (std::uint32_t i = 0u; i < kPasses; ++i)
    {
        EXPECT_EQ((*plan)[i].id.Index, i);
        EXPECT_GE((*plan)[i].topoOrder, i);
        if (i > 0u)
            EXPECT_GT((*plan)[i].topoOrder, (*plan)[i - 1u].topoOrder);
    }
}

TEST(CoreTaskGraphStress, ReusingGraphAcrossEpochsDoesNotGrowRetainedMemory)
{
    auto baseRss = ReadCurrentRssKb();
    if (!baseRss.has_value())
        GTEST_SKIP() << "VmRSS sampling unavailable on this platform";

    TaskGraph graph(QueueDomain::Cpu);
    constexpr int kEpochs = 1000;
    constexpr uint32_t kPasses = 32u;

    for (int epoch = 0; epoch < kEpochs; ++epoch)
    {
        for (uint32_t i = 0u; i < kPasses; ++i)
        {
            const ResourceId a{static_cast<uint32_t>(i % 4u), 1u};
            const ResourceId b{static_cast<uint32_t>((i + 1u) % 4u), 1u};
            const uint32_t passIndex = static_cast<uint32_t>(epoch * 101 + i);

            if ((passIndex % 2u) == 0u)
            {
                graph.AddPass(std::string("EpochPass") + std::to_string(passIndex),
                              [a](TaskGraphBuilder& builder) { builder.ReadResource(a); },
                              []() {});
            }
            else
            {
                graph.AddPass(std::string("EpochPass") + std::to_string(passIndex),
                              [a, b](TaskGraphBuilder& builder)
                              {
                                  builder.ReadResource(a);
                                  builder.WriteResource(b);
                              },
                              []() {});
            }
        }

        ASSERT_TRUE(graph.Compile().has_value()) << "Compile failed";
        ASSERT_TRUE(graph.Execute().has_value()) << "Execute failed";
        EXPECT_EQ(graph.GetScheduleStats().taskCount, kPasses);
        graph.Reset();
    }

    const auto endRss = ReadCurrentRssKb();
    ASSERT_TRUE(endRss.has_value());
    EXPECT_LE(*endRss, *baseRss + 65'536u);
}

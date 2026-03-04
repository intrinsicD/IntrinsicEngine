#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <coroutine>
#include <cstdint>
#include <thread>
#include <vector>

import Core;

using namespace Core;

namespace
{
    constexpr uint32_t kNodeCount = 2000;
    constexpr int kWarmupFrames = 6;
    constexpr int kMeasuredFrames = 40;

#if defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(undefined_behavior_sanitizer)
    constexpr bool kSanitizerBuild = true;
#else
    constexpr bool kSanitizerBuild = false;
#endif
#elif defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_UNDEFINED__)
    constexpr bool kSanitizerBuild = true;
#else
    constexpr bool kSanitizerBuild = false;
#endif

    Tasks::Job WaitOnCounterAndBump(Tasks::CounterEvent* event,
                                    std::atomic<uint64_t>* resumed) noexcept
    {
        co_await Tasks::WaitFor(*event);
        resumed->fetch_add(1, std::memory_order_relaxed);
        co_return;
    }

    uint64_t Percentile(std::vector<uint64_t> values, double p)
    {
        if (values.empty()) return 0;
        std::sort(values.begin(), values.end());
        const auto idx = static_cast<size_t>(
            std::clamp(p, 0.0, 1.0) * static_cast<double>(values.size() - 1));
        return values[idx];
    }
}

TEST(ArchitectureSLO, FrameGraphP95P99BudgetsAt2000Nodes)
{
    if (kSanitizerBuild)
        GTEST_SKIP() << "Architecture SLO thresholds are calibrated for non-sanitized builds.";

    Memory::ScopeStack scope(1024 * 512);
    FrameGraph graph(scope);

    for (uint32_t i = 0; i < kNodeCount; ++i)
    {
        graph.AddPass("NoOp",
            [](FrameGraphBuilder& b)
            {
                b.Read<uint32_t>();
            },
            []() {});
    }

    Tasks::Scheduler::Initialize(8);

    std::vector<uint64_t> compileNs;
    std::vector<uint64_t> executeNs;
    std::vector<uint64_t> criticalNs;
    compileNs.reserve(kMeasuredFrames);
    executeNs.reserve(kMeasuredFrames);
    criticalNs.reserve(kMeasuredFrames);

    for (int frame = 0; frame < (kWarmupFrames + kMeasuredFrames); ++frame)
    {
        auto compile = graph.Compile();
        ASSERT_TRUE(compile.has_value());

        graph.Execute();

        if (frame >= kWarmupFrames)
        {
            compileNs.push_back(graph.GetLastCompileTimeNs());
            executeNs.push_back(graph.GetLastExecuteTimeNs());
            criticalNs.push_back(graph.GetLastCriticalPathTimeNs());
        }

        graph.Reset();
        scope.Reset();

        for (uint32_t i = 0; i < kNodeCount; ++i)
        {
            graph.AddPass("NoOp",
                [](FrameGraphBuilder& b)
                {
                    b.Read<uint32_t>();
                },
                []() {});
        }
    }

    Tasks::Scheduler::Shutdown();

    const uint64_t compileP99 = Percentile(compileNs, 0.99);
    const uint64_t executeP95 = Percentile(executeNs, 0.95);
    const uint64_t criticalP95 = Percentile(criticalNs, 0.95);

    constexpr uint64_t kCompileP99BudgetNs = 350'000;
    constexpr uint64_t kExecuteP95BudgetNs = kSanitizerBuild ? 2'000'000 : 1'500'000;
    constexpr uint64_t kCriticalP95BudgetNs = kSanitizerBuild ? 1'200'000 : 900'000;

    EXPECT_LT(compileP99, kCompileP99BudgetNs);
    EXPECT_LT(executeP95, kExecuteP95BudgetNs);
    EXPECT_LT(criticalP95, kCriticalP95BudgetNs);
}

TEST(ArchitectureSLO, TaskSchedulerContentionAndWakeLatencyBudgets)
{
    if (kSanitizerBuild)
        GTEST_SKIP() << "Task scheduler SLO thresholds are calibrated for non-sanitized builds.";

    constexpr int kWorkerCount = 8;
    constexpr int kDispatchThreads = 4;
    constexpr int kTasksPerThread = 3000;

    Tasks::Scheduler::Initialize(kWorkerCount);

    std::atomic<uint64_t> executed{0};
    std::vector<std::thread> dispatchers;
    dispatchers.reserve(kDispatchThreads);

    for (int t = 0; t < kDispatchThreads; ++t)
    {
        dispatchers.emplace_back([&executed] {
            for (int i = 0; i < kTasksPerThread; ++i)
            {
                Tasks::Scheduler::Dispatch([&executed] {
                    // Keep this tiny to maintain a saturated queue with frequent steals/contention.
                    executed.fetch_add(1, std::memory_order_relaxed);
                });
            }
        });
    }

    for (auto& dispatcher : dispatchers)
        dispatcher.join();

    Tasks::CounterEvent event{1};
    constexpr int kWaiterCount = 512;
    std::atomic<uint64_t> resumedWaiters{0};
    for (int i = 0; i < kWaiterCount; ++i)
        Tasks::Scheduler::Dispatch(WaitOnCounterAndBump(&event, &resumedWaiters));

    // Block without spin until at least one coroutine has parked.
    Tasks::Scheduler::ParkCountAtomic().wait(0, std::memory_order_acquire);
    event.Signal();

    Tasks::Scheduler::WaitForAll();
    const auto stats = Tasks::Scheduler::GetStats();
    Tasks::Scheduler::Shutdown();

    EXPECT_EQ(executed.load(std::memory_order_relaxed),
              static_cast<uint64_t>(kDispatchThreads * kTasksPerThread));
    EXPECT_EQ(resumedWaiters.load(std::memory_order_relaxed), static_cast<uint64_t>(kWaiterCount));

    // SLO gates from docs/ARCHITECTURE_SLOS.md section 2.
    constexpr double kStealRatioMin = 0.20;
    constexpr double kStealRatioMax = 0.65;
    constexpr uint64_t kQueueContentionP95Ceiling = 4'096;
    constexpr uint64_t kIdleWaitCeilingNs = 700'000;
    constexpr uint64_t kWakeLatencyTailCeilingNs = 80'000;

    EXPECT_GE(stats.StealSuccessRatio, kStealRatioMin);
    EXPECT_LE(stats.StealSuccessRatio, kStealRatioMax);
    EXPECT_LT(stats.QueueContentionCount, kQueueContentionP95Ceiling);
    EXPECT_LT(stats.IdleWaitTotalNs, kIdleWaitCeilingNs);
    EXPECT_LT(stats.UnparkLatencyP99Ns, kWakeLatencyTailCeilingNs);
}

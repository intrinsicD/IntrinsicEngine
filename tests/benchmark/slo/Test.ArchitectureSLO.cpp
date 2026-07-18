#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

import Extrinsic.Core.FrameGraph;
import Extrinsic.Core.Tasks;
import Extrinsic.Core.Tasks.CounterEvent;

using namespace Extrinsic::Core;

namespace
{
    constexpr uint32_t kNodeCount = 2000;
    constexpr int kWarmupFrames = 6;
    constexpr int kMeasuredFrames = 40;
    constexpr int kSchedulerWarmupRounds = 3;
    constexpr int kSchedulerMeasuredRounds = 20;
    constexpr uint32_t kLocalTasksPerRound = 3000;
    constexpr size_t kWaiterCount = 512;

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

    uint64_t NowNs() noexcept
    {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    Tasks::Job WaitOnCounterAndMeasure(
        Tasks::CounterEvent* event,
        std::atomic<uint64_t>* signalTimeNs,
        std::atomic<uint64_t>* resumeLatencyNs,
        size_t sampleIndex,
        std::atomic<uint64_t>* resumed) noexcept
    {
        co_await Tasks::WaitFor(*event);

        const uint64_t signalNs = signalTimeNs->load(std::memory_order_acquire);
        resumeLatencyNs[sampleIndex].store(NowNs() - signalNs, std::memory_order_relaxed);
        resumed->fetch_add(1, std::memory_order_relaxed);
        co_return;
    }

    uint64_t Percentile(std::vector<uint64_t> values, double p)
    {
        if (values.empty())
            return 0;

        std::sort(values.begin(), values.end());
        const auto index = static_cast<size_t>(
            std::clamp(p, 0.0, 1.0) * static_cast<double>(values.size() - 1));
        return values[index];
    }

    void PrintBudgetMetric(const char* name, uint64_t valueNs, uint64_t budgetNs)
    {
        std::cout << "SLO_METRIC name=" << name
                  << " value_ns=" << valueNs
                  << " budget_ns=" << budgetNs << '\n';
    }

    void AddNoOpFrameGraphPasses(FrameGraph& graph)
    {
        for (uint32_t i = 0; i < kNodeCount; ++i)
        {
            graph.AddPass("NoOp",
                [](FrameGraphBuilder& builder)
                {
                    builder.Read<uint32_t>();
                },
                []() {});
        }
    }

    uint64_t RunLocalStealRound()
    {
        std::atomic<uint32_t> completed{0};
        std::atomic<bool> producerReady{false};
        const uint64_t startNs = NowNs();

        Tasks::Scheduler::Dispatch([&]
        {
            for (uint32_t i = 0; i < kLocalTasksPerRound; ++i)
            {
                Tasks::Scheduler::Dispatch([&]
                {
                    const uint32_t finished =
                        completed.fetch_add(1, std::memory_order_acq_rel) + 1;
                    if (finished == kLocalTasksPerRound)
                        completed.notify_all();
                });
            }

            producerReady.store(true, std::memory_order_release);
            producerReady.notify_one();

            uint32_t observed = completed.load(std::memory_order_acquire);
            while (observed != kLocalTasksPerRound)
            {
                completed.wait(observed, std::memory_order_acquire);
                observed = completed.load(std::memory_order_acquire);
            }
        });

        producerReady.wait(false, std::memory_order_acquire);
        Tasks::Scheduler::WaitForAll();
        EXPECT_EQ(completed.load(std::memory_order_acquire), kLocalTasksPerRound);
        return NowNs() - startNs;
    }

    void WaitUntilParked(uint64_t targetParkCount)
    {
        const auto& parkCount = Tasks::Scheduler::ParkCountAtomic();
        uint64_t observed = parkCount.load(std::memory_order_acquire);
        while (observed < targetParkCount)
        {
            parkCount.wait(observed, std::memory_order_acquire);
            observed = parkCount.load(std::memory_order_acquire);
        }
    }
}

TEST(ArchitectureSLO, FrameGraphP95P99BudgetsAt2000Nodes)
{
    if (kSanitizerBuild)
        GTEST_SKIP() << "Architecture SLO thresholds are calibrated for non-sanitized builds.";

    FrameGraph graph;
    AddNoOpFrameGraphPasses(graph);

    Tasks::Scheduler::Initialize(8);

    std::vector<uint64_t> compileNs;
    std::vector<uint64_t> executeNs;
    compileNs.reserve(kMeasuredFrames);
    executeNs.reserve(kMeasuredFrames);

    for (int frame = 0; frame < (kWarmupFrames + kMeasuredFrames); ++frame)
    {
        auto compile = graph.Compile();
        ASSERT_TRUE(compile.has_value());

        auto execute = graph.Execute();
        ASSERT_TRUE(execute.has_value());

        if (frame >= kWarmupFrames)
        {
            compileNs.push_back(graph.LastCompileTimeNs());
            executeNs.push_back(graph.LastExecuteTimeNs());
        }

        graph.Reset();
        AddNoOpFrameGraphPasses(graph);
    }

    Tasks::Scheduler::Shutdown();

    const uint64_t compileP99 = Percentile(compileNs, 0.99);
    const uint64_t executeP95 = Percentile(executeNs, 0.95);

    // Retain the passing historical compile-time ratchet.
    constexpr uint64_t kCompileP99BudgetNs = 350'000;
    // Keep no-op scheduling below half of a 60 Hz frame on the standard runner.
    constexpr uint64_t kExecuteP95BudgetNs = 8'333'334;

    PrintBudgetMetric("frame_graph_compile_p99", compileP99, kCompileP99BudgetNs);
    PrintBudgetMetric("frame_graph_execute_p95", executeP95, kExecuteP95BudgetNs);

    EXPECT_LT(compileP99, kCompileP99BudgetNs);
    EXPECT_LT(executeP95, kExecuteP95BudgetNs);
}

TEST(ArchitectureSLO, TaskSchedulerLocalStealAndWakeCompletionBudgets)
{
    if (kSanitizerBuild)
        GTEST_SKIP() << "Task scheduler SLO thresholds are calibrated for non-sanitized builds.";

    constexpr int kWorkerCount = 8;
    Tasks::Scheduler::Initialize(kWorkerCount);

    const auto statsBefore = Tasks::Scheduler::GetStats();
    std::vector<uint64_t> localFanoutNs;
    localFanoutNs.reserve(kSchedulerMeasuredRounds);
    for (int round = 0;
         round < (kSchedulerWarmupRounds + kSchedulerMeasuredRounds);
         ++round)
    {
        const uint64_t elapsedNs = RunLocalStealRound();
        if (round >= kSchedulerWarmupRounds)
            localFanoutNs.push_back(elapsedNs);
    }

    std::vector<uint64_t> resumeLatencyNs;
    resumeLatencyNs.reserve(kSchedulerMeasuredRounds * kWaiterCount);
    std::atomic<uint64_t> resumedWaiters{0};
    for (int round = 0;
         round < (kSchedulerWarmupRounds + kSchedulerMeasuredRounds);
         ++round)
    {
        Tasks::CounterEvent event{1};
        std::atomic<uint64_t> signalTimeNs{0};
        std::array<std::atomic<uint64_t>, kWaiterCount> roundLatencyNs{};
        const uint64_t parkedBefore = Tasks::Scheduler::GetParkCount();

        for (size_t waiter = 0; waiter < kWaiterCount; ++waiter)
        {
            Tasks::Scheduler::Dispatch(WaitOnCounterAndMeasure(
                &event,
                &signalTimeNs,
                roundLatencyNs.data(),
                waiter,
                &resumedWaiters));
        }

        WaitUntilParked(parkedBefore + kWaiterCount);
        signalTimeNs.store(NowNs(), std::memory_order_release);
        event.Signal();
        Tasks::Scheduler::WaitForAll();

        if (round >= kSchedulerWarmupRounds)
        {
            for (const auto& latencyNs : roundLatencyNs)
                resumeLatencyNs.push_back(latencyNs.load(std::memory_order_relaxed));
        }
    }

    const auto stats = Tasks::Scheduler::GetStats();
    Tasks::Scheduler::Shutdown();

    const uint64_t localFanoutP95 = Percentile(localFanoutNs, 0.95);
    const uint64_t resumeLatencyP99 = Percentile(resumeLatencyNs, 0.99);
    constexpr uint64_t kOne60HzFrameNs = 16'666'667;
    constexpr uint64_t kLocalFanoutP95BudgetNs = kOne60HzFrameNs;
    constexpr uint64_t kResumeLatencyP99BudgetNs = kOne60HzFrameNs;
    const uint64_t expectedSteals =
        static_cast<uint64_t>(kSchedulerWarmupRounds + kSchedulerMeasuredRounds) *
        kLocalTasksPerRound;
    const uint64_t expectedResumes =
        static_cast<uint64_t>(kSchedulerWarmupRounds + kSchedulerMeasuredRounds) *
        kWaiterCount;

    PrintBudgetMetric(
        "scheduler_local_fanout_p95", localFanoutP95, kLocalFanoutP95BudgetNs);
    PrintBudgetMetric(
        "scheduler_signal_to_resume_p99", resumeLatencyP99, kResumeLatencyP99BudgetNs);
    std::cout << "SLO_DIAGNOSTIC"
              << " steal_pop_delta=" << (stats.StealPopCount - statsBefore.StealPopCount)
              << " steal_attempt_delta="
              << (stats.TotalStealAttempts - statsBefore.TotalStealAttempts)
              << " steal_success_ratio=" << stats.StealSuccessRatio
              << " queue_contention_count=" << stats.QueueContentionCount
              << " idle_wait_count=" << stats.IdleWaitCount
              << " idle_wait_total_ns=" << stats.IdleWaitTotalNs
              << " park_to_signal_p99_ns=" << stats.UnparkLatencyP99Ns << '\n';

    EXPECT_EQ(stats.StealPopCount - statsBefore.StealPopCount, expectedSteals);
    EXPECT_EQ(resumedWaiters.load(std::memory_order_relaxed), expectedResumes);
    EXPECT_EQ(resumeLatencyNs.size(), kSchedulerMeasuredRounds * kWaiterCount);
    EXPECT_LT(localFanoutP95, kLocalFanoutP95BudgetNs);
    EXPECT_LT(resumeLatencyP99, kResumeLatencyP99BudgetNs);
}

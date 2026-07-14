// CORE-007 — PR-fast scheduler throughput, steal-latency, and priority probe.

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>

import Extrinsic.Core.Dag.TaskGraph;
import Extrinsic.Core.Dag.Scheduler;
import Extrinsic.Core.Tasks;

namespace
{
    namespace Dag = Extrinsic::Core::Dag;
    namespace Tasks = Extrinsic::Core::Tasks;

    constexpr std::string_view kBenchmarkId = "core.scheduler_hardening.smoke";
    constexpr std::string_view kMethod = "core.scheduler_hardening";
    constexpr std::string_view kDataset = "builtin.synthetic_scheduler_contention.v1";
    constexpr std::uint32_t kWarmupIterations = 3u;
    constexpr std::uint32_t kMeasuredIterations = 9u;
    constexpr std::uint32_t kDispatchTasks = 8'192u;
    constexpr std::uint32_t kStealTasks = 256u;
    constexpr std::uint32_t kLowPriorityTasks = 64u;
    constexpr std::uint32_t kHighPriorityTasks = 16u;
    constexpr std::uint32_t kPriorityTasks = kLowPriorityTasks + kHighPriorityTasks;

    class SchedulerScope final
    {
    public:
        explicit SchedulerScope(const unsigned workerCount)
        {
            Tasks::Scheduler::Initialize(workerCount);
        }

        ~SchedulerScope()
        {
            Tasks::Scheduler::WaitForAll();
            Tasks::Scheduler::Shutdown();
        }

        SchedulerScope(const SchedulerScope&) = delete;
        SchedulerScope& operator=(const SchedulerScope&) = delete;
    };

    struct DispatchMetrics
    {
        double MedianMilliseconds{0.0};
        double ThroughputItemsPerSecond{0.0};
        std::uint32_t WorkerCount{0u};
        bool Succeeded{false};
    };

    struct StealMetrics
    {
        double MedianNanoseconds{0.0};
        double P95Nanoseconds{0.0};
        std::uint64_t QueueContentionCount{0u};
        std::uint64_t SuccessfulSteals{0u};
        bool Succeeded{false};
    };

    struct PriorityMetrics
    {
        double RuntimeMilliseconds{0.0};
        std::uint32_t LowBeforeFirstHigh{0u};
        std::uint32_t LowInHighWindow{0u};
        bool Succeeded{false};
    };

    template <typename T>
    void Publish(std::atomic<T>& value, const T published)
    {
        value.store(published, std::memory_order_release);
        value.notify_all();
    }

    template <typename T>
    void WaitUntilAtLeast(std::atomic<T>& value, const T target)
    {
        auto observed = value.load(std::memory_order_acquire);
        while (observed < target)
        {
            value.wait(observed, std::memory_order_acquire);
            observed = value.load(std::memory_order_acquire);
        }
    }

    template <std::size_t N>
    [[nodiscard]] double Percentile(std::array<double, N> samples, const double percentile)
    {
        std::sort(samples.begin(), samples.end());
        const auto rank = static_cast<std::size_t>(
            percentile * static_cast<double>(samples.size() - 1u));
        return samples[rank];
    }

    [[nodiscard]] DispatchMetrics MeasureDispatchThroughput()
    {
        SchedulerScope scheduler{4u};

        const auto run = []() -> double
        {
            std::atomic<std::uint32_t> completed{0u};
            const auto startedAt = std::chrono::steady_clock::now();
            for (std::uint32_t task = 0u; task < kDispatchTasks; ++task)
            {
                Tasks::Scheduler::Dispatch([&completed]()
                {
                    completed.fetch_add(1u, std::memory_order_release);
                });
            }
            Tasks::Scheduler::WaitForAll();
            const auto finishedAt = std::chrono::steady_clock::now();
            if (completed.load(std::memory_order_acquire) != kDispatchTasks)
                return 0.0;

            const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                finishedAt - startedAt).count();
            return static_cast<double>(elapsed) * 1.0e-6;
        };

        for (std::uint32_t warmup = 0u; warmup < kWarmupIterations; ++warmup)
            (void)run();

        std::array<double, kMeasuredIterations> samples{};
        for (double& sample : samples)
            sample = run();

        DispatchMetrics metrics{};
        metrics.MedianMilliseconds = Percentile(samples, 0.5);
        metrics.ThroughputItemsPerSecond = metrics.MedianMilliseconds > 0.0
            ? static_cast<double>(kDispatchTasks) / (metrics.MedianMilliseconds * 1.0e-3)
            : 0.0;
        metrics.WorkerCount = Tasks::Scheduler::WorkerCount();
        metrics.Succeeded = metrics.MedianMilliseconds > 0.0 &&
            metrics.ThroughputItemsPerSecond > 0.0;
        return metrics;
    }

    [[nodiscard]] StealMetrics MeasureExternalStealLatency()
    {
        SchedulerScope scheduler{1u};
        std::atomic<bool> childrenQueued{false};
        std::atomic<bool> releaseParent{false};
        std::atomic<std::uint32_t> childrenCompleted{0u};

        Tasks::Scheduler::Dispatch([&]()
        {
            for (std::uint32_t task = 0u; task < kStealTasks; ++task)
            {
                Tasks::Scheduler::Dispatch([&childrenCompleted]()
                {
                    childrenCompleted.fetch_add(1u, std::memory_order_release);
                });
            }
            Publish(childrenQueued, true);
            while (!releaseParent.load(std::memory_order_acquire))
                releaseParent.wait(false, std::memory_order_acquire);
        });

        while (!childrenQueued.load(std::memory_order_acquire))
            childrenQueued.wait(false, std::memory_order_acquire);

        const auto statsBefore = Tasks::Scheduler::GetStats();
        std::array<double, kStealTasks> samples{};
        bool stoleEveryTask = true;
        for (double& sample : samples)
        {
            const auto startedAt = std::chrono::steady_clock::now();
            const bool ran = Tasks::Scheduler::TryRunOne();
            const auto finishedAt = std::chrono::steady_clock::now();
            stoleEveryTask = stoleEveryTask && ran;
            sample = static_cast<double>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    finishedAt - startedAt).count());
        }

        Publish(releaseParent, true);
        Tasks::Scheduler::WaitForAll();
        const auto statsAfter = Tasks::Scheduler::GetStats();

        StealMetrics metrics{};
        metrics.MedianNanoseconds = Percentile(samples, 0.5);
        metrics.P95Nanoseconds = Percentile(samples, 0.95);
        metrics.QueueContentionCount =
            statsAfter.QueueContentionCount - statsBefore.QueueContentionCount;
        metrics.SuccessfulSteals =
            statsAfter.SuccessfulStealAttempts - statsBefore.SuccessfulStealAttempts;
        metrics.Succeeded = stoleEveryTask &&
            childrenCompleted.load(std::memory_order_acquire) == kStealTasks &&
            metrics.SuccessfulSteals == kStealTasks;
        return metrics;
    }

    [[nodiscard]] PriorityMetrics MeasurePriorityInversion()
    {
        SchedulerScope scheduler{1u};
        std::atomic<bool> blockerStarted{false};
        std::atomic<bool> releaseBlocker{false};
        std::atomic<std::uint32_t> completed{0u};
        std::atomic<std::uint32_t> nextOrder{0u};
        std::array<std::uint8_t, kPriorityTasks> executionOrder{};

        Tasks::Scheduler::Dispatch([&]()
        {
            Publish(blockerStarted, true);
            while (!releaseBlocker.load(std::memory_order_acquire))
                releaseBlocker.wait(false, std::memory_order_acquire);
        });
        while (!blockerStarted.load(std::memory_order_acquire))
            blockerStarted.wait(false, std::memory_order_acquire);

        Dag::TaskGraph graph;
        const auto addPasses = [&](const std::uint32_t count,
                                   const Dag::TaskPriority priority,
                                   const std::uint8_t classId,
                                   std::string_view prefix)
        {
            Dag::TaskGraphPassOptions options{};
            options.Priority = priority;
            for (std::uint32_t pass = 0u; pass < count; ++pass)
            {
                graph.AddPass(std::string(prefix) + std::to_string(pass), options,
                    [](Dag::TaskGraphBuilder&) {},
                    [&, classId]()
                    {
                        const auto order = nextOrder.fetch_add(1u, std::memory_order_acq_rel);
                        executionOrder[order] = classId;
                        const auto finished = completed.fetch_add(1u, std::memory_order_acq_rel) + 1u;
                        if (finished == kPriorityTasks)
                            completed.notify_all();
                    });
            }
        };

        addPasses(kLowPriorityTasks, Dag::TaskPriority::Background, 0u, "Low");
        addPasses(kHighPriorityTasks, Dag::TaskPriority::Critical, 1u, "High");

        const auto compiled = graph.Compile();
        auto submitted = graph.Submit();
        if (!compiled.has_value() || !submitted.has_value())
        {
            Publish(releaseBlocker, true);
            return {};
        }

        const auto startedAt = std::chrono::steady_clock::now();
        Publish(releaseBlocker, true);
        WaitUntilAtLeast(completed, kPriorityTasks);
        const auto finishedAt = std::chrono::steady_clock::now();
        const auto waited = submitted->Wait();

        PriorityMetrics metrics{};
        metrics.RuntimeMilliseconds = static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                finishedAt - startedAt).count()) * 1.0e-6;

        while (metrics.LowBeforeFirstHigh < kPriorityTasks &&
               executionOrder[metrics.LowBeforeFirstHigh] == 0u)
        {
            ++metrics.LowBeforeFirstHigh;
        }
        for (std::uint32_t order = 0u; order < kHighPriorityTasks; ++order)
        {
            if (executionOrder[order] == 0u)
                ++metrics.LowInHighWindow;
        }

        metrics.Succeeded = waited.has_value() &&
            completed.load(std::memory_order_acquire) == kPriorityTasks &&
            metrics.LowInHighWindow == 0u;
        return metrics;
    }

    [[nodiscard]] std::string ResolveCommit()
    {
        if (const char* commit = std::getenv("GIT_COMMIT"); commit && commit[0] != '\0')
            return commit;
        return "local-dev";
    }

    [[nodiscard]] bool WriteResult(const std::filesystem::path& path,
                                   const DispatchMetrics& dispatch,
                                   const StealMetrics& steal,
                                   const PriorityMetrics& priority)
    {
        if (!path.parent_path().empty())
            std::filesystem::create_directories(path.parent_path());

        std::ofstream output(path, std::ios::trunc);
        if (!output)
            return false;

        const bool passed = dispatch.Succeeded && steal.Succeeded && priority.Succeeded;
        output << std::fixed << std::setprecision(3)
               << "{\n"
               << "  \"benchmark_id\": \"" << kBenchmarkId << "\",\n"
               << "  \"method\": \"" << kMethod << "\",\n"
               << "  \"backend\": \"cpu_optimized\",\n"
               << "  \"dataset\": \"" << kDataset << "\",\n"
               << "  \"commit\": \"" << ResolveCommit() << "\",\n"
               << "  \"metrics\": {\n"
               << "    \"runtime_ms\": " << dispatch.MedianMilliseconds << ",\n"
               << "    \"throughput_items_per_sec\": "
               << dispatch.ThroughputItemsPerSecond << ",\n"
               << "    \"quality_error_l2\": " << priority.LowInHighWindow << "\n"
               << "  },\n"
               << "  \"diagnostics\": {\n"
               << "    \"runner\": \"IntrinsicSchedulerBenchmarkSmoke\",\n"
               << "    \"intent\": \"smoke\",\n"
               << "    \"warmup_iterations\": " << kWarmupIterations << ",\n"
               << "    \"measured_iterations\": " << kMeasuredIterations << ",\n"
               << "    \"dispatch_task_count\": " << kDispatchTasks << ",\n"
               << "    \"worker_count\": " << dispatch.WorkerCount << ",\n"
               << "    \"steal_task_count\": " << kStealTasks << ",\n"
               << "    \"steal_latency_median_ns\": " << steal.MedianNanoseconds << ",\n"
               << "    \"steal_latency_p95_ns\": " << steal.P95Nanoseconds << ",\n"
               << "    \"steal_success_count\": " << steal.SuccessfulSteals << ",\n"
               << "    \"steal_queue_contention_count\": "
               << steal.QueueContentionCount << ",\n"
               << "    \"priority_low_task_count\": " << kLowPriorityTasks << ",\n"
               << "    \"priority_high_task_count\": " << kHighPriorityTasks << ",\n"
               << "    \"low_before_first_high\": " << priority.LowBeforeFirstHigh << ",\n"
               << "    \"low_in_high_window\": " << priority.LowInHighWindow << ",\n"
               << "    \"priority_probe_runtime_ms\": "
               << priority.RuntimeMilliseconds << ",\n"
               << "    \"priority_contract_satisfied\": "
               << (priority.Succeeded ? "true" : "false") << ",\n"
               << "    \"chase_lev_probe\": \"spinlock_queue_contention_and_steal_latency\",\n"
               << "    \"chase_lev_adopted\": false\n"
               << "  },\n"
               << "  \"status\": \"" << (passed ? "passed" : "failed") << "\"\n"
               << "}\n";
        return output.good() && passed;
    }
}

int main(const int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "usage: IntrinsicSchedulerBenchmarkSmoke <result.json>\n";
        return 2;
    }

    const DispatchMetrics dispatch = MeasureDispatchThroughput();
    const StealMetrics steal = MeasureExternalStealLatency();
    const PriorityMetrics priority = MeasurePriorityInversion();
    if (!WriteResult(argv[1], dispatch, steal, priority))
    {
        std::cerr << "scheduler benchmark failed; result written to " << argv[1] << "\n";
        return 1;
    }

    std::cout << "scheduler benchmark passed; result written to " << argv[1] << "\n";
    return 0;
}

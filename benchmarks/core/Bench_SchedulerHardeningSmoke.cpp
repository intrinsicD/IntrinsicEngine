// CORE-007 - scheduler priority and wait-registry smoke benchmark.

#include "Bench.SchedulerHardeningSmoke.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

import Extrinsic.Core.Dag.TaskGraph;
import Extrinsic.Core.Dag.Scheduler;
import Extrinsic.Core.Tasks;

namespace Intrinsic::Bench::Core
{
    namespace
    {
        namespace Dag = Extrinsic::Core::Dag;
        namespace Tasks = Extrinsic::Core::Tasks;

        constexpr std::uint32_t kWarmupIterations = 3u;
        constexpr std::uint32_t kMeasuredIterations = 9u;
        constexpr std::uint32_t kDispatchWorkerRequest = 4u;
        constexpr std::uint32_t kDispatchTaskCount = 8'192u;
        constexpr std::uint32_t kPriorityLowTaskCount = 64u;
        constexpr std::uint32_t kPriorityHighTaskCount = 16u;
        constexpr std::uint32_t kPriorityTaskCount =
            kPriorityLowTaskCount + kPriorityHighTaskCount;
        constexpr std::uint32_t kWaitRegistryThreadCount = 8u;
        constexpr std::uint32_t kWaitRegistryOperationsPerThread = 4'096u;

        class SchedulerScope
        {
        public:
            explicit SchedulerScope(const unsigned threadCount)
                : m_Owns(!Tasks::Scheduler::IsInitialized())
            {
                if (m_Owns)
                {
                    Tasks::Scheduler::Initialize(threadCount);
                }
            }

            ~SchedulerScope()
            {
                if (m_Owns)
                {
                    Tasks::Scheduler::WaitForAll();
                    Tasks::Scheduler::Shutdown();
                }
            }

            SchedulerScope(const SchedulerScope&) = delete;
            SchedulerScope& operator=(const SchedulerScope&) = delete;

        private:
            bool m_Owns = false;
        };

        struct DispatchProbeMetrics
        {
            double MedianMilliseconds{0.0};
            double ThroughputItemsPerSecond{0.0};
            std::uint32_t WorkerCount{0u};
            bool Succeeded{false};
        };

        struct PriorityProbeMetrics
        {
            double RuntimeMilliseconds{0.0};
            std::uint32_t LowBeforeFirstHigh{0u};
            std::uint32_t LowInHighWindow{0u};
            bool Succeeded{false};
        };

        struct WaitRegistryProbeMetrics
        {
            double SingleThreadMedianMilliseconds{0.0};
            double ContendedMedianMilliseconds{0.0};
            double SingleThreadThroughputItemsPerSecond{0.0};
            double ContendedThroughputItemsPerSecond{0.0};
            double ContendedScalingEfficiency{0.0};
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
        [[nodiscard]] double Median(std::array<double, N> samples)
        {
            std::sort(samples.begin(), samples.end());
            return samples[samples.size() / 2u];
        }

        [[nodiscard]] DispatchProbeMetrics MeasureDispatchThroughput()
        {
            SchedulerScope scheduler{kDispatchWorkerRequest};
            const auto workerCount =
                static_cast<std::uint32_t>(Tasks::Scheduler::GetStats().WorkerLocalDepths.size());

            const auto run = []() -> double
            {
                std::atomic<std::uint32_t> completed{0u};
                const auto startedAt = std::chrono::steady_clock::now();
                for (std::uint32_t task = 0u; task < kDispatchTaskCount; ++task)
                {
                    Tasks::Scheduler::Dispatch(
                        [&completed]()
                        {
                            completed.fetch_add(1u, std::memory_order_release);
                        });
                }
                Tasks::Scheduler::WaitForAll();
                const auto finishedAt = std::chrono::steady_clock::now();
                if (completed.load(std::memory_order_acquire) != kDispatchTaskCount)
                {
                    return 0.0;
                }

                const auto elapsedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    finishedAt - startedAt).count();
                return static_cast<double>(elapsedNs) * 1.0e-6;
            };

            for (std::uint32_t warmup = 0u; warmup < kWarmupIterations; ++warmup)
            {
                (void)run();
            }

            std::array<double, kMeasuredIterations> samples{};
            for (double& sample : samples)
            {
                sample = run();
            }

            DispatchProbeMetrics metrics{};
            metrics.MedianMilliseconds = Median(samples);
            metrics.ThroughputItemsPerSecond =
                metrics.MedianMilliseconds > 0.0
                    ? static_cast<double>(kDispatchTaskCount) /
                          (metrics.MedianMilliseconds * 1.0e-3)
                    : 0.0;
            metrics.WorkerCount = workerCount;
            metrics.Succeeded = metrics.MedianMilliseconds > 0.0 &&
                                metrics.ThroughputItemsPerSecond > 0.0 &&
                                metrics.WorkerCount > 0u;
            return metrics;
        }

        [[nodiscard]] PriorityProbeMetrics MeasurePriorityInversion()
        {
            SchedulerScope scheduler{1u};
            std::atomic<bool> blockerStarted{false};
            std::atomic<bool> releaseBlocker{false};
            std::atomic<std::uint32_t> completed{0u};
            std::atomic<std::uint32_t> nextOrder{0u};
            std::array<std::uint8_t, kPriorityTaskCount> executionOrder{};

            Tasks::Scheduler::Dispatch(
                [&]()
                {
                    Publish(blockerStarted, true);
                    while (!releaseBlocker.load(std::memory_order_acquire))
                    {
                        releaseBlocker.wait(false, std::memory_order_acquire);
                    }
                });
            while (!blockerStarted.load(std::memory_order_acquire))
            {
                blockerStarted.wait(false, std::memory_order_acquire);
            }

            Dag::TaskGraph graph;
            const auto addPasses =
                [&](const std::uint32_t count,
                    const Dag::TaskPriority priority,
                    const std::uint8_t classId,
                    const std::string_view prefix)
            {
                Dag::TaskGraphPassOptions options{};
                options.Priority = priority;
                for (std::uint32_t pass = 0u; pass < count; ++pass)
                {
                    graph.AddPass(
                        std::string(prefix) + std::to_string(pass),
                        options,
                        [](Dag::TaskGraphBuilder&)
                        {
                        },
                        [&, classId]()
                        {
                            const auto order =
                                nextOrder.fetch_add(1u, std::memory_order_acq_rel);
                            executionOrder[order] = classId;
                            const auto finished =
                                completed.fetch_add(1u, std::memory_order_acq_rel) + 1u;
                            if (finished == kPriorityTaskCount)
                            {
                                completed.notify_all();
                            }
                        });
                }
            };

            addPasses(
                kPriorityLowTaskCount, Dag::TaskPriority::Background, 0u, "Low");
            addPasses(
                kPriorityHighTaskCount, Dag::TaskPriority::Critical, 1u, "High");

            const auto compiled = graph.Compile();
            auto submitted = graph.Submit();
            if (!compiled.has_value() || !submitted.has_value())
            {
                Publish(releaseBlocker, true);
                return {};
            }

            const auto startedAt = std::chrono::steady_clock::now();
            Publish(releaseBlocker, true);
            WaitUntilAtLeast(completed, kPriorityTaskCount);
            const auto finishedAt = std::chrono::steady_clock::now();
            const auto waited = submitted->Wait();

            PriorityProbeMetrics metrics{};
            metrics.RuntimeMilliseconds = static_cast<double>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    finishedAt - startedAt).count()) *
                1.0e-6;
            while (metrics.LowBeforeFirstHigh < kPriorityTaskCount &&
                   executionOrder[metrics.LowBeforeFirstHigh] == 0u)
            {
                ++metrics.LowBeforeFirstHigh;
            }
            for (std::uint32_t order = 0u; order < kPriorityHighTaskCount; ++order)
            {
                if (executionOrder[order] == 0u)
                {
                    ++metrics.LowInHighWindow;
                }
            }

            metrics.Succeeded = waited.has_value() &&
                                completed.load(std::memory_order_acquire) ==
                                    kPriorityTaskCount &&
                                metrics.LowInHighWindow == 0u;
            return metrics;
        }

        [[nodiscard]] WaitRegistryProbeMetrics MeasureWaitRegistryContention()
        {
            SchedulerScope scheduler{1u};
            bool warmupSucceeded = true;

            std::vector<Tasks::Scheduler::WaitToken> capacityWarmup;
            capacityWarmup.reserve(kWaitRegistryThreadCount);
            for (std::uint32_t i = 0u; i < kWaitRegistryThreadCount; ++i)
            {
                auto token = Tasks::Scheduler::AcquireWaitToken();
                warmupSucceeded = warmupSucceeded && token.Valid();
                capacityWarmup.push_back(token);
            }
            for (const auto token : capacityWarmup)
            {
                Tasks::Scheduler::ReleaseWaitToken(token);
            }

            const auto runSingleThread = [&]() -> double
            {
                const auto startedAt = std::chrono::steady_clock::now();
                for (std::uint32_t operation = 0u;
                     operation < kWaitRegistryOperationsPerThread;
                     ++operation)
                {
                    const auto token = Tasks::Scheduler::AcquireWaitToken();
                    warmupSucceeded = warmupSucceeded && token.Valid();
                    Tasks::Scheduler::ReleaseWaitToken(token);
                }
                const auto finishedAt = std::chrono::steady_clock::now();
                return static_cast<double>(
                           std::chrono::duration_cast<std::chrono::nanoseconds>(
                               finishedAt - startedAt)
                               .count()) *
                       1.0e-6;
            };

            for (std::uint32_t warmup = 0u; warmup < kWarmupIterations; ++warmup)
            {
                (void)runSingleThread();
            }

            std::array<double, kMeasuredIterations> singleThreadSamples{};
            for (double& sample : singleThreadSamples)
            {
                sample = runSingleThread();
            }

            constexpr std::uint32_t roundCount =
                kWarmupIterations + kMeasuredIterations;
            std::barrier startRound{kWaitRegistryThreadCount + 1u};
            std::barrier finishRound{kWaitRegistryThreadCount + 1u};
            std::atomic<bool> contendedSucceeded{true};
            std::vector<std::thread> workers;
            workers.reserve(kWaitRegistryThreadCount);
            for (std::uint32_t threadIndex = 0u;
                 threadIndex < kWaitRegistryThreadCount;
                 ++threadIndex)
            {
                workers.emplace_back(
                    [&]()
                    {
                        for (std::uint32_t round = 0u; round < roundCount; ++round)
                        {
                            startRound.arrive_and_wait();
                            for (std::uint32_t operation = 0u;
                                 operation < kWaitRegistryOperationsPerThread;
                                 ++operation)
                            {
                                const auto token =
                                    Tasks::Scheduler::AcquireWaitToken();
                                if (!token.Valid())
                                {
                                    contendedSucceeded.store(
                                        false, std::memory_order_release);
                                }
                                Tasks::Scheduler::ReleaseWaitToken(token);
                            }
                            finishRound.arrive_and_wait();
                        }
                    });
            }

            std::array<double, kMeasuredIterations> contendedSamples{};
            for (std::uint32_t round = 0u; round < roundCount; ++round)
            {
                const auto startedAt = std::chrono::steady_clock::now();
                startRound.arrive_and_wait();
                finishRound.arrive_and_wait();
                const auto finishedAt = std::chrono::steady_clock::now();
                if (round >= kWarmupIterations)
                {
                    contendedSamples[round - kWarmupIterations] =
                        static_cast<double>(
                            std::chrono::duration_cast<std::chrono::nanoseconds>(
                                finishedAt - startedAt)
                                .count()) *
                        1.0e-6;
                }
            }

            for (auto& worker : workers)
            {
                worker.join();
            }

            WaitRegistryProbeMetrics metrics{};
            metrics.SingleThreadMedianMilliseconds = Median(singleThreadSamples);
            metrics.ContendedMedianMilliseconds = Median(contendedSamples);
            metrics.SingleThreadThroughputItemsPerSecond =
                metrics.SingleThreadMedianMilliseconds > 0.0
                    ? static_cast<double>(kWaitRegistryOperationsPerThread) /
                          (metrics.SingleThreadMedianMilliseconds * 1.0e-3)
                    : 0.0;
            metrics.ContendedThroughputItemsPerSecond =
                metrics.ContendedMedianMilliseconds > 0.0
                    ? static_cast<double>(
                          kWaitRegistryThreadCount *
                          kWaitRegistryOperationsPerThread) /
                          (metrics.ContendedMedianMilliseconds * 1.0e-3)
                    : 0.0;
            metrics.ContendedScalingEfficiency =
                metrics.SingleThreadThroughputItemsPerSecond > 0.0
                    ? metrics.ContendedThroughputItemsPerSecond /
                          (metrics.SingleThreadThroughputItemsPerSecond *
                           static_cast<double>(kWaitRegistryThreadCount))
                    : 0.0;
            metrics.Succeeded =
                warmupSucceeded &&
                contendedSucceeded.load(std::memory_order_acquire) &&
                metrics.SingleThreadMedianMilliseconds > 0.0 &&
                metrics.ContendedMedianMilliseconds > 0.0 &&
                metrics.SingleThreadThroughputItemsPerSecond > 0.0 &&
                metrics.ContendedThroughputItemsPerSecond > 0.0;
            return metrics;
        }
    } // namespace

    SchedulerHardeningSmokeMetrics RunSchedulerHardeningSmoke()
    {
        const DispatchProbeMetrics dispatch = MeasureDispatchThroughput();
        const PriorityProbeMetrics priority = MeasurePriorityInversion();
        const WaitRegistryProbeMetrics waitRegistry =
            MeasureWaitRegistryContention();

        SchedulerHardeningSmokeMetrics metrics{};
        metrics.RuntimeMilliseconds = dispatch.MedianMilliseconds;
        metrics.ThroughputItemsPerSecond =
            dispatch.ThroughputItemsPerSecond;
        metrics.QualityErrorL2 =
            static_cast<double>(priority.LowInHighWindow);
        metrics.PriorityProbeRuntimeMilliseconds =
            priority.RuntimeMilliseconds;
        metrics.WaitRegistrySingleThreadMedianMilliseconds =
            waitRegistry.SingleThreadMedianMilliseconds;
        metrics.WaitRegistryContendedMedianMilliseconds =
            waitRegistry.ContendedMedianMilliseconds;
        metrics.WaitRegistrySingleThreadThroughputItemsPerSecond =
            waitRegistry.SingleThreadThroughputItemsPerSecond;
        metrics.WaitRegistryContendedThroughputItemsPerSecond =
            waitRegistry.ContendedThroughputItemsPerSecond;
        metrics.WaitRegistryContendedScalingEfficiency =
            waitRegistry.ContendedScalingEfficiency;
        metrics.WarmupIterations = kWarmupIterations;
        metrics.MeasuredIterations = kMeasuredIterations;
        metrics.DispatchWorkerRequest = kDispatchWorkerRequest;
        metrics.DispatchWorkerCount = dispatch.WorkerCount;
        metrics.DispatchTaskCount = kDispatchTaskCount;
        metrics.PriorityLowTaskCount = kPriorityLowTaskCount;
        metrics.PriorityHighTaskCount = kPriorityHighTaskCount;
        metrics.PriorityLowBeforeFirstHigh =
            priority.LowBeforeFirstHigh;
        metrics.PriorityLowInHighWindow = priority.LowInHighWindow;
        metrics.WaitRegistryThreadCount = kWaitRegistryThreadCount;
        metrics.WaitRegistryOperationsPerThread =
            kWaitRegistryOperationsPerThread;
        metrics.DispatchSucceeded = dispatch.Succeeded;
        metrics.PriorityContractSatisfied = priority.Succeeded;
        metrics.WaitRegistrySucceeded = waitRegistry.Succeeded;
        metrics.Succeeded = metrics.DispatchSucceeded &&
                            metrics.PriorityContractSatisfied &&
                            metrics.WaitRegistrySucceeded;
        return metrics;
    }
} // namespace Intrinsic::Bench::Core

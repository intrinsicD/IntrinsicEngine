// GRAPHICS-119 - render-graph parallel recording smoke benchmark.

#include "Bench.RenderGraphParallelRecordingSmoke.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

import Extrinsic.Core.Error;
import Extrinsic.Core.Tasks;
import Extrinsic.Graphics.RenderGraph;

namespace Intrinsic::Bench::Rendering
{
    namespace
    {
        namespace Core = Extrinsic::Core;
        namespace Graphics = Extrinsic::Graphics;
        namespace Tasks = Extrinsic::Core::Tasks;

        constexpr std::uint32_t kWarmupIterations = 4u;
        constexpr std::uint32_t kMeasuredIterations = 64u;
        constexpr std::uint32_t kPassCount = 256u;
        constexpr std::uint32_t kSchedulerWorkerCount = 2u;
        constexpr std::uint32_t kRecordOpsPerPass = 256u;

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
                    Tasks::Scheduler::Shutdown();
                }
            }

            SchedulerScope(const SchedulerScope&) = delete;
            SchedulerScope& operator=(const SchedulerScope&) = delete;

        private:
            bool m_Owns = false;
        };

        [[nodiscard]] Graphics::CompiledRenderGraph CompileIndependentPassGraph()
        {
            Graphics::RenderGraph graph{};
            for (std::uint32_t passIndex = 0u; passIndex < kPassCount; ++passIndex)
            {
                (void)graph.AddPass("ParallelRecordingBench" + std::to_string(passIndex), true);
            }

            auto compiled = graph.Compile();
            if (!compiled.has_value())
            {
                return Graphics::CompiledRenderGraph{};
            }
            return *compiled;
        }

        [[nodiscard]] std::uint64_t RecordSyntheticPass(const std::uint32_t passIndex) noexcept
        {
            std::uint64_t value = 0x9E3779B97F4A7C15ull ^
                                  (static_cast<std::uint64_t>(passIndex + 1u) * 0x100000001B3ull);
            for (std::uint32_t i = 0u; i < kRecordOpsPerPass; ++i)
            {
                value ^= value >> 29u;
                value *= 0xBF58476D1CE4E5B9ull;
                value ^= static_cast<std::uint64_t>(i + 1u) *
                         static_cast<std::uint64_t>(passIndex + 17u);
            }
            return value;
        }

        [[nodiscard]] std::uint64_t ReduceChecksums(const std::vector<std::uint64_t>& checksums) noexcept
        {
            std::uint64_t total = 0u;
            for (const std::uint64_t value : checksums)
            {
                total ^= value + 0x9E3779B97F4A7C15ull + (total << 6u) + (total >> 2u);
            }
            return total;
        }

        [[nodiscard]] std::uint64_t ExecuteSerial(const Graphics::CompiledRenderGraph& compiled,
                                                  std::vector<std::uint64_t>& checksums,
                                                  bool& succeeded)
        {
            std::ranges::fill(checksums, 0u);
            Graphics::RenderGraphExecutor executor{};
            const Core::Result result = executor.Execute(
                compiled,
                [&checksums](const std::uint32_t passIndex)
                {
                    if (passIndex < checksums.size())
                    {
                        checksums[passIndex] = RecordSyntheticPass(passIndex);
                    }
                },
                {});
            succeeded = result.has_value();
            return ReduceChecksums(checksums);
        }

        [[nodiscard]] std::uint64_t ExecuteParallel(const Graphics::CompiledRenderGraph& compiled,
                                                    std::vector<std::uint64_t>& checksums,
                                                    Graphics::ParallelRecordStats& stats,
                                                    bool& succeeded)
        {
            std::ranges::fill(checksums, 0u);
            stats = {};
            Graphics::RenderGraphExecutor executor{};
            const Core::Result result = executor.ExecuteParallelRecordJoin(
                compiled,
                [&checksums](const std::uint32_t passIndex,
                             const std::uint32_t) -> Core::Result
                {
                    if (passIndex >= checksums.size())
                    {
                        return Core::Err(Core::ErrorCode::OutOfRange);
                    }
                    checksums[passIndex] = RecordSyntheticPass(passIndex);
                    return Core::Ok();
                },
                {},
                {},
                &stats,
                Graphics::ParallelRecordOptions{
                    .UseScheduler = true,
                    .MinWorkerPassCount = 1u,
                });
            succeeded = result.has_value();
            return ReduceChecksums(checksums);
        }

        template <typename ExecuteFn>
        [[nodiscard]] double MeasureMilliseconds(ExecuteFn execute,
                                                 std::uint64_t& checksum,
                                                 bool& allSucceeded)
        {
            const auto begin = std::chrono::steady_clock::now();
            for (std::uint32_t i = 0u; i < kMeasuredIterations; ++i)
            {
                bool iterationSucceeded = false;
                checksum = execute(iterationSucceeded);
                allSucceeded = allSucceeded && iterationSucceeded;
            }
            const auto end = std::chrono::steady_clock::now();
            const auto totalNs =
                std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
            return (static_cast<double>(totalNs) /
                    static_cast<double>(kMeasuredIterations)) *
                   1.0e-6;
        }
    } // namespace

    RenderGraphParallelRecordingSmokeMetrics RunRenderGraphParallelRecordingSmoke()
    {
        SchedulerScope scheduler{kSchedulerWorkerCount};
        const Graphics::CompiledRenderGraph compiled = CompileIndependentPassGraph();
        std::vector<std::uint64_t> checksums(kPassCount, 0u);

        Graphics::ParallelRecordStats warmupStats{};
        bool warmupSucceeded = true;
        for (std::uint32_t i = 0u; i < kWarmupIterations; ++i)
        {
            bool serialOk = false;
            bool parallelOk = false;
            (void)ExecuteSerial(compiled, checksums, serialOk);
            (void)ExecuteParallel(compiled, checksums, warmupStats, parallelOk);
            warmupSucceeded = warmupSucceeded && serialOk && parallelOk;
        }

        std::uint64_t serialChecksum = 0u;
        std::uint64_t parallelChecksum = 0u;
        bool serialSucceeded = warmupSucceeded;
        bool parallelSucceeded = warmupSucceeded;
        Graphics::ParallelRecordStats finalStats{};

        const double serialMs = MeasureMilliseconds(
            [&](bool& iterationSucceeded) -> std::uint64_t
            {
                return ExecuteSerial(compiled, checksums, iterationSucceeded);
            },
            serialChecksum,
            serialSucceeded);

        const double parallelMs = MeasureMilliseconds(
            [&](bool& iterationSucceeded) -> std::uint64_t
            {
                return ExecuteParallel(compiled, checksums, finalStats, iterationSucceeded);
            },
            parallelChecksum,
            parallelSucceeded);

        const double checksumDelta = static_cast<double>(
            std::max(serialChecksum, parallelChecksum) -
            std::min(serialChecksum, parallelChecksum));
        const double passDelta = static_cast<double>(
            compiled.PassCount > kPassCount
                ? compiled.PassCount - kPassCount
                : kPassCount - compiled.PassCount);
        const double qualityErrorL2 =
            std::sqrt((checksumDelta * checksumDelta) + (passDelta * passDelta));

        RenderGraphParallelRecordingSmokeMetrics metrics{};
        metrics.RuntimeMilliseconds = parallelMs;
        metrics.SerialRecordMilliseconds = serialMs;
        metrics.ParallelRecordMilliseconds = parallelMs;
        metrics.ParallelToSerialRuntimeRatio = serialMs > 0.0 ? parallelMs / serialMs : 0.0;
        metrics.QualityErrorL2 = qualityErrorL2;
        metrics.PassCount = compiled.PassCount;
        metrics.WarmupIterations = kWarmupIterations;
        metrics.MeasuredIterations = kMeasuredIterations;
        metrics.SchedulerWorkerCount = kSchedulerWorkerCount;
        metrics.RecordOpsPerPass = kRecordOpsPerPass;
        metrics.ParallelLayerCount = finalStats.LayerCount;
        metrics.ParallelMaxLayerWidth = finalStats.MaxLayerWidth;
        metrics.ParallelWorkerTaskCount = finalStats.WorkerTaskCount;
        metrics.ParallelCallerRecordCount = finalStats.CallerRecordCount;
        metrics.SerialChecksum = serialChecksum;
        metrics.ParallelChecksum = parallelChecksum;
        metrics.Succeeded = serialSucceeded &&
                            parallelSucceeded &&
                            compiled.PassCount == kPassCount &&
                            finalStats.UsedScheduler &&
                            finalStats.WorkerTaskCount == kPassCount &&
                            finalStats.CallerRecordCount == 0u &&
                            serialChecksum == parallelChecksum &&
                            qualityErrorL2 == 0.0 &&
                            serialMs > 0.0 &&
                            parallelMs > 0.0;
        return metrics;
    }
} // namespace Intrinsic::Bench::Rendering

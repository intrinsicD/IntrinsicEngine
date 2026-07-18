// CORE-008 - compiled TaskGraph plan-reuse smoke benchmarks.

#include "Bench.TaskGraphPlanReuseSmoke.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

import Extrinsic.Core.Dag.TaskGraph;
import Extrinsic.Core.Dag.Scheduler;

namespace Intrinsic::Bench::Core
{
    namespace
    {
        namespace Dag = Extrinsic::Core::Dag;

        constexpr std::array<std::string_view, 9u> kPassNames{
            "InputSnapshot",
            "TransformPropagation",
            "VisibilityClassification",
            "GeometryResidency",
            "MaterialResolve",
            "DrawPacketBuild",
            "LightPacketBuild",
            "OverlayPacketBuild",
            "PublishSnapshot",
        };

        template <std::size_t N>
        [[nodiscard]] double Median(std::array<double, N> samples)
        {
            std::sort(samples.begin(), samples.end());
            return samples[samples.size() / 2u];
        }

        void RegisterShape(
            Dag::TaskGraph& graph,
            const std::uint32_t passCount,
            const std::uint64_t epoch,
            std::vector<std::uint64_t>* callbackTrace)
        {
            for (std::uint32_t pass = 0u; pass < passCount; ++pass)
            {
                graph.AddPass(
                    kPassNames[pass],
                    [pass](Dag::TaskGraphBuilder& builder)
                    {
                        if (pass > 0u)
                        {
                            builder.ReadResource(Dag::ResourceId{pass - 1u, 1u});
                        }
                        builder.WriteResource(Dag::ResourceId{pass, 1u});
                    },
                    [callbackTrace, epoch, pass]()
                    {
                        if (callbackTrace != nullptr)
                        {
                            callbackTrace->push_back((epoch << 8u) | pass);
                        }
                    });
            }
        }

        [[nodiscard]] std::uint64_t Mix(
            std::uint64_t checksum,
            const std::uint64_t value) noexcept
        {
            checksum ^= value + 0x9e3779b97f4a7c15ULL +
                        (checksum << 6u) + (checksum >> 2u);
            return checksum;
        }

        [[nodiscard]] TaskGraphPlanReuseSmokeMetrics RunShape(
            const std::uint32_t passCount)
        {
            Dag::TaskGraph graph{Dag::TaskGraphExecutionMode::PlanOnly};
            std::uint64_t failureCount = 0u;
            std::uint64_t epoch = 1u;

            const auto runBatch = [&]() -> double
            {
                const auto startedAt = std::chrono::steady_clock::now();
                for (std::uint32_t iteration = 0u;
                     iteration < kTaskGraphPlanReuseEpochsPerBatch;
                     ++iteration, ++epoch)
                {
                    RegisterShape(graph, passCount, epoch, nullptr);
                    if (!graph.Compile().has_value())
                    {
                        ++failureCount;
                    }
                    if (!graph.ResetForReplay().has_value())
                    {
                        ++failureCount;
                    }
                }
                const auto finishedAt = std::chrono::steady_clock::now();
                const auto elapsedNs =
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        finishedAt - startedAt).count();
                return static_cast<double>(elapsedNs) * 1.0e-6 /
                       static_cast<double>(kTaskGraphPlanReuseEpochsPerBatch);
            };

            for (std::uint32_t warmup = 0u;
                 warmup < kTaskGraphPlanReuseWarmupBatches;
                 ++warmup)
            {
                (void)runBatch();
            }

            std::array<double, kTaskGraphPlanReuseMeasuredBatches> samples{};
            for (double& sample : samples)
            {
                sample = runBatch();
            }

            double qualityErrorSquared = 0.0;
            std::uint64_t planChecksum = 0xcbf29ce484222325ULL;
            std::uint64_t callbackChecksum = 0xcbf29ce484222325ULL;
            std::vector<std::uint64_t> callbackTrace{};
            callbackTrace.reserve(static_cast<std::size_t>(passCount) * 2u);

            const auto validateEpoch =
                [&](const std::uint64_t validationEpoch)
            {
                RegisterShape(
                    graph, passCount, validationEpoch, &callbackTrace);
                if (!graph.Compile().has_value())
                {
                    ++failureCount;
                    qualityErrorSquared += 1.0;
                    return;
                }

                auto plan = graph.BuildPlan();
                if (!plan.has_value())
                {
                    ++failureCount;
                    qualityErrorSquared += 1.0;
                    return;
                }

                qualityErrorSquared +=
                    std::pow(
                        static_cast<double>(plan->size()) -
                            static_cast<double>(passCount),
                        2.0);
                for (std::uint32_t order = 0u;
                     order < static_cast<std::uint32_t>(plan->size());
                     ++order)
                {
                    const auto passIndex = (*plan)[order].id.Index;
                    const auto expectedIndex =
                        std::min(order, passCount - 1u);
                    const auto delta =
                        static_cast<double>(passIndex) -
                        static_cast<double>(expectedIndex);
                    qualityErrorSquared += delta * delta;
                    planChecksum = Mix(
                        planChecksum,
                        (static_cast<std::uint64_t>((*plan)[order].topoOrder)
                         << 32u) |
                            passIndex);
                    graph.ExecutePass(passIndex);
                }
            };

            validateEpoch(0x101u);
            if (!graph.ResetForReplay().has_value())
            {
                ++failureCount;
                qualityErrorSquared += 1.0;
            }
            validateEpoch(0x202u);

            const auto expectedTraceCount =
                static_cast<std::size_t>(passCount) * 2u;
            qualityErrorSquared +=
                std::pow(
                    static_cast<double>(callbackTrace.size()) -
                        static_cast<double>(expectedTraceCount),
                    2.0);
            for (std::size_t index = 0u;
                 index < callbackTrace.size();
                 ++index)
            {
                const auto validationEpoch =
                    index < passCount ? 0x101u : 0x202u;
                const auto pass =
                    static_cast<std::uint64_t>(index % passCount);
                const auto expected = (validationEpoch << 8u) | pass;
                const auto delta =
                    static_cast<double>(callbackTrace[index]) -
                    static_cast<double>(expected);
                qualityErrorSquared += delta * delta;
                callbackChecksum = Mix(callbackChecksum, callbackTrace[index]);
            }

            const auto scheduleStats = graph.GetScheduleStats();
            const auto reuseStats = graph.GetPlanReuseStats();
            const double runtimeMs = Median(samples);

            TaskGraphPlanReuseSmokeMetrics metrics{};
            metrics.RuntimeMilliseconds = runtimeMs;
            metrics.ThroughputItemsPerSecond =
                runtimeMs > 0.0 ? 1'000.0 / runtimeMs : 0.0;
            metrics.QualityErrorL2 = std::sqrt(qualityErrorSquared);
            metrics.RuntimeSamplesMilliseconds = samples;
            metrics.WarmupBatches = kTaskGraphPlanReuseWarmupBatches;
            metrics.MeasuredBatches = kTaskGraphPlanReuseMeasuredBatches;
            metrics.EpochsPerBatch = kTaskGraphPlanReuseEpochsPerBatch;
            metrics.PassCount = passCount;
            metrics.EdgeCount = scheduleStats.edgeCount;
            metrics.LayerCount = scheduleStats.layerCount;
            metrics.CompileCallCount = reuseStats.CompileCallCount;
            metrics.PlanBuildCount = reuseStats.PlanBuildCount;
            metrics.PlanReuseCount = reuseStats.PlanReuseCount;
            metrics.PlanChecksum = planChecksum;
            metrics.CallbackChecksum = callbackChecksum;
            metrics.FailureCount = failureCount;
            metrics.LastCompileReusedPlan =
                reuseStats.LastCompileReusedPlan;
            metrics.Succeeded =
                runtimeMs > 0.0 &&
                metrics.ThroughputItemsPerSecond > 0.0 &&
                metrics.QualityErrorL2 == 0.0 &&
                metrics.FailureCount == 0u &&
                metrics.EdgeCount == passCount - 1u &&
                metrics.LayerCount == passCount &&
                metrics.CompileCallCount ==
                    metrics.PlanBuildCount + metrics.PlanReuseCount;
            return metrics;
        }
    } // namespace

    TaskGraphPlanReuseSmokeMetrics RunTaskGraphPlanReuseEcs3Smoke()
    {
        return RunShape(3u);
    }

    TaskGraphPlanReuseSmokeMetrics RunTaskGraphPlanReuseRenderPrep9Smoke()
    {
        return RunShape(9u);
    }
} // namespace Intrinsic::Bench::Core

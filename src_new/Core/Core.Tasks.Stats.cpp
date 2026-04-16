module;

#include <array>
#include <atomic>

module Extrinsic.Core.Tasks;

import :Internal;

namespace Extrinsic::Core::Tasks
{
    Scheduler::Stats Scheduler::GetStats()
    {
        Stats stats{};
        if (!s_Ctx)
            return stats;

        stats.InFlightTasks = s_Ctx->inFlightTasks.load(std::memory_order_acquire);
        stats.QueuedTasks = static_cast<uint64_t>(s_Ctx->queuedTaskCount.load(std::memory_order_relaxed));
        stats.ActiveTasks = static_cast<uint64_t>(s_Ctx->activeTaskCount.load(std::memory_order_relaxed));
        stats.InjectPushCount = s_Ctx->injectPushCount.load(std::memory_order_relaxed);
        stats.InjectPopCount = s_Ctx->injectPopCount.load(std::memory_order_relaxed);
        stats.LocalPopCount = s_Ctx->localPopCount.load(std::memory_order_relaxed);
        stats.StealPopCount = s_Ctx->stealPopCount.load(std::memory_order_relaxed);
        stats.TotalStealAttempts = s_Ctx->totalStealAttempts.load(std::memory_order_relaxed);
        stats.SuccessfulStealAttempts = s_Ctx->successfulStealAttempts.load(std::memory_order_relaxed);
        stats.ParkCount = s_Ctx->parkCount.load(std::memory_order_relaxed);
        stats.UnparkCount = s_Ctx->unparkCount.load(std::memory_order_relaxed);
        stats.ParkLatencyTotalNs = s_Ctx->parkLatencyTotalNs.load(std::memory_order_relaxed);
        stats.UnparkLatencyTotalNs = s_Ctx->unparkLatencyTotalNs.load(std::memory_order_relaxed);

        std::array<uint64_t, Detail::SchedulerContext::LatencyBucketCount> parkHistogram{};
        std::array<uint64_t, Detail::SchedulerContext::LatencyBucketCount> unparkHistogram{};
        for (size_t i = 0; i < Detail::SchedulerContext::LatencyBucketCount; ++i)
        {
            parkHistogram[i] = s_Ctx->parkLatencyHistogram[i].load(std::memory_order_relaxed);
            unparkHistogram[i] = s_Ctx->unparkLatencyHistogram[i].load(std::memory_order_relaxed);
        }

        stats.ParkLatencyP50Ns = Detail::EstimateLatencyPercentile(parkHistogram, stats.ParkCount, 0.50);
        stats.ParkLatencyP95Ns = Detail::EstimateLatencyPercentile(parkHistogram, stats.ParkCount, 0.95);
        stats.ParkLatencyP99Ns = Detail::EstimateLatencyPercentile(parkHistogram, stats.ParkCount, 0.99);
        stats.UnparkLatencyP50Ns = Detail::EstimateLatencyPercentile(unparkHistogram, stats.UnparkCount, 0.50);
        stats.UnparkLatencyP95Ns = Detail::EstimateLatencyPercentile(unparkHistogram, stats.UnparkCount, 0.95);
        stats.UnparkLatencyP99Ns = Detail::EstimateLatencyPercentile(unparkHistogram, stats.UnparkCount, 0.99);
        stats.UnparkLatencyTailSpreadNs =
            (stats.UnparkLatencyP99Ns >= stats.UnparkLatencyP50Ns)
                ? (stats.UnparkLatencyP99Ns - stats.UnparkLatencyP50Ns)
                : 0;

        stats.IdleWaitCount = s_Ctx->idleWaitCount.load(std::memory_order_relaxed);
        stats.IdleWaitTotalNs = s_Ctx->idleWaitTotalNs.load(std::memory_order_relaxed);
        stats.QueueContentionCount = s_Ctx->queueContentionCount.load(std::memory_order_relaxed);

        if (stats.TotalStealAttempts > 0)
        {
            stats.StealSuccessRatio =
                static_cast<double>(stats.SuccessfulStealAttempts) /
                static_cast<double>(stats.TotalStealAttempts);
        }

        stats.WorkerLocalDepths.reserve(s_Ctx->workerStates.size());
        stats.WorkerVictimStealCounts.reserve(s_Ctx->workerStates.size());
        for (auto& worker : s_Ctx->workerStates)
        {
            std::lock_guard lock(worker.localLock);
            stats.WorkerLocalDepths.push_back(static_cast<uint32_t>(worker.localDeque.size()));
            stats.WorkerVictimStealCounts.push_back(worker.stealCount.load(std::memory_order_relaxed));
        }

        return stats;
    }

    uint64_t Scheduler::GetParkCount() noexcept
    {
        if (!s_Ctx)
            return 0;
        return s_Ctx->parkCount.load(std::memory_order_acquire);
    }

    uint64_t Scheduler::GetUnparkCount() noexcept
    {
        if (!s_Ctx)
            return 0;
        return s_Ctx->unparkCount.load(std::memory_order_acquire);
    }

    const std::atomic<uint64_t>& Scheduler::ParkCountAtomic() noexcept
    {
        return s_Ctx->parkCount;
    }
}

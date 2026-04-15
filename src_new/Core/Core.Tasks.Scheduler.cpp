module;

#include <optional>

module Extrinsic.Core.Tasks;

import Extrinsic.Core.Logging;

#include "Core.Tasks.Internal.hpp"

namespace Extrinsic::Core::Tasks
{
    void Scheduler::Initialize(unsigned threadCount)
    {
        if (s_Ctx) return;
        s_Ctx = std::make_unique<Detail::SchedulerContext>();
        s_Ctx->isRunning = true;

        if (threadCount == 0)
            threadCount = std::thread::hardware_concurrency();
        if (threadCount > 2) threadCount--;

        s_Ctx->workerStates.resize(threadCount);

        for (unsigned i = 0; i < threadCount; ++i)
            s_Ctx->workers.emplace_back([i] { WorkerEntry(i); });

        Log::Info("Initialized Scheduler with {} worker threads.", threadCount);
    }

    void Scheduler::Shutdown()
    {
        if (!s_Ctx) return;

        s_Ctx->isRunning = false;
        s_Ctx->workSignal.fetch_add(1, std::memory_order_release);
        s_Ctx->workSignal.notify_all();

        for (auto& t : s_Ctx->workers)
        {
            if (t.joinable())
                t.join();
        }
        s_Ctx.reset();
    }

    void Scheduler::Dispatch(Job&& job)
    {
        if (!s_Ctx || !job.Valid())
            return;

        auto handle = job.m_Handle;
        auto alive = job.m_Alive;
        job.m_Handle = {};
        job.m_Alive = nullptr;

        Reschedule(handle, std::move(alive));
    }

    void Scheduler::Reschedule(std::coroutine_handle<> h, std::shared_ptr<std::atomic<bool>> alive)
    {
        if (!s_Ctx || !h)
            return;

        DispatchInternal(LocalTask([h, alive = std::move(alive)]() mutable
        {
            if (alive && !alive->load(std::memory_order_acquire))
                return;

            h.resume();
            if (h.done())
                h.destroy();
        }));
    }

    void Scheduler::WorkerEntry(unsigned threadIndex)
    {
        static constexpr uint32_t FairnessInterval = 32;
        s_WorkerIndex = static_cast<int>(threadIndex);
        uint32_t lastSignal = s_Ctx->workSignal.load(std::memory_order_acquire);
        uint32_t localPopBudget = FairnessInterval;

        while (s_Ctx->isRunning)
        {
            LocalTask task;
            const bool shouldForceFairness = localPopBudget == 0;
            const bool canUseLocal = !shouldForceFairness;
            bool hasTask = false;

            if (canUseLocal && TryPopLocal(threadIndex, task))
            {
                hasTask = true;
                if (localPopBudget > 0)
                    localPopBudget -= 1;
            }
            else
            {
                if (TrySteal(threadIndex, task) || TryPopInject(task))
                {
                    hasTask = true;
                    localPopBudget = FairnessInterval;
                }
            }

            if (hasTask)
            {
                OnTaskDequeuedAndRun(task);
            }
            else
            {
                const auto idleWaitStart = std::chrono::steady_clock::now();
                s_Ctx->workSignal.wait(lastSignal);
                const auto idleWaitNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - idleWaitStart).count();
                s_Ctx->idleWaitCount.fetch_add(1, std::memory_order_relaxed);
                s_Ctx->idleWaitTotalNs.fetch_add(static_cast<uint64_t>(idleWaitNs), std::memory_order_relaxed);
                lastSignal = s_Ctx->workSignal.load(std::memory_order_acquire);
                localPopBudget = FairnessInterval;
            }
        }

        s_WorkerIndex = -1;
    }

    void Scheduler::DispatchInternal(LocalTask&& task)
    {
        if (!s_Ctx || !s_Ctx->isRunning)
            return;

        s_Ctx->inFlightTasks.fetch_add(1, std::memory_order_release);
        s_Ctx->activeTaskCount.fetch_add(1, std::memory_order_relaxed);
        s_Ctx->queuedTaskCount.fetch_add(1, std::memory_order_relaxed);

        if (s_WorkerIndex >= 0)
        {
            auto& worker = s_Ctx->workerStates[static_cast<unsigned>(s_WorkerIndex)];
            std::lock_guard lock(worker.localLock);
            worker.localDeque.push_back(std::move(task));
        }
        else
        {
            (void)EnqueueInject(std::move(task));
        }

        s_Ctx->workSignal.fetch_add(1, std::memory_order_release);
        s_Ctx->workSignal.notify_one();
    }

    bool Scheduler::IsInitialized() noexcept
    {
        return s_Ctx != nullptr;
    }

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

    void Scheduler::WaitForAll()
    {
        if (!s_Ctx)
            return;

        for (;;)
        {
            if (s_Ctx->inFlightTasks.load(std::memory_order_acquire) == 0)
                break;

            LocalTask task;
            if (TryPopTask(task, std::nullopt))
            {
                OnTaskDequeuedAndRun(task);
                continue;
            }

            const auto observed = s_Ctx->inFlightTasks.load(std::memory_order_acquire);
            if (observed > 0)
                s_Ctx->inFlightTasks.wait(observed, std::memory_order_relaxed);
            else
                Detail::CpuRelaxOrYield();
        }
    }
}

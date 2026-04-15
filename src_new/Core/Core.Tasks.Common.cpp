module;

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#endif

module Extrinsic.Core.Tasks;

import Extrinsic.Core.LockFreeQueue;

#include "Core.Tasks.Internal.hpp"

namespace Extrinsic::Core::Tasks
{
    namespace Detail
    {
        [[nodiscard]] bool CpuRelaxOnce() noexcept
        {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
            _mm_pause();
            return true;
#elif defined(__aarch64__) || defined(__arm__)
            asm volatile("yield" ::: "memory");
            return true;
#else
            return false;
#endif
        }

        void CpuRelaxOrYield() noexcept
        {
            if (!CpuRelaxOnce())
                std::this_thread::yield();
        }

        bool SpinLock::try_lock()
        {
            return !locked.exchange(true, std::memory_order_acquire);
        }

        void SpinLock::lock()
        {
            while (true)
            {
                if (!locked.exchange(true, std::memory_order_acquire))
                    return;

                constexpr int kSpinIters = 64;
                int spins = 0;
                while (locked.load(std::memory_order_relaxed))
                {
                    if (spins++ < kSpinIters)
                    {
                        CpuRelaxOrYield();
                        continue;
                    }

                    locked.wait(true, std::memory_order_relaxed);
                    spins = 0;
                }
            }
        }

        void SpinLock::unlock()
        {
            locked.store(false, std::memory_order_release);
            locked.notify_one();
        }

        SchedulerContext::WorkerState::WorkerState(WorkerState&& other) noexcept
            : localDeque(std::move(other.localDeque))
              , stealCount(other.stealCount.load(std::memory_order_relaxed))
        {
            other.stealCount.store(0, std::memory_order_relaxed);
        }

        SchedulerContext::WorkerState& SchedulerContext::WorkerState::operator=(WorkerState&& other) noexcept
        {
            if (this != &other)
            {
                localDeque = std::move(other.localDeque);
                stealCount.store(other.stealCount.load(std::memory_order_relaxed), std::memory_order_relaxed);
                other.stealCount.store(0, std::memory_order_relaxed);
            }
            return *this;
        }

        uint64_t EstimateLatencyPercentile(
            const std::array<uint64_t, SchedulerContext::LatencyBucketCount>& histogram,
            const uint64_t totalSamples,
            const double percentile)
        {
            if (totalSamples == 0)
                return 0;

            const auto targetRank = static_cast<uint64_t>(
                std::ceil(std::clamp(percentile, 0.0, 1.0) * static_cast<double>(totalSamples)));
            uint64_t prefix = 0;
            for (size_t i = 0; i < histogram.size(); ++i)
            {
                prefix += histogram[i];
                if (prefix >= targetRank)
                    return (i == 0) ? 0 : (1ull << (i - 1));
            }

            return 1ull << (histogram.size() - 2);
        }

        size_t LatencyBucketIndex(const uint64_t latencyNs)
        {
            if (latencyNs == 0)
                return 0;

            const uint32_t msb = std::bit_width(latencyNs) - 1;
            return std::min<size_t>(msb + 1, SchedulerContext::LatencyBucketCount - 1);
        }

        void RecordLatencySample(
            std::array<std::atomic<uint64_t>, SchedulerContext::LatencyBucketCount>& histogram,
            const uint64_t latencyNs)
        {
            histogram[LatencyBucketIndex(latencyNs)].fetch_add(1, std::memory_order_relaxed);
        }
    }

    std::unique_ptr<Detail::SchedulerContext> s_Ctx;
    thread_local int s_WorkerIndex = -1;

    [[nodiscard]] bool EnqueueInject(LocalTask&& task)
    {
        bool pushed = s_Ctx->globalQueue.Push(std::move(task));
        s_Ctx->injectPushCount.fetch_add(1, std::memory_order_relaxed);
        if (pushed)
            return true;

        std::lock_guard lock(s_Ctx->overflowMutex);
        s_Ctx->overflowQueue.push_back(std::move(task));
        s_Ctx->hasOverflow.store(true, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool TryPopInject(LocalTask& outTask)
    {
        if (s_Ctx->globalQueue.Pop(outTask))
        {
            s_Ctx->injectPopCount.fetch_add(1, std::memory_order_relaxed);
            return true;
        }

        if (!s_Ctx->hasOverflow.load(std::memory_order_acquire))
            return false;

        std::lock_guard lock(s_Ctx->overflowMutex);
        if (s_Ctx->overflowQueue.empty())
        {
            s_Ctx->hasOverflow.store(false, std::memory_order_release);
            return false;
        }

        outTask = std::move(s_Ctx->overflowQueue.front());
        s_Ctx->overflowQueue.pop_front();
        s_Ctx->injectPopCount.fetch_add(1, std::memory_order_relaxed);
        if (s_Ctx->overflowQueue.empty())
            s_Ctx->hasOverflow.store(false, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool TryPopLocal(unsigned workerIndex, LocalTask& outTask)
    {
        auto& worker = s_Ctx->workerStates[workerIndex];
        std::lock_guard lock(worker.localLock);
        if (worker.localDeque.empty())
            return false;

        outTask = std::move(worker.localDeque.back());
        worker.localDeque.pop_back();
        s_Ctx->localPopCount.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    [[nodiscard]] bool TrySteal(unsigned thiefIndex, LocalTask& outTask)
    {
        const auto workerCount = static_cast<unsigned>(s_Ctx->workerStates.size());
        if (workerCount <= 1)
            return false;

        for (unsigned offset = 1; offset < workerCount; ++offset)
        {
            s_Ctx->totalStealAttempts.fetch_add(1, std::memory_order_relaxed);
            const unsigned victimIndex = (thiefIndex + offset) % workerCount;
            auto& victim = s_Ctx->workerStates[victimIndex];
            if (!victim.localLock.try_lock())
            {
                s_Ctx->queueContentionCount.fetch_add(1, std::memory_order_relaxed);
                continue;
            }

            bool stole = false;
            if (!victim.localDeque.empty())
            {
                outTask = std::move(victim.localDeque.front());
                victim.localDeque.pop_front();
                victim.stealCount.fetch_add(1, std::memory_order_relaxed);
                s_Ctx->stealPopCount.fetch_add(1, std::memory_order_relaxed);
                s_Ctx->successfulStealAttempts.fetch_add(1, std::memory_order_relaxed);
                stole = true;
            }
            victim.localLock.unlock();

            if (stole)
                return true;
        }

        return false;
    }

    [[nodiscard]] bool TryPopTask(LocalTask& outTask, std::optional<unsigned> workerIndex)
    {
        if (workerIndex.has_value() && TryPopLocal(*workerIndex, outTask))
            return true;

        if (TryPopInject(outTask))
            return true;

        if (workerIndex.has_value() && TrySteal(*workerIndex, outTask))
            return true;

        return false;
    }

    void OnTaskDequeuedAndRun(LocalTask& task)
    {
        s_Ctx->queuedTaskCount.fetch_sub(1, std::memory_order_relaxed);
        if (task.Valid())
            task();

        s_Ctx->activeTaskCount.fetch_sub(1, std::memory_order_relaxed);
        const auto remaining = s_Ctx->inFlightTasks.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (remaining == 0)
            s_Ctx->inFlightTasks.notify_all();
    }
}

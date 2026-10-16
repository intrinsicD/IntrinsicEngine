module;

#include <atomic>
#include <coroutine>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

module Extrinsic.Core.Tasks;

import :Internal;

namespace Extrinsic::Core::Tasks
{
    namespace
    {
        struct ResumeTask
        {
            std::coroutine_handle<> Handle;
            std::shared_ptr<std::atomic<bool>> Alive;

            ResumeTask(std::coroutine_handle<> handle,
                       std::shared_ptr<std::atomic<bool>> alive) noexcept
                : Handle(handle), Alive(std::move(alive)) {}
            ResumeTask(ResumeTask&& other) noexcept
                : Handle(std::exchange(other.Handle, {})), Alive(std::move(other.Alive)) {}
            ~ResumeTask()
            {
                if (Handle && (!Alive || Alive->exchange(false, std::memory_order_acq_rel)))
                    Handle.destroy();
            }
            void operator()()
            {
                const auto handle = std::exchange(Handle, {});
                if (handle && (!Alive || Alive->load(std::memory_order_acquire)))
                    handle.resume();
            }
        };

        inline constexpr std::uint8_t NormalPriorityLane =
            static_cast<std::uint8_t>(DispatchPriority::Normal);

        [[nodiscard]] constexpr std::uint8_t PriorityLaneIndex(
            const DispatchPriority priority) noexcept
        {
            const auto lane = static_cast<std::uint8_t>(priority);
            return lane < Detail::PriorityLaneCount
                ? lane
                : static_cast<std::uint8_t>(DispatchPriority::Normal);
        }
    }

    thread_local std::uint8_t s_CurrentPriorityLane = NormalPriorityLane;

    void Scheduler::Dispatch(Job&& job)
    {
        Dispatch(DispatchPriority::Normal, std::move(job));
    }

    void Scheduler::Dispatch(const DispatchPriority priority, Job&& job)
    {
        if (!s_Ctx || !job.Valid())
            return;

        auto handle = job.m_Handle;
        auto alive = job.m_Alive;
        job.m_Handle = {};
        job.m_Alive = nullptr;

        DispatchInternal(LocalTask(ResumeTask(handle, std::move(alive))), priority);
    }

    void Scheduler::Reschedule(std::coroutine_handle<> h, std::shared_ptr<std::atomic<bool>> alive)
    {
        Reschedule(h, std::move(alive), static_cast<DispatchPriority>(s_CurrentPriorityLane));
    }

    void Scheduler::Reschedule(std::coroutine_handle<> h, std::shared_ptr<std::atomic<bool>> alive,
                               const DispatchPriority priority)
    {
        DispatchInternal(LocalTask(ResumeTask(h, std::move(alive))), priority);
    }

    void Scheduler::DispatchInternal(LocalTask&& task,
                                     const DispatchPriority priority)
    {
        if (!s_Ctx || !s_Ctx->isRunning.load(std::memory_order_acquire))
            return;

        const std::uint8_t lane = PriorityLaneIndex(priority);
        s_Ctx->inFlightTasks.fetch_add(1, std::memory_order_release);
        s_Ctx->activeTaskCount.fetch_add(1, std::memory_order_relaxed);
        s_Ctx->queuedTaskCount.fetch_add(1, std::memory_order_relaxed);
        if (lane != NormalPriorityLane)
        {
            s_Ctx->queuedTaskCountByLane[lane].fetch_add(
                1, std::memory_order_relaxed);
        }

        if (s_WorkerIndex >= 0 && lane < Detail::HelpableLaneCount)
        {
            auto& worker = s_Ctx->workerStates[static_cast<unsigned>(s_WorkerIndex)];
            std::lock_guard lock(worker.localLock);
            worker.localDeques[lane].push_back(std::move(task));
        }
        else
        {
            (void)EnqueueInject(std::move(task), lane);
        }

        // This increment and the parked-count observation pair with the
        // worker's seq_cst park publication and signal recheck. Either the
        // worker sees this signal before sleeping or this dispatch sees the
        // published parked worker and notifies it.
        s_Ctx->workSignal.fetch_add(1, std::memory_order_seq_cst);
        if (s_Ctx->parkedWorkerCount.load(std::memory_order_seq_cst) != 0u)
        {
            s_Ctx->workerWakeNotificationCount.fetch_add(
                1u, std::memory_order_relaxed);
            s_Ctx->workSignal.notify_one();
        }
        PublishWorkProgress();
    }

    bool EnqueueInject(LocalTask&& task, const std::uint8_t lane)
    {
        auto& injectLane = s_Ctx->injectLanes[lane];
        // One FIFO owns worker-only tasks, including submissions from workers.
        const bool pushed = lane < Detail::HelpableLaneCount &&
                            injectLane.Queue.Push(std::move(task));
        s_Ctx->injectPushCount.fetch_add(1, std::memory_order_relaxed);
        if (pushed)
            return true;

        std::lock_guard lock(injectLane.OverflowMutex);
        injectLane.OverflowQueue.push_back(std::move(task));
        injectLane.HasOverflow.store(true, std::memory_order_release);
        return true;
    }

    bool TryPopWorkerTask(LocalTask& outTask,
                          const unsigned workerIndex,
                          std::uint8_t& poppedLane,
                          const bool allowLocal,
                          bool& poppedLocal)
    {
        poppedLocal = false;
        for (std::uint8_t lane = 0u;
             lane < Detail::PriorityLaneCount;
             ++lane)
        {
            // This count is an ordinary-worker hint only. A false negative is
            // closed by the work-signal park handshake. Definitive helper
            // scans never consult it.
            if (lane != NormalPriorityLane &&
                s_Ctx->queuedTaskCountByLane[lane].load(
                    std::memory_order_relaxed) == 0)
            {
                continue;
            }

            if (allowLocal && TryPopLocal(workerIndex, outTask, lane))
            {
                poppedLane = lane;
                poppedLocal = true;
                return true;
            }
            if (TrySteal(workerIndex, outTask, lane) ||
                TryPopGlobalInject(outTask, lane))
            {
                poppedLane = lane;
                return true;
            }
        }

        return false;
    }

    bool TryPopDefinitiveTask(LocalTask& outTask,
                              const std::optional<unsigned> workerIndex,
                              std::uint8_t& poppedLane)
    {
        // Helpers stop before the worker-only lanes so a waiting
        // thread never inlines a long job. Workers' own loop still drains
        // those lanes, so skipping them here cannot strand the task.
        for (std::uint8_t lane = 0u;
             lane < Detail::HelpableLaneCount;
             ++lane)
        {
            if (workerIndex.has_value() &&
                TryPopLocal(*workerIndex, outTask, lane))
            {
                poppedLane = lane;
                return true;
            }
            if (TryPopGlobalInject(outTask, lane) ||
                TryStealExternal(outTask, lane))
            {
                poppedLane = lane;
                return true;
            }
        }

        return false;
    }

    bool TryPopGlobalInject(LocalTask& outTask, const std::uint8_t lane)
    {
        auto& injectLane = s_Ctx->injectLanes[lane];
        if (injectLane.Queue.Pop(outTask))
        {
            s_Ctx->injectPopCount.fetch_add(1, std::memory_order_relaxed);
            return true;
        }

        if (!injectLane.HasOverflow.load(std::memory_order_acquire))
            return false;

        std::lock_guard lock(injectLane.OverflowMutex);
        if (injectLane.OverflowQueue.empty())
        {
            injectLane.HasOverflow.store(false, std::memory_order_release);
            return false;
        }

        outTask = std::move(injectLane.OverflowQueue.front());
        injectLane.OverflowQueue.pop_front();
        s_Ctx->injectPopCount.fetch_add(1, std::memory_order_relaxed);
        if (injectLane.OverflowQueue.empty())
            injectLane.HasOverflow.store(false, std::memory_order_release);
        return true;
    }

    bool TryPopLocal(const unsigned workerIndex,
                     LocalTask& outTask,
                     const std::uint8_t lane)
    {
        auto& worker = s_Ctx->workerStates[workerIndex];
        std::lock_guard lock(worker.localLock);
        auto& localDeque = worker.localDeques[lane];
        if (localDeque.empty())
            return false;

        outTask = std::move(localDeque.back());
        localDeque.pop_back();
        s_Ctx->localPopCount.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    bool TrySteal(const unsigned thiefIndex,
                  LocalTask& outTask,
                  const std::uint8_t lane)
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
            auto& localDeque = victim.localDeques[lane];
            if (!localDeque.empty())
            {
                outTask = std::move(localDeque.front());
                localDeque.pop_front();
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

    bool TryStealExternal(LocalTask& outTask, const std::uint8_t lane)
    {
        const auto workerCount = static_cast<unsigned>(s_Ctx->workerStates.size());
        for (unsigned victimIndex = 0; victimIndex < workerCount; ++victimIndex)
        {
            s_Ctx->totalStealAttempts.fetch_add(1, std::memory_order_relaxed);
            auto& victim = s_Ctx->workerStates[victimIndex];
            if (!victim.localLock.try_lock())
            {
                s_Ctx->queueContentionCount.fetch_add(1, std::memory_order_relaxed);
                // External helpers park after this scan. Wait for the short
                // queue critical section so contention cannot masquerade as
                // an empty victim deque and strand already-published work.
                victim.localLock.lock();
            }

            bool stole = false;
            auto& localDeque = victim.localDeques[lane];
            if (!localDeque.empty())
            {
                outTask = std::move(localDeque.front());
                localDeque.pop_front();
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

    void PublishWorkProgress() noexcept
    {
        // Publish after queue visibility or task retirement. The seq_cst
        // epoch/waiter handshake guarantees that a waiter either observes the
        // new epoch before parking or is visible to this notification.
        s_Ctx->workProgressEpoch.fetch_add(1u, std::memory_order_seq_cst);
        if (s_Ctx->externalProgressWaiters.load(std::memory_order_seq_cst) != 0u)
            s_Ctx->workProgressEpoch.notify_all();
    }

    bool Scheduler::TryRunOne()
    {
        if (!s_Ctx || !s_Ctx->isRunning.load(std::memory_order_acquire))
            return false;

        LocalTask task;
        std::uint8_t lane =
            static_cast<std::uint8_t>(DispatchPriority::Normal);
        const auto workerIndex = s_WorkerIndex >= 0
            ? std::optional<unsigned>{
                  static_cast<unsigned>(s_WorkerIndex)}
            : std::nullopt;
        // Explicit help may be followed by a progress wait, including when
        // the helper is itself a worker. Ignore advisory lane counts and wait
        // through short deque critical sections so empty means definitive.
        if (!TryPopDefinitiveTask(task, workerIndex, lane))
            return false;

        OnTaskDequeuedAndRun(task, lane);
        return true;
    }

    void OnTaskDequeuedAndRun(LocalTask& task, const std::uint8_t lane)
    {
        s_Ctx->queuedTaskCount.fetch_sub(1, std::memory_order_relaxed);
        if (lane != NormalPriorityLane)
        {
            s_Ctx->queuedTaskCountByLane[lane].fetch_sub(
                1, std::memory_order_relaxed);
        }
        // Nested help executes another task on this thread; restore its caller's
        // lane so a later park or yield retains the outer continuation's policy.
        const auto previousLane = std::exchange(s_CurrentPriorityLane, lane);
        if (task.Valid())
            task();
        s_CurrentPriorityLane = previousLane;

        s_Ctx->activeTaskCount.fetch_sub(1, std::memory_order_relaxed);
        const auto remaining = s_Ctx->inFlightTasks.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (remaining == 0)
            s_Ctx->inFlightTasks.notify_all();
        PublishWorkProgress();
    }
}

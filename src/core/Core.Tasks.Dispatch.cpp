module;

#include <atomic>
#include <optional>
#include <coroutine>
#include <memory>

module Extrinsic.Core.Tasks;

import :Internal;

namespace Extrinsic::Core::Tasks
{
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

    bool EnqueueInject(LocalTask&& task)
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

    bool TryPopTask(LocalTask& outTask, std::optional<unsigned> workerIndex)
    {
        if (workerIndex.has_value() && TryPopLocal(*workerIndex, outTask))
            return true;

        if (TryPopGlobalInject(outTask))
            return true;

        if (workerIndex.has_value() && TrySteal(*workerIndex, outTask))
            return true;

        return false;
    }

    bool TryPopGlobalInject(LocalTask& outTask)
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

    bool TryPopLocal(unsigned workerIndex, LocalTask& outTask)
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

    bool TrySteal(unsigned thiefIndex, LocalTask& outTask)
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

module;
#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>

module Core.Tasks;
import Core.Logging;

namespace Core::Tasks
{
    struct SchedulerContext
    {
        std::vector<std::thread> workers;
        std::deque<LocalTask> globalQueue; // Now stores LocalTask (No std::function allocs)

        std::mutex queueMutex;
        std::condition_variable wakeCondition;

        std::atomic<bool> isRunning{false};
        std::atomic<int> activeTaskCount{0};
        std::atomic<int> queuedTaskCount{0}; // Tracked separately to avoid queue locking for size

        std::condition_variable waitCondition;
        std::mutex waitMutex;
    };

    static std::unique_ptr<SchedulerContext> s_Ctx = nullptr;

    void Scheduler::Initialize(unsigned threadCount)
    {
        if (s_Ctx) return;
        s_Ctx = std::make_unique<SchedulerContext>();
        s_Ctx->isRunning = true;

        if (threadCount == 0)
        {
            threadCount = std::thread::hardware_concurrency();
            if (threadCount > 2) threadCount--; // Leave core for OS/Main
        }

        Log::Info("Initializing Scheduler with {} worker threads.", threadCount);

        for (unsigned i = 0; i < threadCount; ++i)
        {
            s_Ctx->workers.emplace_back([i] { WorkerEntry(i); });
        }
    }

    void Scheduler::Shutdown()
    {
        if (!s_Ctx) return;

        // Stop accepting work
        {
            std::lock_guard lock(s_Ctx->queueMutex);
            s_Ctx->isRunning = false;
        }
        s_Ctx->wakeCondition.notify_all();

        for (auto& t : s_Ctx->workers)
        {
            if (t.joinable()) t.join();
        }
        s_Ctx.reset();
    }

    void Scheduler::DispatchInternal(LocalTask&& task)
    {
        if (!s_Ctx) return;

        // Optimization: Increment counters BEFORE lock
        // This ensures WaitForAll doesn't accidentally exit if it sees 0 queued
        // before the worker picks it up.
        s_Ctx->activeTaskCount.fetch_add(1, std::memory_order_relaxed);
        s_Ctx->queuedTaskCount.fetch_add(1, std::memory_order_release);

        {
            std::lock_guard lock(s_Ctx->queueMutex);
            s_Ctx->globalQueue.emplace_back(std::move(task));
        }
        s_Ctx->wakeCondition.notify_one();
    }

    void Scheduler::WaitForAll()
    {
        if (!s_Ctx) return;

        //Help with work while waiting
        while (s_Ctx->activeTaskCount.load(std::memory_order_acquire) > 0 ||
            s_Ctx->queuedTaskCount.load(std::memory_order_acquire) > 0)
        {
            LocalTask task;
            bool foundWork = false;

            // Try to steal work
            {
                std::unique_lock lock(s_Ctx->queueMutex);
                if (!s_Ctx->globalQueue.empty())
                {
                    task = std::move(s_Ctx->globalQueue.front());
                    s_Ctx->globalQueue.pop_front();
                    s_Ctx->queuedTaskCount.fetch_sub(1, std::memory_order_relaxed);
                    foundWork = true;
                }
            }

            if (foundWork && task.Valid())
            {
                // We picked up a task, so we are effectively a worker now.
                // activeTaskCount was already incremented by Dispatch.
                task();
                s_Ctx->activeTaskCount.fetch_sub(1, std::memory_order_release);
            }
            else
            {
                // No work left in queue, but active tasks exist on other threads.
                // Now we yield/wait.
                std::unique_lock lock(s_Ctx->waitMutex);
                s_Ctx->waitCondition.wait(lock, []
                {
                    return s_Ctx->activeTaskCount.load(std::memory_order_acquire) == 0;
                });
                break; // Exit loop if condition met
            }
        }
    }

    void Scheduler::WorkerEntry(unsigned)
    {
        while (true)
        {
            LocalTask task;

            // SCOPE 1: Acquire Task
            {
                std::unique_lock lock(s_Ctx->queueMutex);

                s_Ctx->wakeCondition.wait(lock, []
                {
                    return !s_Ctx->globalQueue.empty() || !s_Ctx->isRunning;
                });

                if (!s_Ctx->isRunning && s_Ctx->globalQueue.empty())
                {
                    return;
                }

                if (!s_Ctx->globalQueue.empty())
                {
                    task = std::move(s_Ctx->globalQueue.front());
                    s_Ctx->globalQueue.pop_front();
                    s_Ctx->queuedTaskCount.fetch_sub(1, std::memory_order_relaxed);
                }
            }

            // SCOPE 2: Execute Task (Unlocked)
            if (task.Valid())
            {
                task();

                // Decrement active count
                int remaining = s_Ctx->activeTaskCount.fetch_sub(1, std::memory_order_release) - 1;

                // Signal Main Thread if all done
                if (remaining == 0)
                {
                    // Optimization: Only lock/notify if we hit zero
                    std::lock_guard waitLock(s_Ctx->waitMutex);
                    s_Ctx->waitCondition.notify_all();
                }
            }
        }
    }
}

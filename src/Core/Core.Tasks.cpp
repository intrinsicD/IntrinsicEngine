module;

#include <vector>
#include <thread>
#include <queue>
#include <mutex>
#include <memory>
#include <condition_variable>
#include <atomic>
#include <functional>

module Core.Tasks;
import Core.Logging; // We use our new logger

namespace Core::Tasks
{
    // Global Scheduler State
    struct SchedulerContext
    {
        std::vector<std::thread> workers;
        std::deque<TaskFunction> globalQueue;

        std::mutex queueMutex;
        std::condition_variable wakeCondition;

        std::atomic<bool> isRunning{false};
        std::atomic<int> activeTaskCount{0};
        std::condition_variable waitCondition; // For WaitForAll
    };

    static std::unique_ptr<SchedulerContext> s_Ctx = nullptr;

    void Scheduler::Initialize(unsigned threadCount)
    {
        if (s_Ctx) return; // Already initialized

        s_Ctx = std::make_unique<SchedulerContext>();
        s_Ctx->isRunning = true;

        // Auto-detect
        if (threadCount == 0)
        {
            threadCount = std::thread::hardware_concurrency();
            // Leave one core free for the OS if we have many
            if (threadCount > 4) threadCount--;
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
        WaitForAll();

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
        Log::Info("Scheduler Shutdown.");
    }

    void Scheduler::Dispatch(TaskFunction&& task)
    {
        if (!s_Ctx) {
            Log::Error("Scheduler::Dispatch called before Initialize");
            return;
        }

        // Increment counter BEFORE enqueuing to prevent race condition
        // where worker grabs and completes task before WaitForAll sees the increment
        ++s_Ctx->activeTaskCount;

        {
            std::lock_guard lock(s_Ctx->queueMutex);
            s_Ctx->globalQueue.push_back(std::move(task));
        }
        s_Ctx->wakeCondition.notify_one();
    }

    void Scheduler::WaitForAll()
    {
        if (!s_Ctx) {
            Log::Error("Scheduler::WaitForAll called before Initialize");
            return;
        }
        std::unique_lock lock(s_Ctx->queueMutex);
        s_Ctx->waitCondition.wait(lock, []
        {
            return s_Ctx->globalQueue.empty() && s_Ctx->activeTaskCount == 0;
        });
    }

    void Scheduler::WorkerEntry(unsigned threadIndex)
    {
        // Identify thread
        // Log::Debug("Worker {} started.", threadIndex);

        while (true)
        {
            TaskFunction task;

            {
                std::unique_lock lock(s_Ctx->queueMutex);

                // Wait for work or shutdown
                s_Ctx->wakeCondition.wait(lock, []
                {
                    return !s_Ctx->globalQueue.empty() || !s_Ctx->isRunning;
                });

                if (!s_Ctx->isRunning && s_Ctx->globalQueue.empty())
                {
                    return; // Exit loop
                }

                // Grab task
                task = std::move(s_Ctx->globalQueue.front());
                s_Ctx->globalQueue.pop_front();
            }

            // Execute Task
            task();

            // Decrement counter and notify waiter
            {
                std::lock_guard lock(s_Ctx->queueMutex);
                --s_Ctx->activeTaskCount;
                if (s_Ctx->activeTaskCount == 0 && s_Ctx->globalQueue.empty())
                {
                    s_Ctx->waitCondition.notify_all();
                }
            }
        }
    }
}

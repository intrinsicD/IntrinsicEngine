module;
#include <vector>
#include <thread>
#include <deque>
#include <mutex>
#include <memory>
#include <condition_variable>
#include <atomic>
#include <random>
#include <type_traits>
#include <utility>
#include <new>

module Core.Tasks;
import Core.Logging; // We use our new logger
import Core.Memory;

namespace Core::Tasks
{
    namespace
    {
        struct WorkStealingQueue
        {
            std::deque<TaskFunction> tasks;
            mutable std::mutex mutex;

            void Push(TaskFunction&& task)
            {
                std::lock_guard lock(mutex);
                tasks.push_back(std::move(task));
            }

            bool Pop(TaskFunction& out)
            {
                std::lock_guard lock(mutex);
                if (tasks.empty()) return false;
                out = std::move(tasks.back());
                tasks.pop_back();
                return true;
            }

            bool Steal(TaskFunction& out)
            {
                std::lock_guard lock(mutex);
                if (tasks.empty()) return false;
                out = std::move(tasks.front());
                tasks.pop_front();
                return true;
            }

            bool Empty() const
            {
                std::lock_guard lock(mutex);
                return tasks.empty();
            }
        };

        struct SchedulerContext
        {
            struct Worker
            {
                WorkStealingQueue queue;
            };

            std::vector<std::thread> workers;
            std::vector<Worker> workerData;

            WorkStealingQueue globalQueue; // Injector for external threads

            std::condition_variable workAvailable;
            std::condition_variable fenceCondition;
            std::mutex sleepMutex;

            std::atomic<bool> isRunning{false};
            std::atomic<size_t> pendingTasks{0};

            Core::Memory::LinearArena taskArena{512 * 1024};
            std::mutex taskArenaMutex;
        };

        static thread_local SchedulerContext::Worker* t_Worker = nullptr;
        static std::unique_ptr<SchedulerContext> s_Ctx = nullptr;

        bool HasPendingQueues()
        {
            if (!s_Ctx) return false;
            if (!s_Ctx->globalQueue.Empty()) return true;

            for (auto& worker : s_Ctx->workerData)
            {
                if (!worker.queue.Empty()) return true;
            }
            return false;
        }
    }

    void* Scheduler::AcquireTaskStorage(size_t size, size_t alignment)
    {
        if (!s_Ctx) return nullptr;
        std::lock_guard lock(s_Ctx->taskArenaMutex);
        auto mem = s_Ctx->taskArena.Alloc(size, alignment);
        if (!mem) return nullptr;
        return *mem;
    }

    void Scheduler::ResetTaskArena()
    {
        if (!s_Ctx) return;
        std::lock_guard lock(s_Ctx->taskArenaMutex);
        s_Ctx->taskArena.Reset();
    }

    void Scheduler::Initialize(unsigned threadCount)
    {
        if (s_Ctx) return; // Already initialized

        s_Ctx = std::make_unique<SchedulerContext>();
        s_Ctx->isRunning = true;

        if (threadCount == 0)
        {
            threadCount = std::thread::hardware_concurrency();
            if (threadCount > 4) threadCount--;
        }

        Log::Info("Initializing Scheduler with {} worker threads.", threadCount);

        s_Ctx->workerData.resize(threadCount);
        s_Ctx->workers.reserve(threadCount);
        for (unsigned i = 0; i < threadCount; ++i)
        {
            s_Ctx->workers.emplace_back([i] { WorkerEntry(i); });
        }
    }

    void Scheduler::Shutdown()
    {
        if (!s_Ctx) return;
        WaitForAll();

        s_Ctx->isRunning = false;
        s_Ctx->workAvailable.notify_all();

        for (auto& t : s_Ctx->workers)
        {
            if (t.joinable()) t.join();
        }

        s_Ctx.reset();
        Log::Info("Scheduler Shutdown.");
    }

    void Scheduler::Dispatch(TaskFunction&& task)
    {
        if (!s_Ctx)
        {
            Log::Error("Scheduler::Dispatch called before Initialize");
            return;
        }

        s_Ctx->pendingTasks.fetch_add(1, std::memory_order_relaxed);

        if (t_Worker)
        {
            t_Worker->queue.Push(std::move(task));
        }
        else
        {
            s_Ctx->globalQueue.Push(std::move(task));
        }

        s_Ctx->workAvailable.notify_one();
    }

    void Scheduler::WaitForAll()
    {
        if (!s_Ctx)
        {
            Log::Error("Scheduler::WaitForAll called before Initialize");
            return;
        }

        std::unique_lock fenceLock(s_Ctx->sleepMutex);
        s_Ctx->fenceCondition.wait(fenceLock, []
        {
            return s_Ctx->pendingTasks.load(std::memory_order_acquire) == 0 && !HasPendingQueues();
        });

        ResetTaskArena();
    }

    void Scheduler::WorkerEntry(unsigned threadIndex)
    {
        SchedulerContext::Worker& worker = s_Ctx->workerData[threadIndex];
        t_Worker = &worker;

        std::mt19937 rng(threadIndex + 1u);
        std::uniform_int_distribution<size_t> dist(0, s_Ctx->workerData.size() - 1);

        while (s_Ctx->isRunning)
        {
            TaskFunction task;
            if (!worker.queue.Pop(task))
            {
                if (s_Ctx->globalQueue.Steal(task))
                {
                    // grabbed from injector
                }
                else
                {
                    bool stole = false;
                    for (size_t attempt = 0; attempt < s_Ctx->workerData.size(); ++attempt)
                    {
                        size_t target = dist(rng);
                        if (target == threadIndex) continue;
                        if (s_Ctx->workerData[target].queue.Steal(task))
                        {
                            stole = true;
                            break;
                        }
                    }

                    if (!stole)
                    {
                        std::unique_lock lock(s_Ctx->sleepMutex);
                        s_Ctx->workAvailable.wait(lock, []
                        {
                            return !s_Ctx->isRunning || HasPendingQueues() || s_Ctx->pendingTasks.load(std::memory_order_relaxed) > 0;
                        });
                        continue;
                    }
                }
            }

            if (!task)
            {
                continue;
            }

            task();

            size_t remaining = s_Ctx->pendingTasks.fetch_sub(1, std::memory_order_acq_rel) - 1;
            if (remaining == 0 && !HasPendingQueues())
            {
                std::lock_guard lock(s_Ctx->sleepMutex);
                s_Ctx->fenceCondition.notify_all();
            }
        }
    }
}


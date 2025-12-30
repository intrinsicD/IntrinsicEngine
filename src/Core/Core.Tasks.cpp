module;
#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>

module Core:Tasks.Impl;
import :Tasks;
import :Logging;

namespace Core::Tasks
{

    LocalTask:: ~LocalTask()
    {
        if (m_VTable) std::destroy_at(m_VTable);
    }

    LocalTask::LocalTask(LocalTask&& other) noexcept
    {
        if (other.m_VTable)
        {
            other.m_VTable->MoveTo(m_Storage);
            m_VTable = reinterpret_cast<Concept*>(m_Storage);
            std::destroy_at(other.m_VTable);
            other.m_VTable = nullptr;
        }
    }

    LocalTask& LocalTask::operator=(LocalTask&& other) noexcept
    {
        if (this != &other)
        {
            if (m_VTable) std::destroy_at(m_VTable);
            m_VTable = nullptr;

            if (other.m_VTable)
            {
                other.m_VTable->MoveTo(m_Storage);
                m_VTable = reinterpret_cast<Concept*>(m_Storage);
                std::destroy_at(other.m_VTable);
                other.m_VTable = nullptr;
            }
        }
        return *this;
    }

    void LocalTask::operator()()
    {
        if (m_VTable) m_VTable->Execute();
    }

    struct SchedulerContext
    {
        std::vector<std::thread> workers;
        std::deque<LocalTask> globalQueue;

        std::mutex queueMutex;
        std::condition_variable wakeCondition; // Used to wake up sleeping WORKERS

        std::atomic<bool> isRunning{false};

        // We use std::atomic::wait on activeTaskCount, so no separate CV/Mutex needed for waiting
        std::atomic<int> activeTaskCount{0};
        std::atomic<int> queuedTaskCount{0};
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

        // Increment active count (Task is now tracked)
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

        // 1. Help with work while tasks are queued
        while (s_Ctx->queuedTaskCount.load(std::memory_order_acquire) > 0)
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
                // Execute stolen task
                task();

                // Task done. Decrement active count and notify any other waiters.
                s_Ctx->activeTaskCount.fetch_sub(1, std::memory_order_release);
                s_Ctx->activeTaskCount.notify_all();
            }
            else
            {
                // Queue counter suggested work, but queue was empty (race condition).
                // Yield briefly to let other threads progress.
                std::this_thread::yield();
            }
        }

        // 2. Efficient Wait (C++20)
        // Queue is empty, but workers might still be running tasks.
        // Wait until activeTaskCount drops to 0.
        int active = s_Ctx->activeTaskCount.load(std::memory_order_acquire);
        while (active > 0)
        {
            // Atomically waits if value is still 'active'.
            // This blocks at OS level (futex) preventing 100% CPU usage.
            s_Ctx->activeTaskCount.wait(active);

            // Reload value to check loop condition
            active = s_Ctx->activeTaskCount.load(std::memory_order_acquire);
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
                s_Ctx->activeTaskCount.fetch_sub(1, std::memory_order_release);

                // Notify WaitForAll() that a task has finished
                s_Ctx->activeTaskCount.notify_all();
            }
        }
    }
}
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
    struct SpinLock
    {
        std::atomic_flag flag = ATOMIC_FLAG_INIT;

        void lock()
        {
            while (flag.test_and_set(std::memory_order_acquire))
            {
#if defined(__cpp_lib_atomic_wait)
                flag.wait(true, std::memory_order_relaxed);
#else
                std::this_thread::yield();
#endif
            }
        }

        void unlock()
        {
            flag.clear(std::memory_order_release);
#if defined(__cpp_lib_atomic_wait)
            flag.notify_one();
#endif
        }
    };

    LocalTask::~LocalTask()
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

        SpinLock queueMutex;

        std::atomic<uint32_t> workSignal{0};

        std::atomic<bool> isRunning{false};

        // Existing counters remain for logic
        std::atomic<int> activeTaskCount{0}; // Tasks currently running on threads
        std::atomic<int> queuedTaskCount{0}; // Tasks sitting in the deque
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

        s_Ctx->isRunning = false;

        // Wake everyone up so they can exit
        s_Ctx->workSignal.fetch_add(1, std::memory_order_release);
        s_Ctx->workSignal.notify_all();

        for (auto& t : s_Ctx->workers)
        {
            if (t.joinable()) t.join();
        }
        s_Ctx.reset();
    }

    void Scheduler::DispatchInternal(LocalTask&& task)
    {
        if (!s_Ctx) return;

        // 1. Update Counters
        s_Ctx->activeTaskCount.fetch_add(1, std::memory_order_relaxed);
        s_Ctx->queuedTaskCount.fetch_add(1, std::memory_order_release);

        // 2. Push to Queue
        {
            std::lock_guard lock(s_Ctx->queueMutex);
            s_Ctx->globalQueue.emplace_back(std::move(task));
        }

        // 3. Wake up ONE worker (Futex wake)
        s_Ctx->workSignal.fetch_add(1, std::memory_order_release);
        s_Ctx->workSignal.notify_one();
    }

    void Scheduler::WaitForAll()
    {
        if (!s_Ctx) return;

        // 1. Work Stealing Loop
        // While there are tasks in the queue, the main thread helps out.
        while (s_Ctx->queuedTaskCount.load(std::memory_order_acquire) > 0)
        {
            LocalTask task;
            bool foundWork = false;

            {
                // Try locking with std::try_lock to avoid contention with workers?
                // For simplicity, standard lock is fine here.
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
                task();
                s_Ctx->activeTaskCount.fetch_sub(1, std::memory_order_release);
                s_Ctx->activeTaskCount.notify_all();
            }
            else
            {
                std::this_thread::yield();
            }
        }

        // 2. Wait for Active Tasks (C++20 Atomic Wait)
        // Queue is empty, but threads might still be processing the last items.
        int active = s_Ctx->activeTaskCount.load(std::memory_order_acquire);
        while (active > 0)
        {
            s_Ctx->activeTaskCount.wait(active);
            active = s_Ctx->activeTaskCount.load(std::memory_order_acquire);
        }
    }

    void Scheduler::WorkerEntry(unsigned)
    {
        // Capture current signal to know when it changes
        uint32_t lastSignal = s_Ctx->workSignal.load(std::memory_order_acquire);

        while (s_Ctx->isRunning)
        {
            LocalTask task;
            bool foundWork = false;

            // 1. Try Pop
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

            // 2. Execute or Sleep
            if (foundWork && task.Valid())
            {
                task();

                // Task Complete
                s_Ctx->activeTaskCount.fetch_sub(1, std::memory_order_release);

                // Wake up the Main Thread if it is blocked in WaitForAll
                s_Ctx->activeTaskCount.notify_all();
            }
            else
            {
                // No work found. Sleep efficiently using atomic wait.
                // We wait until 'workSignal' is NOT EQUAL to 'lastSignal'.
                s_Ctx->workSignal.wait(lastSignal);

                // Refresh signal value for next loop
                lastSignal = s_Ctx->workSignal.load(std::memory_order_acquire);
            }
        }
    }
}

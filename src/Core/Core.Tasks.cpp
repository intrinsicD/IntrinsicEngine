module;
#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>
#include <coroutine>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
  #include <immintrin.h> // _mm_pause
#endif

module Core:Tasks.Impl;
import :Tasks;
import :Logging;

namespace Core::Tasks
{
    namespace
    {
        [[nodiscard]] inline bool CpuRelaxOnce() noexcept
        {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
            _mm_pause();
            return true;
#elif defined(__aarch64__) || defined(__arm__)
            // Hint to the CPU that we're in a spin-wait loop.
            asm volatile("yield" ::: "memory");
            return true;
#else
            return false;
#endif
        }

        inline void CpuRelaxOrYield() noexcept
        {
            if (!CpuRelaxOnce())
                std::this_thread::yield();
        }
    }

    struct SpinLock
    {
        std::atomic<bool> locked = false;

        void lock()
        {
            // Hybrid strategy:
            // 1) Try to acquire.
            // 2)) If contended, do a short polite spin with CPU relax.
            // 3) If still contended, use atomic::wait (futex) to sleep.
            while (true)
            {
                if (!locked.exchange(true, std::memory_order_acquire))
                    return;

                // Bounded spin to reduce cache/memory-bus pressure under short contention.
                // Keep the load relaxed: we only care about observing a transition to false.
                constexpr int kSpinIters = 64;
                int spins = 0;
                while (locked.load(std::memory_order_relaxed))
                {
                    if (spins++ < kSpinIters)
                    {
                        CpuRelaxOrYield();
                        continue;
                    }

                    // Sleep until 'locked' changes from true -> false.
                    // Note: wait() may spuriously wake; we re-check in the outer loop.
                    locked.wait(true, std::memory_order_relaxed);
                    spins = 0;
                }
            }
        }

        void unlock()
        {
            locked.store(false, std::memory_order_release);
            locked.notify_one();
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

    // ---------------------------------------------------------------------
    // Coroutine support
    // ---------------------------------------------------------------------

    Job Job::promise_type::get_return_object() noexcept
    {
        return Job(std::coroutine_handle<promise_type>::from_promise(*this));
    }

    void Job::promise_type::unhandled_exception() noexcept
    {
        // Exceptions are disabled in this engine. If something still throws in user code,
        // we treat it as fatal-in-debug. In release, log and continue.
        Log::Error("Core::Tasks::Job encountered an unhandled exception (exceptions disabled). Terminating coroutine.");
    }

    void YieldAwaiter::await_suspend(std::coroutine_handle<> h) const noexcept
    {
        Scheduler::Reschedule(h);
    }

    void Scheduler::Reschedule(std::coroutine_handle<> h)
    {
        if (!s_Ctx) return;
        if (!h) return;

        // Enqueue a task that runs exactly one coroutine slice.
        // The coroutine runs until its next suspension point (e.g., Yield) or completion.
        DispatchInternal(LocalTask([h]() mutable {
            if (!h) return;

            h.resume();

            // With final_suspend = suspend_always, the coroutine frame is still valid here.
            if (h.done())
                h.destroy();
        }));
    }

    void Scheduler::Dispatch(Job&& job)
    {
        if (!s_Ctx) return;
        if (!job.Valid()) return;

        auto h = job.m_Handle;
        job.m_Handle = {};

        // Treat initial start just like any other continuation.
        Reschedule(h);
    }

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
                // Use lock_guard for exception safety (though we don't throw)
                std::lock_guard lock(s_Ctx->queueMutex);
                if (!s_Ctx->globalQueue.empty())
                {
                    // Decrement BEFORE popping to prevent race with workers
                    s_Ctx->queuedTaskCount.fetch_sub(1, std::memory_order_release);
                    task = std::move(s_Ctx->globalQueue.front());
                    s_Ctx->globalQueue.pop_front();
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
                std::lock_guard lock(s_Ctx->queueMutex);
                if (!s_Ctx->globalQueue.empty())
                {
                    // Decrement BEFORE popping to maintain consistency with WaitForAll
                    s_Ctx->queuedTaskCount.fetch_sub(1, std::memory_order_release);
                    task = std::move(s_Ctx->globalQueue.front());
                    s_Ctx->globalQueue.pop_front();
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

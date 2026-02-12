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

module Core.Tasks;
import Core.Logging;
import Utils.LockFreeQueue;

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

    struct alignas(64) SchedulerContext
    {
        std::vector<std::thread> workers;

        // 1. FAST PATH: MPMC Lock-Free Queue
        // 65536 slots = power of 2, sufficient for high throughput
        Utils::LockFreeQueue<LocalTask> globalQueue{65536};
        // 2. SLOW PATH: Unbounded Overflow Queue
        std::mutex overflowMutex;
        std::deque<LocalTask> overflowQueue;
        std::atomic<bool> hasOverflow{false}; // Optimization: Atomic flag to avoid locking mutex on read

        alignas(64) std::atomic<uint32_t> workSignal{0};
        alignas(64) std::atomic<bool> isRunning{false};
        alignas(64) std::atomic<int> activeTaskCount{0};
        alignas(64) std::atomic<int> queuedTaskCount{0};
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
        // YieldAwaiter does not carry an alive token; the coroutine is guaranteed
        // alive at the point of co_await since it's executing inside the frame.
        Scheduler::Reschedule(h);
    }

    void Scheduler::Reschedule(std::coroutine_handle<> h, std::shared_ptr<std::atomic<bool>> alive)
    {
        if (!s_Ctx) return;
        if (!h) return;

        // Enqueue a task that runs exactly one coroutine slice.
        // The alive token (if present) allows detecting if the owning Job was
        // destroyed before this task executes, preventing use-after-free.
        DispatchInternal(LocalTask([h, alive = std::move(alive)]() mutable {
            // If the owning Job was destroyed, the coroutine frame is gone.
            if (alive && !alive->load(std::memory_order_acquire))
                return;

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
        auto alive = job.m_Alive; // Share the lifetime token with the scheduler
        job.m_Handle = {};
        job.m_Alive = nullptr; // Transfer ownership to scheduler

        // Treat initial start just like any other continuation.
        Reschedule(h, std::move(alive));
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
        if (!s_Ctx || !s_Ctx->isRunning) return;

        // Increment active counters immediately
        s_Ctx->activeTaskCount.fetch_add(1, std::memory_order_relaxed);
        s_Ctx->queuedTaskCount.fetch_add(1, std::memory_order_release);

        // 1. Try Fast Path (Lock-Free)
        // No lock_guard here anymore!
        bool pushed = s_Ctx->globalQueue.Push(std::move(task));

        // 2. Fallback to Slow Path (Overflow)
        if (!pushed)
        {
            // Performance Warning: This should ideally not happen every frame.
            // Consider logging strictly once per second if this path is hit to alert developers.
            {
                std::lock_guard lock(s_Ctx->overflowMutex);
                s_Ctx->overflowQueue.push_back(std::move(task));
            }
            s_Ctx->hasOverflow.store(true, std::memory_order_release);
        }

        // 3. Wake up workers
        s_Ctx->workSignal.fetch_add(1, std::memory_order_release);
        s_Ctx->workSignal.notify_one();
    }

    static bool TryPopTask(LocalTask& outTask)
    {
        // 1. Try Fast Path (Lock-Free)
        // No lock_guard here anymore!
        if (s_Ctx->globalQueue.Pop(outTask)) return true;

        // 2. Try Slow Path (Overflow)
        // Optimization: Check atomic flag before acquiring mutex
        if (s_Ctx->hasOverflow.load(std::memory_order_acquire))
        {
            std::lock_guard lock(s_Ctx->overflowMutex);
            if (!s_Ctx->overflowQueue.empty())
            {
                outTask = std::move(s_Ctx->overflowQueue.front());
                s_Ctx->overflowQueue.pop_front();

                // Update flag if empty
                if (s_Ctx->overflowQueue.empty())
                    s_Ctx->hasOverflow.store(false, std::memory_order_release);

                return true;
            }
            else
            {
                // Race condition handle: flag was true, but queue is empty now
                s_Ctx->hasOverflow.store(false, std::memory_order_release);
            }
        }

        return false;
    }

    void Scheduler::WaitForAll()
    {
        if (!s_Ctx) return;

        // Work Stealing Loop
        while (s_Ctx->queuedTaskCount.load(std::memory_order_acquire) > 0)
        {
            LocalTask task;
            if (TryPopTask(task))
            {
                // Decrement queued count (item removed from storage)
                s_Ctx->queuedTaskCount.fetch_sub(1, std::memory_order_release);

                if (task.Valid())
                {
                    task();
                    // Decrement active count (execution finished)
                    s_Ctx->activeTaskCount.fetch_sub(1, std::memory_order_release);
                    s_Ctx->activeTaskCount.notify_all();
                }
            }
            else
            {
                // Yield if we see count > 0 but failed to pop (contention)
                std::this_thread::yield();
            }
        }

        // Wait for Active Tasks (other threads executing)
        int active = s_Ctx->activeTaskCount.load(std::memory_order_acquire);
        while (active > 0)
        {
            s_Ctx->activeTaskCount.wait(active);
            active = s_Ctx->activeTaskCount.load(std::memory_order_acquire);
        }
    }

    void Scheduler::WorkerEntry(unsigned)
    {
        uint32_t lastSignal = s_Ctx->workSignal.load(std::memory_order_acquire);

        while (s_Ctx->isRunning)
        {
            LocalTask task;

            if (TryPopTask(task))
            {
                s_Ctx->queuedTaskCount.fetch_sub(1, std::memory_order_release);

                if (task.Valid())
                {
                    task();
                    s_Ctx->activeTaskCount.fetch_sub(1, std::memory_order_release);
                    s_Ctx->activeTaskCount.notify_all();
                }
            }
            else
            {
                // Sleep until signaled
                s_Ctx->workSignal.wait(lastSignal);
                lastSignal = s_Ctx->workSignal.load(std::memory_order_acquire);
            }
        }
    }
}

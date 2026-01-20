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

    template <typename T, size_t Capacity>
    struct RingBuffer {
        std::array<T, Capacity> Buffer;
        std::atomic<size_t> Head{0};
        std::atomic<size_t> Tail{0};

        bool Push(T&& item) {
            size_t tail = Tail.load(std::memory_order_relaxed);
            size_t head = Head.load(std::memory_order_acquire);
            if (tail - head >= Capacity) return false; // Full

            Buffer[tail % Capacity] = std::move(item);
            Tail.store(tail + 1, std::memory_order_release);
            return true;
        }

        bool Pop(T& outItem) {
            size_t head = Head.load(std::memory_order_relaxed);
            size_t tail = Tail.load(std::memory_order_acquire);
            if (head >= tail) return false; // Empty

            outItem = std::move(Buffer[head % Capacity]);
            Head.store(head + 1, std::memory_order_release);
            return true;
        }
    };


    struct alignas(64) SchedulerContext
    {
        std::vector<std::thread> workers;

        // REPLACED: std::deque with RingBuffer (64k tasks = ~8MB of fixed memory)
        RingBuffer<LocalTask, 65536> globalQueue;
        SpinLock queueMutex;

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
        if (!s_Ctx || !s_Ctx->isRunning) return;

        s_Ctx->activeTaskCount.fetch_add(1, std::memory_order_relaxed);

        // We increment queued count tentatively
        s_Ctx->queuedTaskCount.fetch_add(1, std::memory_order_release);

        bool pushed = false;
        {
            std::lock_guard lock(s_Ctx->queueMutex);
            pushed = s_Ctx->globalQueue.Push(std::move(task));
        }

        if (!pushed)
        {
            // Critical failure: Queue full.
            // In a production engine, we might spin-wait here or fallback to a secondary overflow list.
            // For now, reverse the counters and log (task is dropped).
            s_Ctx->queuedTaskCount.fetch_sub(1, std::memory_order_relaxed);
            s_Ctx->activeTaskCount.fetch_sub(1, std::memory_order_relaxed);
            Log::Error("Scheduler RingBuffer Full! Task dropped.");
            return;
        }

        s_Ctx->workSignal.fetch_add(1, std::memory_order_release);
        s_Ctx->workSignal.notify_one();
    }

    void Scheduler::WaitForAll()
    {
        if (!s_Ctx) return;

        // 1. Work Stealing Loop
        while (s_Ctx->queuedTaskCount.load(std::memory_order_acquire) > 0)
        {
            LocalTask task;
            bool foundWork = false;

            {
                std::lock_guard lock(s_Ctx->queueMutex);
                // CHANGE: Use RingBuffer Pop
                if (s_Ctx->globalQueue.Pop(task))
                {
                    // Decrement BEFORE execution
                    s_Ctx->queuedTaskCount.fetch_sub(1, std::memory_order_release);
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
                // If queue count > 0 but Pop failed (contention), yield.
                std::this_thread::yield();
            }
        }

        // 2. Wait for Active Tasks (unchanged)
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
            bool foundWork = false;

            // 1. Try Pop
            {
                std::lock_guard lock(s_Ctx->queueMutex);
                // CHANGE: Use RingBuffer Pop
                if (s_Ctx->globalQueue.Pop(task))
                {
                    s_Ctx->queuedTaskCount.fetch_sub(1, std::memory_order_release);
                    foundWork = true;
                }
            }

            // 2. Execute or Sleep
            if (foundWork && task.Valid())
            {
                task();
                s_Ctx->activeTaskCount.fetch_sub(1, std::memory_order_release);
                s_Ctx->activeTaskCount.notify_all();
            }
            else
            {
                s_Ctx->workSignal.wait(lastSignal);
                lastSignal = s_Ctx->workSignal.load(std::memory_order_acquire);
            }
        }
    }
}

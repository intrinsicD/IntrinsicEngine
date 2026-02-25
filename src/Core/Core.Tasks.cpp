module;
#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>
#include <coroutine>
#include <optional>

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

        [[nodiscard]] bool try_lock()
        {
            return !locked.exchange(true, std::memory_order_acquire);
        }

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
        struct alignas(64) WorkerState
        {
            SpinLock localLock{};
            std::deque<LocalTask> localDeque{};
            std::atomic<uint64_t> stealCount{0};
        };

        std::vector<std::thread> workers;
        std::vector<WorkerState> workerStates;

        // Inject queue for external producers (main thread / IO threads).
        Utils::LockFreeQueue<LocalTask> globalQueue{65536};
        std::mutex overflowMutex;
        std::deque<LocalTask> overflowQueue;
        std::atomic<bool> hasOverflow{false};

        alignas(64) std::atomic<uint32_t> workSignal{0};
        alignas(64) std::atomic<bool> isRunning{false};

        // Synchronization contract.
        alignas(64) std::atomic<uint64_t> inFlightTasks{0};

        // Telemetry counters (non-authoritative for completion).
        alignas(64) std::atomic<int> activeTaskCount{0};
        alignas(64) std::atomic<int> queuedTaskCount{0};
    };

    static std::unique_ptr<SchedulerContext> s_Ctx = nullptr;
    static thread_local int s_WorkerIndex = -1;

    [[nodiscard]] static bool EnqueueInject(LocalTask&& task)
    {
        bool pushed = s_Ctx->globalQueue.Push(std::move(task));
        if (pushed)
            return true;

        std::lock_guard lock(s_Ctx->overflowMutex);
        s_Ctx->overflowQueue.push_back(std::move(task));
        s_Ctx->hasOverflow.store(true, std::memory_order_release);
        return true;
    }

    [[nodiscard]] static bool TryPopInject(LocalTask& outTask)
    {
        if (s_Ctx->globalQueue.Pop(outTask))
            return true;

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
        if (s_Ctx->overflowQueue.empty())
            s_Ctx->hasOverflow.store(false, std::memory_order_release);
        return true;
    }

    [[nodiscard]] static bool TryPopLocal(unsigned workerIndex, LocalTask& outTask)
    {
        auto& worker = s_Ctx->workerStates[workerIndex];
        std::lock_guard lock(worker.localLock);
        if (worker.localDeque.empty())
            return false;

        outTask = std::move(worker.localDeque.back());
        worker.localDeque.pop_back();
        return true;
    }

    [[nodiscard]] static bool TrySteal(unsigned thiefIndex, LocalTask& outTask)
    {
        const unsigned workerCount = static_cast<unsigned>(s_Ctx->workerStates.size());
        if (workerCount <= 1)
            return false;

        for (unsigned offset = 1; offset < workerCount; ++offset)
        {
            const unsigned victimIndex = (thiefIndex + offset) % workerCount;
            auto& victim = s_Ctx->workerStates[victimIndex];
            if (!victim.localLock.try_lock())
                continue;

            bool stole = false;
            if (!victim.localDeque.empty())
            {
                outTask = std::move(victim.localDeque.front());
                victim.localDeque.pop_front();
                victim.stealCount.fetch_add(1, std::memory_order_relaxed);
                stole = true;
            }
            victim.localLock.unlock();

            if (stole)
                return true;
        }

        return false;
    }

    [[nodiscard]] static bool TryPopTask(LocalTask& outTask, std::optional<unsigned> workerIndex)
    {
        if (workerIndex.has_value() && TryPopLocal(*workerIndex, outTask))
            return true;

        if (TryPopInject(outTask))
            return true;

        if (workerIndex.has_value() && TrySteal(*workerIndex, outTask))
            return true;

        return false;
    }

    static void OnTaskDequeuedAndRun(LocalTask& task)
    {
        s_Ctx->queuedTaskCount.fetch_sub(1, std::memory_order_relaxed);
        if (task.Valid())
            task();

        s_Ctx->activeTaskCount.fetch_sub(1, std::memory_order_relaxed);
        const auto remaining = s_Ctx->inFlightTasks.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (remaining == 0)
            s_Ctx->inFlightTasks.notify_all();
    }

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

        s_Ctx->workerStates.resize(threadCount);

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

        s_Ctx->inFlightTasks.fetch_add(1, std::memory_order_release);
        s_Ctx->activeTaskCount.fetch_add(1, std::memory_order_relaxed);
        s_Ctx->queuedTaskCount.fetch_add(1, std::memory_order_relaxed);

        // Worker-produced tasks go to local deque (LIFO) for cache locality.
        if (s_WorkerIndex >= 0)
        {
            auto& worker = s_Ctx->workerStates[static_cast<unsigned>(s_WorkerIndex)];
            std::lock_guard lock(worker.localLock);
            worker.localDeque.push_back(std::move(task));
        }
        else
        {
            EnqueueInject(std::move(task));
        }

        s_Ctx->workSignal.fetch_add(1, std::memory_order_release);
        s_Ctx->workSignal.notify_one();
    }

    void Scheduler::WaitForAll()
    {
        if (!s_Ctx) return;

        while (s_Ctx->inFlightTasks.load(std::memory_order_acquire) > 0)
        {
            LocalTask task;
            if (TryPopTask(task, std::nullopt))
            {
                OnTaskDequeuedAndRun(task);
            }
            else
            {
                const auto observed = s_Ctx->inFlightTasks.load(std::memory_order_acquire);
                if (observed > 0)
                    s_Ctx->inFlightTasks.wait(observed, std::memory_order_relaxed);
            }
        }
    }

    void Scheduler::WorkerEntry(unsigned threadIndex)
    {
        s_WorkerIndex = static_cast<int>(threadIndex);
        uint32_t lastSignal = s_Ctx->workSignal.load(std::memory_order_acquire);

        while (s_Ctx->isRunning)
        {
            LocalTask task;

            if (TryPopTask(task, threadIndex))
            {
                OnTaskDequeuedAndRun(task);
            }
            else
            {
                s_Ctx->workSignal.wait(lastSignal);
                lastSignal = s_Ctx->workSignal.load(std::memory_order_acquire);
            }
        }

        s_WorkerIndex = -1;
    }
}

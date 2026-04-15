module;

#include <atomic>
#include <coroutine>
#include <memory>
#include <thread>
#include <deque>
#include <array>
#include <cmath>
#include <optional>
#include <algorithm>
#include <vector>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h> // _mm_pause
#endif

module Extrinsic.Core.Tasks;

import Extrinsic.Core.LockFreeQueue;
import Extrinsic.Core.Logging;

namespace Extrinsic::Core::Tasks
{
    namespace Detail
    {
        namespace
        {
            [[nodiscard]] bool CpuRelaxOnce() noexcept
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

            void CpuRelaxOrYield() noexcept
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
                // 2) If contended, do a short polite spin with CPU relax.
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

        struct alignas(64) SchedulerContext
        {
            struct alignas(64) WorkerState
            {
                SpinLock localLock{};
                std::deque<LocalTask> localDeque{};
                std::atomic<uint64_t> stealCount{0};

                WorkerState() = default;
                WorkerState(const WorkerState&) = delete;
                WorkerState& operator=(const WorkerState&) = delete;

                WorkerState(WorkerState&& other) noexcept
                    : localDeque(std::move(other.localDeque))
                      , stealCount(other.stealCount.load(std::memory_order_relaxed))
                {
                    other.stealCount.store(0, std::memory_order_relaxed);
                }

                WorkerState& operator=(WorkerState&& other) noexcept
                {
                    if (this != &other)
                    {
                        // Note: we intentionally don't attempt to transfer lock state.
                        // Vector moves can only happen during initialization/resizing.
                        localDeque = std::move(other.localDeque);
                        stealCount.store(other.stealCount.load(std::memory_order_relaxed), std::memory_order_relaxed);
                        other.stealCount.store(0, std::memory_order_relaxed);
                    }
                    return *this;
                }
            };

            std::vector<std::thread> workers;
            std::vector<WorkerState> workerStates;
            LockFreeQueue<LocalTask> globalQueue{65536};
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

            // Scheduler telemetry counters.
            alignas(64) std::atomic<uint64_t> injectPushCount{0};
            alignas(64) std::atomic<uint64_t> injectPopCount{0};
            alignas(64) std::atomic<uint64_t> localPopCount{0};
            alignas(64) std::atomic<uint64_t> stealPopCount{0};
            alignas(64) std::atomic<uint64_t> totalStealAttempts{0};
            alignas(64) std::atomic<uint64_t> successfulStealAttempts{0};
            alignas(64) std::atomic<uint64_t> parkCount{0};
            alignas(64) std::atomic<uint64_t> unparkCount{0};
            alignas(64) std::atomic<uint64_t> parkLatencyTotalNs{0};
            alignas(64) std::atomic<uint64_t> unparkLatencyTotalNs{0};
            static constexpr size_t LatencyBucketCount = 64;
            std::array<std::atomic<uint64_t>, LatencyBucketCount> parkLatencyHistogram{};
            std::array<std::atomic<uint64_t>, LatencyBucketCount> unparkLatencyHistogram{};
            alignas(64) std::atomic<uint64_t> idleWaitCount{0};
            alignas(64) std::atomic<uint64_t> idleWaitTotalNs{0};
            alignas(64) std::atomic<uint64_t> queueContentionCount{0};

            static constexpr uint32_t InvalidParkedNode = std::numeric_limits<uint32_t>::max();

            struct ParkedContinuation
            {
                std::coroutine_handle<> Handle{};
                std::shared_ptr<std::atomic<bool>> Alive{};
                std::chrono::steady_clock::time_point ParkedAt{};
            };

            struct WaitSlot
            {
                uint32_t generation = 1;
                bool inUse = false;
                uint32_t parkedHead = InvalidParkedNode;
                uint32_t parkedTail = InvalidParkedNode;
                uint32_t parkedCount = 0;
                bool ready = false;
            };

            struct ParkedNode
            {
                uint32_t next = InvalidParkedNode;
                ParkedContinuation continuation{};
            };

            std::mutex waitMutex;
            std::vector<WaitSlot> waitSlots;
            std::vector<uint32_t> freeWaitSlots;
            std::vector<ParkedNode> parkedNodes;
            std::vector<uint32_t> freeParkedNodes;
        };

        [[nodiscard]] static uint64_t EstimateLatencyPercentile(
            const std::array<uint64_t, SchedulerContext::LatencyBucketCount>& histogram,
            const uint64_t totalSamples,
            const double percentile)
        {
            if (totalSamples == 0)
                return 0;

            const auto targetRank = static_cast<uint64_t>(
                std::ceil(std::clamp(percentile, 0.0, 1.0) * static_cast<double>(totalSamples)));
            uint64_t prefix = 0;
            for (size_t i = 0; i < histogram.size(); ++i)
            {
                prefix += histogram[i];
                if (prefix >= targetRank)
                    return (i == 0) ? 0 : (1ull << (i - 1));
            }

            return 1ull << (histogram.size() - 2);
        }


        [[nodiscard]] inline size_t LatencyBucketIndex(const uint64_t latencyNs)
        {
            if (latencyNs == 0)
                return 0;

            const uint32_t msb = std::bit_width(latencyNs) - 1;
            return std::min<size_t>(msb + 1, SchedulerContext::LatencyBucketCount - 1);
        }

        static void RecordLatencySample(
            std::array<std::atomic<uint64_t>, SchedulerContext::LatencyBucketCount>& histogram,
            uint64_t latencyNs)
        {
            histogram[LatencyBucketIndex(latencyNs)].fetch_add(1, std::memory_order_relaxed);
        }
    }


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

    void LocalTask::operator()() const
    {
        if (m_VTable) m_VTable->Execute();
    }

    Job::Job(std::coroutine_handle<promise_type> handle) noexcept
        : m_Handle(handle), m_Alive(std::make_shared<std::atomic<bool>>(true))
    {
    }

    Job::Job(Job&& other) noexcept
        : m_Handle(other.m_Handle)
          , m_Alive(std::move(other.m_Alive))
    {
        other.m_Handle = {};
    }

    Job& Job::operator=(Job&& other) noexcept
    {
        if (this != &other)
        {
            DestroyIfOwned();
            m_Handle = other.m_Handle;
            m_Alive = std::move(other.m_Alive);
            other.m_Handle = {};
        }
        return *this;
    }

    Job::~Job()
    {
        // If the Job still owns the coroutine handle (was never dispatched),
        // destroy the frame and signal any pending reschedule tasks.
        DestroyIfOwned();
    }

    void Job::DestroyIfOwned()
    {
        if (m_Handle)
        {
            // Signal pending reschedule tasks that the coroutine frame is gone.
            if (m_Alive)
                m_Alive->store(false, std::memory_order_release);
            if (!m_Handle.done())
                m_Handle.destroy();
            m_Handle = {};
        }
    }

    static std::unique_ptr<Detail::SchedulerContext> s_Ctx;
    static thread_local int s_WorkerIndex = -1;

    void Scheduler::Initialize(unsigned threadCount)
    {
        if (s_Ctx) return;
        s_Ctx = std::make_unique<Detail::SchedulerContext>();
        s_Ctx->isRunning = true;

        if (threadCount == 0)
            threadCount = std::thread::hardware_concurrency();
        if (threadCount > 2) threadCount--;

        s_Ctx->workerStates.resize(threadCount);

        for (unsigned i = 0; i < threadCount; ++i)
        {
            s_Ctx->workers.emplace_back([i] { WorkerEntry(i); });
        }

        Log::Info("Initialized Scheduler with {} worker threads.", threadCount);
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

    void Scheduler::Dispatch(Job&& job)
    {
        if (!s_Ctx) return;
        if (!job.Valid()) return;

        auto handle = job.m_Handle;
        auto alive = job.m_Alive; // Share the lifetime token with the scheduler
        job.m_Handle = {};
        job.m_Alive = nullptr; // Transfer ownership to scheduler

        // Treat initial start just like any other continuation.
        Reschedule(handle, std::move(alive));
    }

    void Scheduler::Reschedule(std::coroutine_handle<> h, std::shared_ptr<std::atomic<bool>> alive)
    {
        if (!s_Ctx) return;
        if (!h) return;

        // Enqueue a task that runs exactly one coroutine slice.
        // The alive token (if present) allows detecting if the owning Job was
        // destroyed before this task executes, preventing use-after-free.
        DispatchInternal(LocalTask([h, alive = std::move(alive)]() mutable
        {
            // If the owning Job was destroyed, the coroutine frame is gone.
            if (alive && !alive->load(std::memory_order_acquire))
                return;

            h.resume();

            // With final_suspend = suspend_always, the coroutine frame is still valid here.
            if (h.done())
                h.destroy();
        }));
    }

    [[nodiscard]] static bool EnqueueInject(LocalTask&& task)
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


    [[nodiscard]] static bool TryPopInject(LocalTask& outTask)
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

    [[nodiscard]] static bool TryPopLocal(unsigned workerIndex, LocalTask& outTask)
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

    [[nodiscard]] static bool TrySteal(unsigned thiefIndex, LocalTask& outTask)
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

    void Scheduler::WorkerEntry(unsigned threadIndex)
    {
        static constexpr uint32_t FairnessInterval = 32;
        s_WorkerIndex = static_cast<int>(threadIndex);
        uint32_t lastSignal = s_Ctx->workSignal.load(std::memory_order_acquire);
        uint32_t localPopBudget = FairnessInterval;

        while (s_Ctx->isRunning)
        {
            LocalTask task;

            const bool shouldForceFairness = localPopBudget == 0;
            const bool canUseLocal = !shouldForceFairness;
            bool hasTask = false;

            if (canUseLocal && TryPopLocal(threadIndex, task))
            {
                hasTask = true;
                if (localPopBudget > 0)
                    localPopBudget -= 1;
            }
            else
            {
                if (TrySteal(threadIndex, task) || TryPopInject(task))
                {
                    hasTask = true;
                    localPopBudget = FairnessInterval;
                }
            }

            if (hasTask)
            {
                OnTaskDequeuedAndRun(task);
            }
            else
            {
                const auto idleWaitStart = std::chrono::steady_clock::now();
                s_Ctx->workSignal.wait(lastSignal);
                const auto idleWaitNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - idleWaitStart).count();
                s_Ctx->idleWaitCount.fetch_add(1, std::memory_order_relaxed);
                s_Ctx->idleWaitTotalNs.fetch_add(static_cast<uint64_t>(idleWaitNs), std::memory_order_relaxed);
                lastSignal = s_Ctx->workSignal.load(std::memory_order_acquire);
                localPopBudget = FairnessInterval;
            }
        }

        s_WorkerIndex = -1;
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
            (void)EnqueueInject(std::move(task));
        }

        s_Ctx->workSignal.fetch_add(1, std::memory_order_release);
        s_Ctx->workSignal.notify_one();
    }

    bool Scheduler::IsInitialized() noexcept
    {
        return s_Ctx != nullptr;
    }

    Scheduler::Stats Scheduler::GetStats()
    {
        Stats stats{};
        if (!s_Ctx)
            return stats;

        stats.InFlightTasks = s_Ctx->inFlightTasks.load(std::memory_order_acquire);
        stats.QueuedTasks = static_cast<uint64_t>(s_Ctx->queuedTaskCount.load(std::memory_order_relaxed));
        stats.ActiveTasks = static_cast<uint64_t>(s_Ctx->activeTaskCount.load(std::memory_order_relaxed));
        stats.InjectPushCount = s_Ctx->injectPushCount.load(std::memory_order_relaxed);
        stats.InjectPopCount = s_Ctx->injectPopCount.load(std::memory_order_relaxed);
        stats.LocalPopCount = s_Ctx->localPopCount.load(std::memory_order_relaxed);
        stats.StealPopCount = s_Ctx->stealPopCount.load(std::memory_order_relaxed);
        stats.TotalStealAttempts = s_Ctx->totalStealAttempts.load(std::memory_order_relaxed);
        stats.SuccessfulStealAttempts = s_Ctx->successfulStealAttempts.load(std::memory_order_relaxed);
        stats.ParkCount = s_Ctx->parkCount.load(std::memory_order_relaxed);
        stats.UnparkCount = s_Ctx->unparkCount.load(std::memory_order_relaxed);
        stats.ParkLatencyTotalNs = s_Ctx->parkLatencyTotalNs.load(std::memory_order_relaxed);
        stats.UnparkLatencyTotalNs = s_Ctx->unparkLatencyTotalNs.load(std::memory_order_relaxed);
        std::array<uint64_t, Detail::SchedulerContext::LatencyBucketCount> parkHistogram{};
        std::array<uint64_t, Detail::SchedulerContext::LatencyBucketCount> unparkHistogram{};
        for (size_t i = 0; i < Detail::SchedulerContext::LatencyBucketCount; ++i)
        {
            parkHistogram[i] = s_Ctx->parkLatencyHistogram[i].load(std::memory_order_relaxed);
            unparkHistogram[i] = s_Ctx->unparkLatencyHistogram[i].load(std::memory_order_relaxed);
        }
        stats.ParkLatencyP50Ns = Detail::EstimateLatencyPercentile(parkHistogram, stats.ParkCount, 0.50);
        stats.ParkLatencyP95Ns = Detail::EstimateLatencyPercentile(parkHistogram, stats.ParkCount, 0.95);
        stats.ParkLatencyP99Ns = Detail::EstimateLatencyPercentile(parkHistogram, stats.ParkCount, 0.99);
        stats.UnparkLatencyP50Ns = Detail::EstimateLatencyPercentile(unparkHistogram, stats.UnparkCount, 0.50);
        stats.UnparkLatencyP95Ns = Detail::EstimateLatencyPercentile(unparkHistogram, stats.UnparkCount, 0.95);
        stats.UnparkLatencyP99Ns = Detail::EstimateLatencyPercentile(unparkHistogram, stats.UnparkCount, 0.99);
        stats.UnparkLatencyTailSpreadNs =
            (stats.UnparkLatencyP99Ns >= stats.UnparkLatencyP50Ns)
                ? (stats.UnparkLatencyP99Ns - stats.UnparkLatencyP50Ns)
                : 0;
        stats.IdleWaitCount = s_Ctx->idleWaitCount.load(std::memory_order_relaxed);
        stats.IdleWaitTotalNs = s_Ctx->idleWaitTotalNs.load(std::memory_order_relaxed);
        stats.QueueContentionCount = s_Ctx->queueContentionCount.load(std::memory_order_relaxed);
        if (stats.TotalStealAttempts > 0)
        {
            stats.StealSuccessRatio =
                static_cast<double>(stats.SuccessfulStealAttempts) /
                static_cast<double>(stats.TotalStealAttempts);
        }

        stats.WorkerLocalDepths.reserve(s_Ctx->workerStates.size());
        stats.WorkerVictimStealCounts.reserve(s_Ctx->workerStates.size());
        for (auto& worker : s_Ctx->workerStates)
        {
            std::lock_guard lock(worker.localLock);
            stats.WorkerLocalDepths.push_back(static_cast<uint32_t>(worker.localDeque.size()));
            stats.WorkerVictimStealCounts.push_back(worker.stealCount.load(std::memory_order_relaxed));
        }
        return stats;
    }

    uint64_t Scheduler::GetParkCount() noexcept
    {
        if (!s_Ctx) return 0;
        return s_Ctx->parkCount.load(std::memory_order_acquire);
    }

    uint64_t Scheduler::GetUnparkCount() noexcept
    {
        if (!s_Ctx) return 0;
        return s_Ctx->unparkCount.load(std::memory_order_acquire);
    }

    const std::atomic<uint64_t>& Scheduler::ParkCountAtomic() noexcept
    {
        // Caller must ensure Scheduler is initialized (s_Ctx != nullptr).
        return s_Ctx->parkCount;
    }


    Scheduler::WaitToken Scheduler::AcquireWaitToken()
    {
        if (!s_Ctx)
            return {};

        std::lock_guard lock(s_Ctx->waitMutex);
        uint32_t slot = 0;
        if (!s_Ctx->freeWaitSlots.empty())
        {
            slot = s_Ctx->freeWaitSlots.back();
            s_Ctx->freeWaitSlots.pop_back();
        }
        else
        {
            slot = static_cast<uint32_t>(s_Ctx->waitSlots.size());
            s_Ctx->waitSlots.emplace_back();
        }

        auto& waitSlot = s_Ctx->waitSlots[slot];
        waitSlot.inUse = true;
        waitSlot.parkedHead = Detail::SchedulerContext::InvalidParkedNode;
        waitSlot.parkedTail = Detail::SchedulerContext::InvalidParkedNode;
        waitSlot.parkedCount = 0;
        waitSlot.ready = false;
        return WaitToken{slot, waitSlot.generation};
    }

    void Scheduler::ReleaseWaitToken(WaitToken token)
    {
        if (!s_Ctx || !token.Valid())
            return;

        std::lock_guard lock(s_Ctx->waitMutex);
        if (token.Slot >= s_Ctx->waitSlots.size())
            return;

        auto& slot = s_Ctx->waitSlots[token.Slot];
        if (!slot.inUse || slot.generation != token.Generation)
            return;

        slot.inUse = false;
        uint32_t node = slot.parkedHead;
        const uint32_t safetyLimit = slot.parkedCount + 1;
        uint32_t iterations = 0;
        while (node != Detail::SchedulerContext::InvalidParkedNode)
        {
            if (++iterations > safetyLimit) break;
            auto& parkedNode = s_Ctx->parkedNodes[node];
            const uint32_t next = parkedNode.next;
            parkedNode.next = Detail::SchedulerContext::InvalidParkedNode;
            parkedNode.continuation = {};
            s_Ctx->freeParkedNodes.push_back(node);
            node = next;
        }

        slot.parkedHead = Detail::SchedulerContext::InvalidParkedNode;
        slot.parkedTail = Detail::SchedulerContext::InvalidParkedNode;
        slot.parkedCount = 0;
        slot.ready = false;
        slot.generation++;
        if (slot.generation == 0)
            slot.generation = 1;
        s_Ctx->freeWaitSlots.push_back(token.Slot);
    }

    bool Scheduler::ParkCurrentFiberIfNotReady(WaitToken token, std::coroutine_handle<> h,
                                               std::shared_ptr<std::atomic<bool>> alive)
    {
        if (!s_Ctx || !token.Valid() || !h)
            return false;

        const auto parkStart = std::chrono::steady_clock::now();
        {
            std::lock_guard lock(s_Ctx->waitMutex);
            if (token.Slot >= s_Ctx->waitSlots.size())
                return false;
            auto& slot = s_Ctx->waitSlots[token.Slot];
            if (!slot.inUse || slot.generation != token.Generation)
                return false;

            // If already marked ready, do not park (prevents lost wakeups).
            if (slot.ready)
                return false;

            uint32_t parkedNodeIndex = Detail::SchedulerContext::InvalidParkedNode;
            if (!s_Ctx->freeParkedNodes.empty())
            {
                parkedNodeIndex = s_Ctx->freeParkedNodes.back();
                s_Ctx->freeParkedNodes.pop_back();
            }
            else
            {
                parkedNodeIndex = static_cast<uint32_t>(s_Ctx->parkedNodes.size());
                s_Ctx->parkedNodes.emplace_back();
            }

            auto& parkedNode = s_Ctx->parkedNodes[parkedNodeIndex];
            parkedNode.next = Detail::SchedulerContext::InvalidParkedNode;
            parkedNode.continuation = Detail::SchedulerContext::ParkedContinuation{
                .Handle = h,
                .Alive = std::move(alive),
                .ParkedAt = parkStart,
            };

            if (slot.parkedTail == Detail::SchedulerContext::InvalidParkedNode)
            {
                slot.parkedHead = parkedNodeIndex;
                slot.parkedTail = parkedNodeIndex;
            }
            else
            {
                s_Ctx->parkedNodes[slot.parkedTail].next = parkedNodeIndex;
                slot.parkedTail = parkedNodeIndex;
            }

            slot.parkedCount += 1;

            // Parking transfers ownership from the *current running task* to the wait slot.
            // It does NOT create a new in-flight task. The in-flight token that was acquired
            // when the coroutine slice was scheduled will be released when this slice returns
            // via OnTaskDequeuedAndRun().
            s_Ctx->parkCount.fetch_add(1, std::memory_order_relaxed);
            s_Ctx->parkCount.notify_all();
        }

        const auto parkNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - parkStart).count();
        s_Ctx->parkLatencyTotalNs.fetch_add(static_cast<uint64_t>(parkNs), std::memory_order_relaxed);
        Detail::RecordLatencySample(s_Ctx->parkLatencyHistogram, static_cast<uint64_t>(parkNs));
        return true;
    }

    uint32_t Scheduler::UnparkReady(WaitToken token)
    {
        if (!s_Ctx || !token.Valid())
            return 0;

        std::vector<Detail::SchedulerContext::ParkedContinuation> continuations;
        {
            std::lock_guard lock(s_Ctx->waitMutex);
            if (token.Slot >= s_Ctx->waitSlots.size())
                return 0;
            auto& slot = s_Ctx->waitSlots[token.Slot];
            if (!slot.inUse || slot.generation != token.Generation)
                return 0;

            // Mark ready first, so concurrent Park attempts will observe readiness and not park.
            slot.ready = true;

            if (slot.parkedHead == Detail::SchedulerContext::InvalidParkedNode)
                return 0;

            continuations.reserve(slot.parkedCount);
            uint32_t node = slot.parkedHead;
            const uint32_t safetyLimit = slot.parkedCount + 1;
            uint32_t iterations = 0;
            while (node != Detail::SchedulerContext::InvalidParkedNode)
            {
                if (++iterations > safetyLimit) break;
                auto& parkedNode = s_Ctx->parkedNodes[node];
                const uint32_t next = parkedNode.next;
                parkedNode.next = Detail::SchedulerContext::InvalidParkedNode;
                continuations.push_back(std::move(parkedNode.continuation));
                parkedNode.continuation = {};
                s_Ctx->freeParkedNodes.push_back(node);
                node = next;
            }

            slot.parkedHead = Detail::SchedulerContext::InvalidParkedNode;
            slot.parkedTail = Detail::SchedulerContext::InvalidParkedNode;
            slot.parkedCount = 0;
        }

        if (continuations.empty())
            return 0;

        const auto now = std::chrono::steady_clock::now();
        for (auto& continuation : continuations)
        {
            const auto unparkNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                now - continuation.ParkedAt).count();
            s_Ctx->unparkLatencyTotalNs.fetch_add(static_cast<uint64_t>(unparkNs), std::memory_order_relaxed);
            s_Ctx->unparkCount.fetch_add(1, std::memory_order_relaxed);
            Detail::RecordLatencySample(s_Ctx->unparkLatencyHistogram, static_cast<uint64_t>(unparkNs));

            Reschedule(continuation.Handle, std::move(continuation.Alive));
        }

        return static_cast<uint32_t>(continuations.size());
    }

    void Scheduler::MarkWaitTokenNotReady(WaitToken token)
    {
        if (!s_Ctx || !token.Valid())
            return;

        std::lock_guard lock(s_Ctx->waitMutex);
        if (token.Slot >= s_Ctx->waitSlots.size())
            return;

        auto& slot = s_Ctx->waitSlots[token.Slot];
        if (!slot.inUse || slot.generation != token.Generation)
            return;

        slot.ready = false;
    }

    void Scheduler::WaitForAll()
    {
        if (!s_Ctx) return;

        for (;;)
        {
            if (s_Ctx->inFlightTasks.load(std::memory_order_acquire) == 0)
                break;

            LocalTask task;
            if (TryPopTask(task, std::nullopt))
            {
                OnTaskDequeuedAndRun(task);
                continue;
            }

            const auto observed = s_Ctx->inFlightTasks.load(std::memory_order_acquire);
            if (observed > 0)
                s_Ctx->inFlightTasks.wait(observed, std::memory_order_relaxed);
            else
                Detail::CpuRelaxOrYield();
        }
    }

    // ---------------------------------------------------------------------
    // Coroutine support
    // ---------------------------------------------------------------------

    Job Job::promise_type::get_return_object() noexcept
    {
        return Job(std::coroutine_handle<Job::promise_type>::from_promise(*this));
    }

    void Job::promise_type::unhandled_exception() noexcept
    {
        // Exceptions are disabled in this engine. If something still throws in user code,
        // we treat it as fatal-in-debug. In release, log and continue.
        Log::Error("Core::Tasks::Job encountered an unhandled exception (exceptions disabled). Terminating coroutine.");
    }

    void YieldAwaiter::await_suspend(std::coroutine_handle<> handle) const noexcept
    {
        // YieldAwaiter does not carry an alive token; the coroutine is guaranteed
        // alive at the point of co_await since it's executing inside the frame.
        Scheduler::Reschedule(handle);
    }
}

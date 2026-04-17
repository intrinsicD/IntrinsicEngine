module;

#include <array>
#include <atomic>
#include <deque>
#include <vector>
#include <thread>
#include <coroutine>
#include <memory>
#include <optional>

export module Extrinsic.Core.Tasks:Internal;

import :LocalTask;
import Extrinsic.Core.LockFreeQueue;

export namespace Extrinsic::Core::Tasks
{
    namespace Detail
    {
        [[nodiscard]] bool CpuRelaxOnce() noexcept;
        void CpuRelaxOrYield() noexcept;

        struct SpinLock
        {
            std::atomic<bool> locked = false;

            [[nodiscard]] bool try_lock();
            void lock();
            void unlock();
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
                WorkerState(WorkerState&& other) noexcept;
                WorkerState& operator=(WorkerState&& other) noexcept;
            };

            std::vector<std::thread> workers;
            std::vector<WorkerState> workerStates;
            LockFreeQueue<LocalTask> globalQueue{65536};
            std::mutex overflowMutex;
            std::deque<LocalTask> overflowQueue;
            std::atomic<bool> hasOverflow{false};

            alignas(64) std::atomic<uint32_t> workSignal{0};
            alignas(64) std::atomic<bool> isRunning{false};
            alignas(64) std::atomic<uint64_t> inFlightTasks{0};
            alignas(64) std::atomic<int> activeTaskCount{0};
            alignas(64) std::atomic<int> queuedTaskCount{0};

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

        [[nodiscard]] uint64_t EstimateLatencyPercentile(
            const std::array<uint64_t, SchedulerContext::LatencyBucketCount>& histogram,
            uint64_t totalSamples,
            double percentile);

        [[nodiscard]] size_t LatencyBucketIndex(uint64_t latencyNs);
        void RecordLatencySample(
            std::array<std::atomic<uint64_t>, SchedulerContext::LatencyBucketCount>& histogram,
            uint64_t latencyNs);
    }

    bool EnqueueInject(LocalTask&& task);
    bool TryPopGlobalInject(LocalTask& outTask);
    bool TryPopLocal(unsigned workerIndex, LocalTask& outTask);
    bool TrySteal(unsigned thiefIndex, LocalTask& outTask);
    bool TryPopTask(LocalTask& outTask, std::optional<unsigned> workerIndex);
    void OnTaskDequeuedAndRun(LocalTask& task);
    // Global scheduler state declarations.
    // These are defined exactly once in Core.Tasks.State.cpp.
    extern std::unique_ptr<Detail::SchedulerContext> s_Ctx;
    extern thread_local int s_WorkerIndex;
}

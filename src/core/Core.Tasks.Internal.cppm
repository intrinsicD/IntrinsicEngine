module;

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <coroutine>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

export module Extrinsic.Core.Tasks:Internal;

import :LocalTask;
import Extrinsic.Core.LockFreeQueue;

export namespace Extrinsic::Core::Tasks
{
    namespace Detail
    {
        inline constexpr std::size_t PriorityLaneCount = 3u;
        inline constexpr std::size_t WaitShardCount = 16u;
        inline constexpr std::size_t HighPriorityInjectCapacity = 8'192u;
        inline constexpr std::size_t NormalPriorityInjectCapacity = 65'536u;
        inline constexpr std::size_t LowPriorityInjectCapacity = 8'192u;

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
            std::uint64_t instanceId = 0;
            struct alignas(64) WorkerState
            {
                SpinLock localLock{};
                std::array<std::deque<LocalTask>, PriorityLaneCount> localDeques{};
                std::atomic<uint64_t> stealCount{0};

                WorkerState() = default;
                WorkerState(const WorkerState&) = delete;
                WorkerState& operator=(const WorkerState&) = delete;
                WorkerState(WorkerState&& other) noexcept;
                WorkerState& operator=(WorkerState&& other) noexcept;
            };

            struct InjectLane
            {
                explicit InjectLane(const std::size_t capacity)
                    : Queue(capacity)
                {
                }

                LockFreeQueue<LocalTask> Queue;
                std::mutex OverflowMutex{};
                std::deque<LocalTask> OverflowQueue{};
                std::atomic<bool> HasOverflow{false};
            };

            std::vector<std::thread> workers;
            std::vector<WorkerState> workerStates;
            // Preserve the common Normal lane's previous capacity while
            // bounding the two preference lanes at 8K slots each. This is a
            // 25% aggregate increase, not the historical threefold copy.
            std::array<InjectLane, PriorityLaneCount> injectLanes{
                InjectLane{HighPriorityInjectCapacity},
                InjectLane{NormalPriorityInjectCapacity},
                InjectLane{LowPriorityInjectCapacity},
            };

            alignas(64) std::atomic<uint32_t> workSignal{0};
            alignas(64) std::atomic<uint32_t> parkedWorkerCount{0};
            alignas(64) std::atomic<bool> isRunning{false};
            alignas(64) std::atomic<uint64_t> inFlightTasks{0};
            alignas(64) std::atomic<int> activeTaskCount{0};
            alignas(64) std::atomic<int> queuedTaskCount{0};
            alignas(64) std::array<std::atomic<int>, PriorityLaneCount>
                queuedTaskCountByLane{};
            alignas(64) std::atomic<uint64_t> workProgressEpoch{0};
            alignas(64) std::atomic<uint32_t> externalProgressWaiters{0};

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
            alignas(64) std::atomic<uint64_t> workerWakeNotificationCount{0};

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

            struct WaitShard
            {
                std::mutex Mutex{};
                std::vector<WaitSlot> Slots{};
                std::vector<uint32_t> FreeSlots{};
                std::vector<ParkedNode> ParkedNodes{};
                std::vector<uint32_t> FreeParkedNodes{};
            };

            std::array<WaitShard, WaitShardCount> waitShards{};
            std::atomic<uint32_t> nextWaitShard{0u};
        };

        [[nodiscard]] uint64_t EstimateLatencyPercentile(
            const std::array<uint64_t, SchedulerContext::LatencyBucketCount>& histogram,
            uint64_t totalSamples,
            double percentile);

        [[nodiscard]] size_t LatencyBucketIndex(uint64_t latencyNs);
        void RecordLatencySample(
            std::array<std::atomic<uint64_t>, SchedulerContext::LatencyBucketCount>& histogram,
            uint64_t latencyNs);

        // Caller must hold the owning WaitShard::Mutex while transferring the
        // slot's single-use continuation tokens out of the wait registry.
        [[nodiscard]] std::vector<SchedulerContext::ParkedContinuation>
        TakeParkedContinuationsLocked(SchedulerContext::WaitShard& shard,
                                      SchedulerContext::WaitSlot& slot);
        void DestroyParkedContinuations(
            std::vector<SchedulerContext::ParkedContinuation>& continuations) noexcept;
    }

    bool EnqueueInject(LocalTask&& task, std::uint8_t lane);
    bool TryPopGlobalInject(LocalTask& outTask, std::uint8_t lane);
    bool TryPopLocal(unsigned workerIndex, LocalTask& outTask, std::uint8_t lane);
    bool TrySteal(unsigned thiefIndex, LocalTask& outTask, std::uint8_t lane);
    bool TryStealExternal(LocalTask& outTask, std::uint8_t lane);
    bool TryPopWorkerTask(LocalTask& outTask,
                          unsigned workerIndex,
                          std::uint8_t& poppedLane,
                          bool allowLocal,
                          bool& poppedLocal);
    bool TryPopDefinitiveTask(LocalTask& outTask,
                              std::optional<unsigned> workerIndex,
                              std::uint8_t& poppedLane);
    void PublishWorkProgress() noexcept;
    void OnTaskDequeuedAndRun(LocalTask& task, std::uint8_t lane);
    // Global scheduler state declarations.
    // These are defined exactly once in Core.Tasks.State.cpp.
    extern std::unique_ptr<Detail::SchedulerContext> s_Ctx;
    extern thread_local int s_WorkerIndex;
}

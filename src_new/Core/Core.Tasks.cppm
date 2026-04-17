module;

#include <vector>
#include <memory>
#include <coroutine>
#include <atomic>

export module Extrinsic.Core.Tasks;

import :LocalTask;

namespace Extrinsic::Core::Tasks
{
export class Job;

    export class Scheduler
    {
    public:
        struct WaitToken
        {
            std::uint32_t Slot = 0;
            std::uint32_t Generation = 0;

            [[nodiscard]] bool Valid() const { return Generation != 0; }
        };

        struct Stats
        {
            std::uint64_t InFlightTasks = 0;
            std::uint64_t QueuedTasks = 0;
            std::uint64_t ActiveTasks = 0;
            std::uint64_t InjectPushCount = 0;
            std::uint64_t InjectPopCount = 0;
            std::uint64_t LocalPopCount = 0;
            std::uint64_t StealPopCount = 0;
            std::uint64_t TotalStealAttempts = 0;
            std::uint64_t SuccessfulStealAttempts = 0;
            std::uint64_t ParkCount = 0;
            std::uint64_t UnparkCount = 0;
            std::uint64_t ParkLatencyTotalNs = 0;
            std::uint64_t UnparkLatencyTotalNs = 0;
            std::uint64_t ParkLatencyP50Ns = 0;
            std::uint64_t ParkLatencyP95Ns = 0;
            std::uint64_t ParkLatencyP99Ns = 0;
            std::uint64_t UnparkLatencyP50Ns = 0;
            std::uint64_t UnparkLatencyP95Ns = 0;
            std::uint64_t UnparkLatencyP99Ns = 0;
            std::uint64_t UnparkLatencyTailSpreadNs = 0;
            std::uint64_t IdleWaitCount = 0;
            std::uint64_t IdleWaitTotalNs = 0;
            std::uint64_t QueueContentionCount = 0;
            double StealSuccessRatio = 0.0;
            std::vector<std::uint32_t> WorkerLocalDepths{};
            std::vector<std::uint64_t> WorkerVictimStealCounts{};
        };

        static void Initialize(unsigned threadCount = 0);
        static void Shutdown();

        template <typename F>
        static void Dispatch(F&& task) { DispatchInternal(LocalTask(std::forward<F>(task))); }

        static void Dispatch(Job&& job);
        static void Reschedule(std::coroutine_handle<> h, std::shared_ptr<std::atomic<bool>> alive = nullptr);

        [[nodiscard]] static bool IsInitialized() noexcept;
        [[nodiscard]] static Stats GetStats();
        [[nodiscard]] static std::uint64_t GetParkCount() noexcept;
        [[nodiscard]] static std::uint64_t GetUnparkCount() noexcept;
        [[nodiscard]] static const std::atomic<std::uint64_t>& ParkCountAtomic() noexcept;
        [[nodiscard]] static WaitToken AcquireWaitToken();
        static void ReleaseWaitToken(WaitToken token);
        static bool ParkCurrentFiberIfNotReady(WaitToken token, std::coroutine_handle<> h,
                                               std::shared_ptr<std::atomic<bool>> alive = nullptr);
        static std::uint32_t UnparkReady(WaitToken token);
        static void MarkWaitTokenNotReady(WaitToken token);
        static void WaitForAll();

    private:
        static void WorkerEntry(unsigned threadIndex);
        static void DispatchInternal(LocalTask&& task);
    };

    export class Job
    {
    public:
        struct promise_type
        {
            Job get_return_object() noexcept;
            std::suspend_always initial_suspend() noexcept { return {}; }
            // Keep the frame alive so the scheduler can destroy it after observing done().
            std::suspend_always final_suspend() noexcept { return {}; }

            void return_void() noexcept
            {
            }

            void unhandled_exception() noexcept;
        };

        Job() = default;
        explicit Job(std::coroutine_handle<promise_type> handle) noexcept;
        Job(Job&& other) noexcept;
        Job& operator=(Job&& other) noexcept;
        Job(const Job&) = delete;
        Job& operator=(const Job&) = delete;
        ~Job();

        [[nodiscard]] bool Valid() const noexcept { return m_Handle != nullptr; }

    private:
        void DestroyIfOwned();

        std::coroutine_handle<promise_type> m_Handle{};
        // Shared cancellation token: allows pending reschedule tasks to detect
        // that the owning Job was destroyed and the coroutine frame is gone.
        std::shared_ptr<std::atomic<bool>> m_Alive{};
        friend class Scheduler;
    };
}

module;

#include <atomic>
#include <coroutine>
#include <memory>
#include <vector>

export module Extrinsic.Core.Tasks;

namespace Extrinsic::Core::Tasks
{
    export class Job;

    export class LocalTask
    {
        static constexpr size_t STORAGE_SIZE = 120; // 128 total - sizeof(Concept*)

        struct Concept
        {
            virtual ~Concept() = default;
            virtual void Execute() = 0;
            virtual void MoveTo(void* dest) = 0;
        };

        template <typename T>
        struct Model final : Concept
        {
            T payload;

            explicit Model(T&& p) : payload(std::move(p))
            {
            }

            void Execute() override { payload(); }

            void MoveTo(void* dest) override
            {
                std::construct_at(static_cast<Model*>(dest), std::move(payload));
            }
        };

        alignas(std::max_align_t) std::byte m_Storage[STORAGE_SIZE]{}; // Value-initialize to zero
        Concept* m_VTable = nullptr; // Points to m_Storage (reinterpreted)

    public:
        LocalTask() = default;

        // Constructor for lambdas
        template <typename F> requires (!std::is_same_v<std::decay_t<F>, LocalTask>)
        LocalTask(F&& f)
        {
            using Type = std::decay_t<F>;
            static_assert(sizeof(Model<Type>) <= STORAGE_SIZE,
                          "Task lambda capture is too big! Use pointers or simplify captures.");
            static_assert(alignof(Model<Type>) <= alignof(std::max_align_t),
                          "Task alignment requirement too strict.");

            auto* ptr = reinterpret_cast<Model<Type>*>(m_Storage);
            std::construct_at(ptr, std::forward<F>(f));
            m_VTable = ptr;
        }

        ~LocalTask();

        // Move Constructor
        LocalTask(LocalTask&& other) noexcept;

        // Move Assignment
        LocalTask& operator=(LocalTask&& other) noexcept;

        // No copy
        LocalTask(const LocalTask&) = delete;
        LocalTask& operator=(const LocalTask&) = delete;

        void operator()() const;

        [[nodiscard]] bool Valid() const { return m_VTable != nullptr; }
    };

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


    export class CounterEvent
    {
    public:
        explicit CounterEvent(uint32_t initialCount = 0);
        ~CounterEvent();

        void Add(uint32_t value = 1);
        void Signal(uint32_t value = 1);
        [[nodiscard]] bool IsReady() const { return m_Count.load(std::memory_order_acquire) == 0; }
        [[nodiscard]] Scheduler::WaitToken Token() const { return m_Token; }

    private:
        std::atomic<uint32_t> m_Count{0};
        Scheduler::WaitToken m_Token{};
    };

    export struct WaitCounterAwaiter
    {
        CounterEvent& Counter;

        [[nodiscard]] bool await_ready() const noexcept { return Counter.IsReady(); }

        [[nodiscard]] bool await_suspend(std::coroutine_handle<> h) const noexcept
        {
            // Avoid lost wakeups: if the counter reached zero and signaled between await_ready()
            // and here, we must not park. Returning false means: do not suspend.
            return Scheduler::ParkCurrentFiberIfNotReady(Counter.Token(), h);
        }

        void await_resume() const noexcept
        {
        }
    };

    export [[nodiscard]] inline WaitCounterAwaiter WaitFor(CounterEvent& counter) noexcept
    {
        return WaitCounterAwaiter{counter};
    }

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

    struct YieldAwaiter
    {
        bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<>) const noexcept;

        void await_resume() const noexcept
        {
        }
    };

    // Cooperative yield: reschedules the current coroutine back onto the Scheduler.
    export [[nodiscard]] inline YieldAwaiter Yield() noexcept { return {}; }
}

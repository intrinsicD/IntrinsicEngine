module;

#include <functional>
#include <type_traits>
#include <utility>
#include <memory>
#include <coroutine>

export module Core:Tasks;
import :Logging;

namespace Core::Tasks
{
    // Forward declaration for Scheduler overload.
    export class Job;

    // A fixed-size, non-allocating task wrapper.
    // Sized for two cache lines (128 bytes total) to accommodate lambdas with moderate captures.
    // Layout: 120 bytes storage + 8 bytes vtable pointer = 128 bytes.
    // Trade-off: Larger tasks reduce cache efficiency but allow more flexible captures.
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
                std::construct_at(static_cast<Model<T>*>(dest), std::move(payload));
            }
        };

        alignas(std::max_align_t) std::byte m_Storage[STORAGE_SIZE]{}; // Value-initialize to zero
        Concept* m_VTable = nullptr; // Points to m_Storage (reinterpreted)

    public:
        LocalTask() = default;

        // Constructor for lambdas
        template <typename F>
            requires (!std::is_same_v<std::decay_t<F>, LocalTask>)
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

        void operator()();

        [[nodiscard]] bool Valid() const { return m_VTable != nullptr; }
    };

    export class Scheduler
    {
    public:
        static void Initialize(unsigned threadCount = 0);
        static void Shutdown();

        template <typename F>
        static void Dispatch(F&& task)
        {
            DispatchInternal(LocalTask(std::forward<F>(task)));
        }

        // Coroutine job support (fire-and-forget).
        // The coroutine is resumed on the scheduler until it reaches final suspend.
        static void Dispatch(Job&& job);

        // Used by coroutine awaiters to enqueue a continuation back onto the scheduler.
        static void Reschedule(std::coroutine_handle<> h);

        static void WaitForAll();

    private:
        static void DispatchInternal(LocalTask&& task);
        static void WorkerEntry(unsigned threadIndex);
    };

    // ---------------------------------------------------------------------
    // Coroutine Job (MVP)
    // ---------------------------------------------------------------------

    // Fire-and-forget coroutine driven by Scheduler.
    // It is intentionally minimal: no return value, no exception propagation.
    export class Job
    {
    public:
        struct promise_type
        {
            Job get_return_object() noexcept;
            std::suspend_always initial_suspend() noexcept { return {}; }
            // Keep the frame alive so the scheduler can destroy it after observing done().
            std::suspend_always final_suspend() noexcept { return {}; }
            void return_void() noexcept {}
            void unhandled_exception() noexcept;
        };

        Job() = default;
        explicit Job(std::coroutine_handle<promise_type> h) noexcept : m_Handle(h) {}

        Job(Job&& other) noexcept : m_Handle(other.m_Handle) { other.m_Handle = {}; }
        Job& operator=(Job&& other) noexcept
        {
            if (this != &other)
            {
                // We intentionally don't destroy here; ownership is transferred.
                m_Handle = other.m_Handle;
                other.m_Handle = {};
            }
            return *this;
        }

        Job(const Job&) = delete;
        Job& operator=(const Job&) = delete;

        ~Job() = default; // Lifetime is owned by the scheduler once dispatched.

        [[nodiscard]] bool Valid() const noexcept { return m_Handle != nullptr; }

    private:
        std::coroutine_handle<promise_type> m_Handle{};
        friend class Scheduler;
    };

    struct YieldAwaiter
    {
        bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<>) const noexcept;
        void await_resume() const noexcept {}
    };

    // Cooperative yield: reschedules the current coroutine back onto the Scheduler.
    export [[nodiscard]] inline YieldAwaiter Yield() noexcept { return {}; }
}

module;

#include <coroutine>
#include <memory>

export module Extrinsic.Core.Tasks.CounterEvent;

import Extrinsic.Core.Tasks;

namespace Extrinsic::Core::Tasks
{
    export class CounterEvent
    {
    public:
        explicit CounterEvent(uint32_t initialCount = 0);
        ~CounterEvent();
        CounterEvent(const CounterEvent&) = delete;
        CounterEvent& operator=(const CounterEvent&) = delete;
        CounterEvent(CounterEvent&&) = delete;
        CounterEvent& operator=(CounterEvent&&) = delete;

        void Add(uint32_t value = 1);
        void Signal(uint32_t value = 1);
        [[nodiscard]] bool IsReady() const;
        [[nodiscard]] Scheduler::WaitToken Token() const;

        // Park the calling thread until Signal() changes the pending count.
        // Returns immediately when already ready. Callers re-check their work
        // queues and IsReady() after each progress wake.
        void WaitForProgress() const;

    private:
        struct State;
        std::shared_ptr<State> m_State{};
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

    struct YieldAwaiter
    {
        bool await_ready() const noexcept { return false; }

        void await_suspend(std::coroutine_handle<> handle) const noexcept
        {
            Scheduler::Reschedule(handle);
        }

        void await_resume() const noexcept
        {
        }
    };

    // Cooperative yield: reschedules the current coroutine back onto the Scheduler.
    export [[nodiscard]] inline YieldAwaiter Yield() noexcept { return {}; }
}

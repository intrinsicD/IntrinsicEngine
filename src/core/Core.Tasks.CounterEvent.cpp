module;

#include <atomic>
#include <memory>

module Extrinsic.Core.Tasks.CounterEvent;

namespace Extrinsic::Core::Tasks
{
    struct CounterEvent::State
    {
        std::atomic<uint32_t> Count{0};
        Scheduler::WaitToken Token{};

        explicit State(const uint32_t initialCount)
            : Count(initialCount)
            , Token(Scheduler::AcquireWaitToken())
        {
        }

        ~State()
        {
            Scheduler::ReleaseWaitToken(Token);
        }
    };

    CounterEvent::CounterEvent(uint32_t initialCount)
        : m_State(std::make_shared<State>(initialCount))
    {
    }

    CounterEvent::~CounterEvent() = default;

    bool CounterEvent::IsReady() const
    {
        return PendingCount() == 0u;
    }

    uint32_t CounterEvent::PendingCount() const
    {
        const auto state = m_State;
        return state->Count.load(std::memory_order_acquire);
    }

    Scheduler::WaitToken CounterEvent::Token() const
    {
        return m_State->Token;
    }

    void CounterEvent::WaitForProgress(const uint32_t observedCount) const
    {
        const auto state = m_State;
        if (observedCount != 0u)
            state->Count.wait(observedCount, std::memory_order_acquire);
    }

    void CounterEvent::Add(uint32_t value)
    {
        if (value == 0)
            return;

        const auto state = m_State;
        const uint32_t previous = state->Count.fetch_add(value, std::memory_order_acq_rel);
        if (previous == 0)
            Scheduler::MarkWaitTokenNotReady(state->Token);
    }

    void CounterEvent::Signal(uint32_t value)
    {
        if (value == 0)
            return;

        // Keep the counter/token state alive through notification even when a
        // waiter releases the public CounterEvent immediately after readiness.
        const auto state = m_State;
        const Scheduler::WaitToken token = state->Token;
        uint32_t observed = state->Count.load(std::memory_order_acquire);
        while (observed != 0)
        {
            const uint32_t next = (observed <= value) ? 0u : (observed - value);
            if (state->Count.compare_exchange_weak(observed, next,
                                                   std::memory_order_acq_rel,
                                                   std::memory_order_acquire))
            {
                state->Count.notify_all();
                if (next == 0)
                    Scheduler::UnparkReady(token);
                return;
            }
        }
    }
}

module;

#include <atomic>

module Extrinsic.Core.Tasks.CounterEvent;

namespace Extrinsic::Core::Tasks
{

    CounterEvent::CounterEvent(uint32_t initialCount)
        : m_Count(initialCount)
        , m_Token(Scheduler::AcquireWaitToken())
    {
    }

    CounterEvent::~CounterEvent()
    {
        Scheduler::ReleaseWaitToken(m_Token);
    }

    void CounterEvent::Add(uint32_t value)
    {
        if (value == 0)
            return;

        const uint32_t previous = m_Count.fetch_add(value, std::memory_order_acq_rel);
        if (previous == 0)
            Scheduler::MarkWaitTokenNotReady(m_Token);
    }

    void CounterEvent::Signal(uint32_t value)
    {
        if (value == 0)
            return;

        uint32_t observed = m_Count.load(std::memory_order_acquire);
        while (observed != 0)
        {
            const uint32_t next = (observed <= value) ? 0u : (observed - value);
            if (m_Count.compare_exchange_weak(observed, next,
                                              std::memory_order_acq_rel,
                                              std::memory_order_acquire))
            {
                if (next == 0)
                    Scheduler::UnparkReady(m_Token);
                return;
            }
        }
    }
}
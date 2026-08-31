module;

#include <chrono>

module Core.Timer;

namespace Extrinsic::Core
{
    Timer::Timer() noexcept
        : m_StartTime{Clock::now()}
    {
    }

    void Timer::Restart() noexcept
    {
        m_StartTime = Clock::now();
    }

    double Timer::ElapsedSeconds() const noexcept
    {
        return std::chrono::duration<double>(
            Clock::now() - m_StartTime
        ).count();
    }
}

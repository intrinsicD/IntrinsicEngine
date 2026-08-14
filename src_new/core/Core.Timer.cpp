module;

module Core.Timer;

#include <chrono>

import Core.Timer;

namespace Extrinsic::Core
{
    void Timer::Start()
    {
        m_StartTime = std::chrono::high_resolution_clock::now();
        m_Running = true;
    }

    void Timer::Stop()
    {
        m_EndTime = std::chrono::high_resolution_clock::now();
        m_Running = false;
    }

    double Timer::ElapsedSeconds() const
    {
        if (m_Running)
        {
            auto now = std::chrono::high_resolution_clock::now();
            return std::chrono::duration<double>(now - m_StartTime).count();
        }
        else
        {
            return std::chrono::duration<double>(m_EndTime - m_StartTime).count();
        }
    }
}
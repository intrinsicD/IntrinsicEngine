module;

#include <chrono>

export module Core.Timer;

namespace Extrinsic::Core
{
    export class Timer
    {
    public:
        Timer() noexcept;

        void Restart() noexcept;

        [[nodiscard]] double ElapsedSeconds() const noexcept;

    private:
        using Clock = std::chrono::steady_clock;

        Clock::time_point m_StartTime;
    };
}

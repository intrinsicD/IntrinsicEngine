module;

#include <chrono>

export module Core.Timer;

namespace Extrinsic::Core
{
    export class Timer
    {
    public:
        Timer() = default;
        ~Timer() = default;

        Timer(const Timer&) = delete;
        Timer& operator=(const Timer&) = delete;

        void Start();
        void Stop();
        double ElapsedSeconds() const;

    private:
        std::chrono::high_resolution_clock::time_point m_StartTime;
        std::chrono::high_resolution_clock::time_point m_EndTime;
        bool m_Running{false};
    };
}
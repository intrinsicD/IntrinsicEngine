module;

#include <chrono>
#include <algorithm>

export module Extrinsic.Core.FrameClock;

// ============================================================
// FrameClock — per-frame wall-clock manager.
//
// Responsibilities:
//   - Measure raw frame delta with std::chrono::steady_clock.
//   - Clamp the delta so a debugger pause / OS stall cannot
//     inject an enormous timestep into the simulation.
//   - Resample after deliberate sleeps (minimized window, idle
//     throttle) so sleep time does not count as frame time.
//
// Usage pattern (one instance owned by a frame-loop coordinator):
//
//   clock.BeginFrame();
//   const double delta = clock.FrameDeltaClamped(); // previous completed frame
//   // ... optional deliberate sleep ...
//   clock.Resample();        // call only after a sleep, not every frame
//   // ... simulation, render ...
//   clock.EndFrame();
// ============================================================

namespace Extrinsic::Core
{
    export class FrameClock
    {
    public:
        using Clock    = std::chrono::steady_clock;
        using TimePoint = Clock::time_point;

        /// Mark the start of a new frame.
        void BeginFrame() noexcept
        {
            m_FrameStart = Clock::now();
        }

        /// Re-anchor the clock after a deliberate sleep so the next
        /// FrameDeltaClamped() does not include the sleep duration.
        /// Call at most once between BeginFrame and EndFrame.
        void Resample() noexcept
        {
            m_FrameStart = Clock::now();
        }

        /// Mark the end of a frame and record its raw duration for the next
        /// frame's time consumers and for telemetry.
        void EndFrame() noexcept
        {
            const auto now = Clock::now();
            const std::chrono::duration<double> raw = now - m_FrameStart;
            m_LastRawDelta = raw.count();
        }

        /// Previous completed-frame delta clamped to
        /// [0, max(0, maxDeltaSeconds)]. Use this to drive the fixed-step
        /// simulation accumulator and other frame-time consumers.
        /// The clamp prevents a single huge frame from making the
        /// simulation spiral out of control.
        [[nodiscard]] double FrameDeltaClamped(double maxDeltaSeconds = 0.25) const noexcept
        {
            const double upperBound = std::max(0.0, maxDeltaSeconds);
            return std::clamp(m_LastRawDelta, 0.0, upperBound);
        }

        /// Raw (unclamped) delta of the previous completed frame.
        /// FrameDeltaClamped() derives the simulation/UI delta from this same
        /// completed-frame record.
        [[nodiscard]] double LastRawDelta() const noexcept { return m_LastRawDelta; }

    private:
        TimePoint m_FrameStart{Clock::now()};
        double    m_LastRawDelta{0.0};
    };
}

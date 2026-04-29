module;

#include <chrono>
#include <algorithm>

export module Extrinsic.Runtime.FrameClock;

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
// Usage pattern (one instance owned by Engine::Run):
//
//   clock.BeginFrame();
//   // ... optional deliberate sleep ...
//   clock.Resample();        // call only after a sleep, not every frame
//   // ... simulation, render ...
//   clock.EndFrame();
// ============================================================

namespace Extrinsic::Runtime
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

        /// Mark the end of a frame and record the raw delta for telemetry.
        void EndFrame() noexcept
        {
            const auto now = Clock::now();
            const std::chrono::duration<double> raw = now - m_FrameStart;
            m_LastRawDelta = raw.count();
        }

        /// Delta clamped to [0, maxDeltaSeconds].
        /// Use this to drive the fixed-step simulation accumulator.
        /// The clamp prevents a single huge frame from making the
        /// simulation spiral out of control.
        [[nodiscard]] double FrameDeltaClamped(double maxDeltaSeconds = 0.25) const noexcept
        {
            const auto now = Clock::now();
            const std::chrono::duration<double> raw = now - m_FrameStart;
            return std::min(raw.count(), maxDeltaSeconds);
        }

        /// Raw (unclamped) delta of the previous completed frame.
        /// Use for telemetry and display — not for simulation.
        [[nodiscard]] double LastRawDelta() const noexcept { return m_LastRawDelta; }

    private:
        TimePoint m_FrameStart{Clock::now()};
        double    m_LastRawDelta{0.0};
    };
}


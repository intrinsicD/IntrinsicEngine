// ARCH-023 consumer no-frame probe for one worker step run of a Vulkan method (LOP/WLOP, hybrid consolidation,
// farthest-point sampling, smoothing, a one-shot kernel, CPD). Driven from the app's Frame:
//   StepFirst (iterative runs): Step(1) without a hold (the owner's Init and its owner pages may need frames), then
//     the method queue is held and the run told Run; one-shots (HoldFirst): the method queue is held first, then Run
//     (their Init submits without waiting).
//   Once the run's worker segment is in flight (its page queued behind the held method queue), ProgressWithoutFrames
//   (HeldFramesProgress.hpp, shared with the query consumers' smokes) gates graphics, releases the method queue and
//   waits, with no frame, maintenance, CollectCompleted or owner callback, until `Want` more logical steps completed.
//   A run whose worker segment never comes (an owner-driven run) ends the probe after SegmentLimit, with nothing
//   progressed and the method queue released: the hold is a device-side gate, so a device shutdown behind it would
//   wait forever.
// Outcome: SeparateQueue false (shared hardware: nothing could be held, reported as such), else Gated and Progressed
// with StepsWithoutFrames. Include after HeldFramesProgress.hpp and the Extrinsic.Runtime.StepRuns import.
#pragma once

namespace Intrinsic::Tests
{
    struct HeldStepRunProbe
    {
        enum class Mode : std::uint8_t { StepFirst, HoldFirst };
        Mode Order{Mode::StepFirst};
        std::uint32_t Want{3u};
        std::uint64_t Run{};
        // The longest wait, with the method queue held, for the run's worker segment to be in flight.
        std::chrono::steady_clock::duration SegmentLimit{std::chrono::seconds{10}};
        // Outcome.
        bool SeparateQueue{}, Gated{}, Progressed{}, Done{};
        std::uint32_t StepsBefore{}, StepsWithoutFrames{};

        HeldStepRunProbe() = default;
        HeldStepRunProbe(const HeldStepRunProbe&) = delete;
        HeldStepRunProbe& operator=(const HeldStepRunProbe&) = delete;
        ~HeldStepRunProbe()
        {
            ReleaseHold();
        }
        // Main thread: releases a method-queue hold the probe still keeps (an app that timed out calls it before it
        // exits, so the device can go idle at shutdown).
        void ReleaseHold()
        {
            if (m_MethodRelease)
                (void)std::exchange(m_MethodRelease, {})();
        }
        // Once per frame (main thread) with Run set; true once the probe finished.
        bool Frame(Extrinsic::RHI::IDevice& device, Extrinsic::Runtime::StepRunsService& steps)
        {
            namespace Vk = Extrinsic::Backends::Vulkan;
            namespace R = Extrinsic::Runtime;
            if (Done || Run == 0u)
                return Done;
            const auto run = steps.Observe(Run);
            if (!run)
                return false;
            std::string refusal;
            switch (m_Stage)
            {
            case Stage::Ready:
                if (run->SegmentInFlight || run->Phase != R::StepRunPhase::Ready)
                    return false;
                if (Order == Mode::StepFirst)
                {
                    (void)steps.Control(Run, R::StepRunAction::Step, 1u, refusal);
                    m_Stage = Stage::FirstStep;
                    return false;
                }
                Hold(device, steps);
                return false;
            case Stage::FirstStep:
                if (run->SegmentInFlight || run->Steps < 1u || run->Phase != R::StepRunPhase::Paused)
                    return false;
                Hold(device, steps);
                return false;
            case Stage::Held:
                if (!SeparateQueue)
                {
                    Done = true; // nothing to gate on shared hardware: the caller reports it, claims nothing
                    return true;
                }
                if (!run->WorkerInFlight)
                {
                    if (std::chrono::steady_clock::now() - m_HeldAt < SegmentLimit)
                        return false; // the segment is not submitted yet
                    ReleaseHold(); // no worker segment came: nothing progressed, and the queue is not left held
                    Done = true;
                    return true;
                }
                {
                    const auto outcome = ProgressWithoutFrames(device,
                        m_MethodRelease,
                        [&]
                        {
                            const auto now = steps.Observe(Run);
                            return now && now->Steps >= StepsBefore + Want;
                        });
                    Gated = outcome.Gated;
                    Progressed = outcome.Progressed;
                    const auto after = steps.Observe(Run);
                    StepsWithoutFrames = after ? after->Steps - StepsBefore : 0u;
                }
                Done = true;
                return true;
            }
            return false;
        }

    private:
        enum class Stage : std::uint8_t { Ready, FirstStep, Held };
        void Hold(Extrinsic::RHI::IDevice& device, Extrinsic::Runtime::StepRunsService& steps)
        {
            namespace Vk = Extrinsic::Backends::Vulkan;
            m_MethodRelease = Vk::HoldVulkanQueue(&device, Vk::VulkanGatedQueue::MethodCompute);
            SeparateQueue = bool(m_MethodRelease);
            m_HeldAt = std::chrono::steady_clock::now();
            StepsBefore = steps.Observe(Run) ? steps.Observe(Run)->Steps : 0u;
            std::string refusal;
            (void)steps.Control(Run, Extrinsic::Runtime::StepRunAction::Run, 1u, refusal);
            m_Stage = Stage::Held;
        }
        Stage m_Stage{Stage::Ready};
        std::function<bool()> m_MethodRelease{};
        std::chrono::steady_clock::time_point m_HeldAt{};
    };
}

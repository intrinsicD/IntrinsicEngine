// ARCH-023 consumer no-frame progress (shared by the ICP, bilateral, descriptor and keypoint Vulkan smokes): the owner
// thread itself waits inside a graphics-queue gate, so no frame, maintenance, CollectCompleted or owner callback runs
// while the run's worker steps or pages. The caller holds the method queue first (HoldVulkanQueue MethodCompute) and
// waits until its run's worker segment is in flight; the hold is released here once graphics is gated, so every page
// the measurement counts ran with graphics held and no frames.
// Include after importing Extrinsic.Backends.Vulkan and Extrinsic.RHI.Device; needs <chrono>, <functional>, <thread>.
#pragma once

namespace Intrinsic::Tests
{
    struct HeldFramesOutcome
    {
        bool Gated{false};      // the graphics queue was held (false: the device has no such gate)
        bool Progressed{false}; // `progressed` held before the deadline, with graphics held and no frames
    };

    inline HeldFramesOutcome ProgressWithoutFrames(Extrinsic::RHI::IDevice& device,
        std::function<bool()>& methodRelease,
        const std::function<bool()>& progressed,
        const std::chrono::steady_clock::duration limit = std::chrono::seconds{20})
    {
        namespace Vk = Extrinsic::Backends::Vulkan;
        HeldFramesOutcome outcome{};
        const auto deadline = std::chrono::steady_clock::now() + limit;
        outcome.Gated = Vk::WithVulkanQueueGate(&device,
            Vk::VulkanGatedQueue::Graphics,
            [&]
            {
                if (methodRelease)
                    (void)std::exchange(methodRelease, {})();
                while (std::chrono::steady_clock::now() < deadline)
                {
                    if (progressed())
                    {
                        outcome.Progressed = true;
                        return;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds{1});
                }
            });
        if (methodRelease) // never leave the method queue held (the gate was refused)
            (void)std::exchange(methodRelease, {})();
        return outcome;
    }
}

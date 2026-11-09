// Shared worker wait for a submitted compute page: cancellation between bounded waits, with an optional deadline.
// Include after RHI.TransferQueue; the including unit supplies <chrono> in its global fragment.
#pragma once

extern "C++"
{
namespace Extrinsic::Runtime
{
    enum class ComputePageWait { Complete, Abandoned, Failed, TimedOut };

    template <class Abandoned>
    [[nodiscard]] ComputePageWait WaitForComputePage(RHI::ITransferQueue& queue, const RHI::ReadbackToken token,
        Abandoned abandoned,
        const std::chrono::steady_clock::duration timeout = std::chrono::steady_clock::duration::max())
    {
        const auto start = timeout == std::chrono::steady_clock::duration::max()
            ? std::chrono::steady_clock::time_point{} : std::chrono::steady_clock::now();
        for (;;)
        {
            switch (queue.WaitCompute(token, 20'000'000u))
            {
            case RHI::ComputeWaitStatus::Complete: return ComputePageWait::Complete;
            case RHI::ComputeWaitStatus::Failed: return ComputePageWait::Failed;
            case RHI::ComputeWaitStatus::Pending:
                if (abandoned())
                    return ComputePageWait::Abandoned;
                if (timeout != std::chrono::steady_clock::duration::max() &&
                    std::chrono::steady_clock::now() - start >= timeout)
                    return ComputePageWait::TimedOut;
            }
        }
    }
}
}

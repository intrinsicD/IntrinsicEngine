// Handle-free lane policy of the transfer service (ARCH-023): token lane encoding, per-lane completion and the
// method-compute lane resolution. Re-exported by the primary interface for CPU contract tests.
module;

#include <array>
#include <cstddef>
#include <cstdint>

export module Extrinsic.Backends.Vulkan:TransferLanes;

export import Extrinsic.RHI.QueueAffinity;

namespace Extrinsic::Backends::Vulkan
{
    // The queue a test gate holds (WithVulkanQueueGate): the separate method-compute queue, graphics, or the frames'
    // own async-compute or transfer queue where it is not the graphics queue.
    export enum class VulkanGatedQueue : std::uint8_t { MethodCompute, Graphics, FrameCompute, FrameTransfer };
    // The device-local query selected by the one-shot retirement failure test seam.
    export enum class VulkanRetirementQuery : std::uint8_t { FrameFence, GraphicsTimeline };

    export struct MethodComputeQueueSelection
    {
        std::uint32_t Family = 0;
        std::uint32_t Index = 0;
        bool Available = false;
    };

    // Keep method work off frame compute when either of the already shared families has a spare queue.
    export [[nodiscard]] constexpr MethodComputeQueueSelection SelectMethodComputeQueue(
        const std::uint32_t graphicsFamily,
        const std::uint32_t graphicsComputeQueueCount,
        const std::uint32_t frameComputeFamily,
        const std::uint32_t frameComputeIndex,
        const std::uint32_t frameComputeQueueCount) noexcept
    {
        if (frameComputeQueueCount > frameComputeIndex + 1u)
            return {frameComputeFamily, frameComputeIndex + 1u, true};
        if (graphicsComputeQueueCount > 1u && (frameComputeQueueCount == 0u || frameComputeFamily != graphicsFamily))
            return {graphicsFamily, 1u, true};
        if (frameComputeQueueCount != 0u)
            return {frameComputeFamily, frameComputeIndex, true};
        return {graphicsFamily, 0u, false};
    }

    // ARCH-023: the transfer service submits on two lanes. Uploads, downloads and framed work stay on the graphics
    // queue; method compute (IDevice::SubmitComputeReadback) runs on the async compute queue when one exists and is
    // not forced back to graphics. Each lane has its own command pool and timeline.
    export enum class TransferLane : std::uint8_t { Graphics = 0, Compute = 1 };

    // Readback tokens name their lane in the top bit, so completion is judged against that lane's timeline. Values
    // stay nonzero and monotonic per lane; graphics-lane values are the plain timeline tickets.
    export constexpr std::uint64_t kComputeLaneTicketBit = 1ull << 63u;
    export [[nodiscard]] constexpr std::uint64_t EncodeLaneTicket(
        const TransferLane lane, const std::uint64_t ticket) noexcept
    {
        return lane == TransferLane::Compute ? ticket | kComputeLaneTicketBit : ticket;
    }
    export [[nodiscard]] constexpr TransferLane LaneOfTicket(const std::uint64_t value) noexcept
    {
        return (value & kComputeLaneTicketBit) != 0u ? TransferLane::Compute : TransferLane::Graphics;
    }
    export [[nodiscard]] constexpr std::uint64_t LaneTicketValue(const std::uint64_t value) noexcept
    {
        return value & ~kComputeLaneTicketBit;
    }
    // `completed` is indexed by TransferLane. The invalid value 0 means nothing was submitted.
    export [[nodiscard]] constexpr bool IsLaneTicketComplete(
        const std::uint64_t value, const std::array<std::uint64_t, 2>& completed) noexcept
    {
        return value == 0u || LaneTicketValue(value) <= completed[static_cast<std::size_t>(LaneOfTicket(value))];
    }
    // The lane method compute resolves to, in the queue-affinity vocabulary: requested async compute unless the
    // configuration forces graphics; demoted when no async compute queue was acquired.
    export [[nodiscard]] constexpr RHI::QueueAffinityResolution ResolveMethodComputeLane(
        const bool asyncComputeQueueAvailable, const bool forceGraphics) noexcept
    {
        return RHI::ResolveQueueAffinity(
            forceGraphics ? RHI::QueueAffinity::Graphics : RHI::QueueAffinity::AsyncCompute,
            RHI::QueueCapabilityProfile{.SupportsAsyncCompute = asyncComputeQueueAvailable});
    }
}

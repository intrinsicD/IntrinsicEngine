module;
#include <cstdint>
#include <memory>

export module Extrinsic.Backends.Vulkan;

import Extrinsic.RHI.Device;

namespace Extrinsic::Backends::Vulkan
{
    // Factory — creates the Vulkan backend IDevice.
    // Call once during engine initialization; the returned device owns all
    // Vulkan state.  Destroy it before the window is destroyed.
    export std::unique_ptr<RHI::IDevice> CreateVulkanDevice();

    // Debug breadcrumb for fail-closed non-operational bindless allocation attempts.
    export std::uint64_t GetFallbackBindlessAllocationAttemptCount() noexcept;

    // Debug breadcrumb for fail-closed non-operational transfer-queue upload attempts.
    // Increments for every UploadBuffer/UploadTexture call serviced by the
    // FallbackTransferQueue while the device is non-operational.
    export std::uint64_t GetFallbackTransferUploadAttemptCount() noexcept;

    // Debug breadcrumb for fail-closed non-operational pipeline creation attempts.
    // Increments for every VulkanDevice::CreatePipeline call that returns an
    // invalid handle because the device is non-operational or the global
    // pipeline layout is not yet created.
    export std::uint64_t GetFallbackPipelineCreationAttemptCount() noexcept;

    // Structured reason for the most recent fail-closed CreatePipeline call.
    // Exposed for CPU diagnostics that need to distinguish "device or layout
    // not yet brought up" from "operational guard reached but shader/pipeline
    // construction is still unimplemented". Bindless and transfer-queue
    // fallbacks intentionally do not yet expose a reason enum because each
    // currently has a single fail-closed reason; revisit when a second
    // emerges. The accessor is process-monotonic in the same sense as the
    // counters: it reports the last observed reason and is never reset across
    // Initialize/Shutdown cycles. `None` indicates no fail-closed
    // CreatePipeline call has been observed since process start.
    export enum class FallbackPipelineReason : std::uint8_t
    {
        None = 0,
        PreBringUp = 1,    // device non-operational or global pipeline layout missing
        ShaderMissing = 2, // operational guard reached but shader/pipeline construction unimplemented
    };
    export FallbackPipelineReason GetLastFallbackPipelineReason() noexcept;
}


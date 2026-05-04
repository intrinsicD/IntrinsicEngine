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
}


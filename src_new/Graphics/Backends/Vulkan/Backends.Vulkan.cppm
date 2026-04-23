module;
#include <memory>

export module Extrinsic.Backends.Vulkan;

import Extrinsic.RHI.Device;

namespace Extrinsic::Backends::Vulkan
{
    // Factory — creates the Vulkan backend IDevice.
    // Call once during engine initialization; the returned device owns all
    // Vulkan state.  Destroy it before the window is destroyed.
    export std::unique_ptr<RHI::IDevice> CreateVulkanDevice();
}


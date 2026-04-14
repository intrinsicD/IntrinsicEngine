module;

#include <memory>

export module Extrinsic.Backends.Vulkan;

import Extrinsic.RHI.Device;

namespace Extrinsic::Backends::Vulkan
{
    export std::unique_ptr<RHI::IDevice> CreateVulkanDevice();
}


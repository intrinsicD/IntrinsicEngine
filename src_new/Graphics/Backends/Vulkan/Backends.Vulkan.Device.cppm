export module Extrinsic.Backends.Vulkan.Device;

import <memory>;
import Extrinsic.RHI.Device;

namespace Extrinsic::Backends::Vulkan
{
    export std::unique_ptr<Extrinsic::RHI::IDevice> CreateVulkanDevice();
}
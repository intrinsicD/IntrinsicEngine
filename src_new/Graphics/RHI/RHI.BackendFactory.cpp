module Extrinsic.RHI.Factory;

import Extrinsic.Backends.Vulkan;
import Extrinsic.Core.Config;
import Extrinsic.RHI.Device;
import <memory>;
import <stdexcept>;

namespace Extrinsic::RHI
{
    std::unique_ptr<IDevice> CreateDevice(Core::GraphicsBackend backend)
    {
        switch (backend)
        {
        case Extrinsic::Core::GraphicsBackend::Vulkan:
            return Backends::Vulkan::CreateVulkanDevice();
        }

        throw std::runtime_error("Unsupported graphics backend.");
    }
}
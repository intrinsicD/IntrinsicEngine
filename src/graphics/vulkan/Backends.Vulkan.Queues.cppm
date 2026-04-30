module;

#include <cstdint>

#include "Vulkan.hpp"

export module Extrinsic.Backends.Vulkan:Queues;

namespace Extrinsic::Backends::Vulkan
{
    export struct VulkanQueueFamilies
    {
        uint32_t Graphics = 0;
        uint32_t Present  = 0;
        uint32_t Transfer = 0;
    };

    export struct VulkanQueues
    {
        VkQueue Graphics = VK_NULL_HANDLE;
        VkQueue Present  = VK_NULL_HANDLE;
        VkQueue Transfer = VK_NULL_HANDLE;
    };
}


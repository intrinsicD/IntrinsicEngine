module;

#include "Vulkan.hpp"

export module Extrinsic.Backends.Vulkan:Surface;

namespace Extrinsic::Backends::Vulkan
{
    export struct VulkanSurfaceState
    {
        VkInstance   Instance = VK_NULL_HANDLE;
        VkSurfaceKHR Surface  = VK_NULL_HANDLE;
    };
}


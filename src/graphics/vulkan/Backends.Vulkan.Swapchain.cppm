module;

#include <vector>

#include "Vulkan.hpp"

export module Extrinsic.Backends.Vulkan:Swapchain;

export import Extrinsic.RHI.Handles;
export import Extrinsic.RHI.Types;

namespace Extrinsic::Backends::Vulkan
{
    export struct VulkanSwapchainState
    {
        VkSwapchainKHR Swapchain = VK_NULL_HANDLE;
        VkFormat       Format    = VK_FORMAT_UNDEFINED;
        VkExtent2D     Extent    = {};
        std::vector<VkImage> Images;
        std::vector<VkImageView> Views;
        std::vector<RHI::TextureHandle> Handles;
    };
}


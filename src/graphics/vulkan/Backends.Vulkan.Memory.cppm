module;

#include <cstdint>

#include "Vulkan.hpp"

export module Extrinsic.Backends.Vulkan:Memory;

namespace Extrinsic::Backends::Vulkan
{
    export struct VulkanBuffer
    {
        VkBuffer      Buffer      = VK_NULL_HANDLE;
        VmaAllocation Allocation  = VK_NULL_HANDLE;
        void*         MappedPtr   = nullptr;
        uint64_t      SizeBytes   = 0;
        bool          HostVisible = false;
        bool          HasBDA      = false;
    };

    export struct VulkanImage
    {
        VkImage       Image       = VK_NULL_HANDLE;
        VkImageView   View        = VK_NULL_HANDLE;
        VmaAllocation Allocation  = VK_NULL_HANDLE;
        VkFormat      Format      = VK_FORMAT_UNDEFINED;
        uint32_t      Width       = 0;
        uint32_t      Height      = 0;
        uint32_t      MipLevels   = 1;
        uint32_t      ArrayLayers = 1;
        bool          OwnsMemory  = true;
    };

    export struct VulkanSampler
    {
        VkSampler Sampler = VK_NULL_HANDLE;
    };
}


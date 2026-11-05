module;

#include <algorithm>
#include <cstdint>

#include "Vulkan.hpp"

export module Extrinsic.Backends.Vulkan:Memory;

export import Extrinsic.RHI.Descriptors;
export import Extrinsic.RHI.Device;

namespace Extrinsic::Backends::Vulkan
{
    // A host buffer the CPU reads after the device wrote it (readback staging, CPU-read host-visible buffers). AUTO is
    // needed for VMA to honour the random-access flag (prefer HOST_CACHED); CPU_ONLY and CPU_TO_GPU ignore it and can
    // land in uncached memory, where the CPU's copy out is slow. Coherent stays required.
    export [[nodiscard]] inline VmaAllocationCreateInfo HostReadbackAllocationInfo() noexcept
    {
        VmaAllocationCreateInfo info{};
        info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        info.requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
        return info;
    }

    // True when the allocation lacks HOST_CACHED although the device has a visible, coherent and cached type.
    export [[nodiscard]] inline bool IsUncachedHostReadback(VmaAllocator vma, VmaAllocation allocation) noexcept
    {
        VkMemoryPropertyFlags flags{};
        vmaGetAllocationMemoryProperties(vma, allocation, &flags);
        const VkPhysicalDeviceMemoryProperties* memory = nullptr;
        vmaGetMemoryProperties(vma, &memory);
        constexpr VkMemoryPropertyFlags cached = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        const bool available = std::any_of(memory->memoryTypes, memory->memoryTypes + memory->memoryTypeCount,
            [](const VkMemoryType& type) { return (type.propertyFlags & cached) == cached; });
        return (flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) == 0 && available;
    }

    export struct VulkanMemoryBlock
    {
        VmaAllocation Allocation = VK_NULL_HANDLE;
        std::uint64_t SizeBytes = 0;
        std::uint64_t AlignmentBytes = 1;
        std::uint32_t MemoryTypeBits = 0;
        std::uint32_t SelectedMemoryTypeBit = 0;
    };

    export struct VulkanBuffer
    {
        VkBuffer      Buffer       = VK_NULL_HANDLE;
        VmaAllocation Allocation   = VK_NULL_HANDLE;
        void*         MappedPtr    = nullptr;
        VkBufferUsageFlags Usage   = 0;
        uint64_t      SizeBytes    = 0;
        bool          HostVisible  = false;
        bool          HostCoherent = false;
        bool          HostReadUncached = false; // host-visible without HOST_CACHED while a cached type exists
        bool          HasBDA       = false;
        bool          OwnsMemory   = true;
        RHI::PlacedResourceInfo Placement{};
    };

    export struct VulkanImage
    {
        VkImage       Image       = VK_NULL_HANDLE;
        VkImageView   View        = VK_NULL_HANDLE;
        VmaAllocation Allocation  = VK_NULL_HANDLE;
        VkFormat      Format      = VK_FORMAT_UNDEFINED;
        RHI::Format   RhiFormat   = RHI::Format::Undefined;
        RHI::TextureDimension Dimension = RHI::TextureDimension::Tex2D;
        VkImageUsageFlags Usage    = 0;
        uint32_t      Width       = 0;
        uint32_t      Height      = 0;
        uint32_t      Depth       = 1;
        uint32_t      MipLevels   = 1;
        uint32_t      ArrayLayers = 1;
        VkImageLayout CurrentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        bool          OwnsImage   = true;
        bool          OwnsMemory  = true;
        RHI::PlacedResourceInfo Placement{};
    };

    export struct VulkanSampler
    {
        VkSampler Sampler = VK_NULL_HANDLE;
    };
}

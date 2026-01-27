module;
#include <algorithm>
#include <mutex>
#include <utility>
#include "RHI.Vulkan.hpp"

module RHI:TransientAllocator.Impl;

import :TransientAllocator;
import Core;

namespace RHI
{
    static constexpr VkDeviceSize AlignUpPow2(VkDeviceSize offset, VkDeviceSize alignment)
    {
        // Vulkan guarantees power-of-two alignments for VkMemoryRequirements::alignment.
        return (offset + alignment - 1) & ~(alignment - 1);
    }

    TransientAllocator::TransientAllocator(VulkanDevice& device, VkDeviceSize pageSizeBytes)
        : m_Device(device)
          , m_PageSize(pageSizeBytes)
    {
        VkPhysicalDeviceMemoryProperties memProps{};
        vkGetPhysicalDeviceMemoryProperties(m_Device.GetPhysicalDevice(), &memProps);

        m_Buckets.resize(memProps.memoryTypeCount);
        for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
        {
            m_Buckets[i].MemoryTypeIndex = i;
        }
    }

    TransientAllocator::~TransientAllocator()
    {
        // Note: SafeDestroy is for deferred destruction; but transient pages are long-lived.
        // We can destroy immediately since allocator lifetime matches device lifetime.
        VkDevice device = m_Device.GetLogicalDevice();

        for (auto& bucket : m_Buckets)
        {
            for (auto& page : bucket.Pages)
            {
                if (page.Memory != VK_NULL_HANDLE)
                {
                    vkFreeMemory(device, page.Memory, nullptr);
                    page.Memory = VK_NULL_HANDLE;
                }
            }
            bucket.Pages.clear();
        }
    }

    void TransientAllocator::Reset()
    {
        std::lock_guard lock(m_Mutex);

        for (auto& bucket : m_Buckets)
        {
            for (auto& page : bucket.Pages)
            {
                page.UsedOffset = 0;
            }
            bucket.ActivePageIndex = 0;
        }
    }

    TransientAllocator::Allocation TransientAllocator::Allocate(const VkMemoryRequirements& reqs,
                                                                VkMemoryPropertyFlags preferredFlags)
    {
        if (reqs.size == 0)
        {
            return {};
        }

        const VkDeviceSize alignment = std::max<VkDeviceSize>(reqs.alignment, 1);

        // Strictly speaking this should always be power-of-two, but defend anyway.
        if ((alignment & (alignment - 1)) != 0)
        {
            Core::Log::Error("TransientAllocator: Non power-of-two alignment {}", static_cast<uint64_t>(alignment));
            return {};
        }

        uint32_t typeIndex = FindMemoryType(reqs.memoryTypeBits, preferredFlags);
        if (typeIndex == ~0u)
        {
            // Fallback: accept any compatible type.
            typeIndex = FindMemoryType(reqs.memoryTypeBits, 0);
        }

        if (typeIndex == ~0u)
        {
            Core::Log::Error("TransientAllocator: Failed to find compatible memory type (typeBits=0x{:x})",
                            reqs.memoryTypeBits);
            return {};
        }

        std::lock_guard lock(m_Mutex);
        auto& bucket = m_Buckets[typeIndex];

        // Try to allocate from existing pages.
        for (size_t i = bucket.ActivePageIndex; i < bucket.Pages.size(); ++i)
        {
            auto& page = bucket.Pages[i];
            const VkDeviceSize alignedOffset = AlignUpPow2(page.UsedOffset, alignment);

            if (alignedOffset + reqs.size <= page.Size)
            {
                bucket.ActivePageIndex = i;
                page.UsedOffset = alignedOffset + reqs.size;
                return {page.Memory, alignedOffset, reqs.size};
            }
        }

        // Need a new page. Grow to fit this request.
        const VkDeviceSize newPageSize = std::max(m_PageSize, reqs.size);
        Page newPage = CreatePage(typeIndex, newPageSize);
        if (newPage.Memory == VK_NULL_HANDLE)
        {
            return {};
        }

        // Place at offset 0 (0 is aligned for any power-of-two alignment).
        newPage.UsedOffset = reqs.size;
        bucket.Pages.push_back(newPage);
        bucket.ActivePageIndex = bucket.Pages.size() - 1;
        return {newPage.Memory, 0, reqs.size};
    }

    uint32_t TransientAllocator::FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const
    {
        VkPhysicalDeviceMemoryProperties memProps{};
        vkGetPhysicalDeviceMemoryProperties(m_Device.GetPhysicalDevice(), &memProps);

        for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
        {
            const bool supported = (typeFilter & (1u << i)) != 0;
            if (!supported) continue;

            if ((memProps.memoryTypes[i].propertyFlags & properties) == properties)
            {
                return i;
            }
        }

        return ~0u;
    }

    TransientAllocator::Page TransientAllocator::CreatePage(uint32_t memoryTypeIndex, VkDeviceSize sizeBytes) const
    {
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = sizeBytes;
        allocInfo.memoryTypeIndex = memoryTypeIndex;

        Page page{};
        page.Size = sizeBytes;
        page.UsedOffset = 0;

        const VkResult res = vkAllocateMemory(m_Device.GetLogicalDevice(), &allocInfo, nullptr, &page.Memory);
        if (res != VK_SUCCESS)
        {
            Core::Log::Error("TransientAllocator: vkAllocateMemory failed (size={} bytes, typeIndex={}, res={})",
                            static_cast<uint64_t>(sizeBytes),
                            memoryTypeIndex,
                            static_cast<int>(res));
            page.Memory = VK_NULL_HANDLE;
            page.Size = 0;
            return page;
        }

        return page;
    }
}

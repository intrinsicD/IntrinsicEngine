module;
#include <cstring> // memcpy
#include <memory>
#include <utility> // std::exchange
#include "RHI.Vulkan.hpp"

module RHI:Buffer.Impl;

import :Buffer;
import Core;

namespace RHI
{
    static bool IsHostVisibleUsage(VmaMemoryUsage usage)
    {
        return usage == VMA_MEMORY_USAGE_CPU_TO_GPU ||
               usage == VMA_MEMORY_USAGE_GPU_TO_CPU ||
               usage == VMA_MEMORY_USAGE_CPU_ONLY ||
               usage == VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
    }

    VulkanBuffer::VulkanBuffer(VulkanDevice& device, size_t size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage)
        : m_Device(device)
    {
        m_SizeBytes = size;

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        // IMPORTANT: Do not implicitly add SHADER_DEVICE_ADDRESS.
        // Some buffers (e.g. indirect count buffers) must be created with specific usage bits;
        // adding unrelated bits can trigger validation requirements or feature dependencies.
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = memoryUsage;
        if (IsHostVisibleUsage(memoryUsage))
        {
            allocInfo.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
            if (memoryUsage == VMA_MEMORY_USAGE_CPU_TO_GPU || memoryUsage == VMA_MEMORY_USAGE_AUTO_PREFER_HOST)
            {
                allocInfo.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
            }
            else if (memoryUsage == VMA_MEMORY_USAGE_GPU_TO_CPU)
            {
                allocInfo.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
            }
        }

        VmaAllocationInfo resultInfo{};

        if (vmaCreateBuffer(device.GetAllocator(), &bufferInfo, &allocInfo, &m_Buffer, &m_Allocation, &resultInfo) !=
            VK_SUCCESS)
        {
            Core::Log::Error("Failed to create buffer (size={})", size);
            return;
        }

        // Cache memory properties so Map() can be a fast, safe check.
        VkMemoryPropertyFlags memFlags = 0;
        vmaGetAllocationMemoryProperties(device.GetAllocator(), m_Allocation, &memFlags);
        m_IsHostVisible = (memFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;

        if (m_IsHostVisible)
        {
            if (resultInfo.pMappedData != nullptr)
            {
                m_MappedData = resultInfo.pMappedData;
            }
            else
            {
                VkResult mapRes = vmaMapMemory(device.GetAllocator(), m_Allocation, &m_MappedData);
                if (mapRes != VK_SUCCESS)
                {
                    Core::Log::Error("Failed to persistently map host-visible buffer (VkResult={}).",
                                    static_cast<int>(mapRes));
                    m_MappedData = nullptr;
                }
            }
        }
        else
        {
            m_MappedData = nullptr;
        }
    }

    VulkanBuffer::~VulkanBuffer()
    {
        if (!m_Buffer) return;

        VkBuffer buffer = m_Buffer;
        VmaAllocation allocation = m_Allocation;
        VmaAllocator allocator = m_Device.GetAllocator();

        m_Device.SafeDestroy([allocator, buffer, allocation]()
        {
            vmaDestroyBuffer(allocator, buffer, allocation);
        });
    }

    VulkanBuffer::VulkanBuffer(VulkanBuffer&& other) noexcept
        : m_Device(other.m_Device)
          , m_Buffer(std::exchange(other.m_Buffer, VK_NULL_HANDLE))
          , m_Allocation(std::exchange(other.m_Allocation, VK_NULL_HANDLE))
          , m_MappedData(std::exchange(other.m_MappedData, nullptr))
          , m_SizeBytes(other.m_SizeBytes)
          , m_IsHostVisible(other.m_IsHostVisible)
    {
    }

    VulkanBuffer& VulkanBuffer::operator=(VulkanBuffer&& other) noexcept
    {
        if (this != &other)
        {
            // Destroy current resources if any
            if (m_Buffer)
            {
                VkBuffer buffer = m_Buffer;
                VmaAllocation allocation = m_Allocation;
                VmaAllocator allocator = m_Device.GetAllocator();
                m_Device.SafeDestroy([allocator, buffer, allocation]()
                {
                    vmaDestroyBuffer(allocator, buffer, allocation);
                });
            }

            // Move from other
            m_Buffer = std::exchange(other.m_Buffer, VK_NULL_HANDLE);
            m_Allocation = std::exchange(other.m_Allocation, VK_NULL_HANDLE);
            m_MappedData = std::exchange(other.m_MappedData, nullptr);
            m_SizeBytes = other.m_SizeBytes;
            m_IsHostVisible = other.m_IsHostVisible;
        }
        return *this;
    }

    void* VulkanBuffer::Map()
    {
        if (!m_Allocation) return nullptr;

        if (!m_IsHostVisible)
        {
            Core::Log::Error("VulkanBuffer::Map(): Attempted to map non-host-visible memory (device-local). "
                            "Use a staging upload path or create the buffer with CPU_TO_GPU/AUTO_PREFER_HOST.");
            return nullptr;
        }

        return m_MappedData;
    }

    void VulkanBuffer::Unmap()
    {
        // No-op for persistent mapping. Kept for API compatibility.
    }

    uint64_t VulkanBuffer::GetDeviceAddress() const
    {
        VkBufferDeviceAddressInfo info{};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        info.buffer = m_Buffer;
        return vkGetBufferDeviceAddress(m_Device.GetLogicalDevice(), &info);
    }

    void VulkanBuffer::Invalidate(size_t offset, size_t size)
    {
        if (!m_Allocation || !m_IsHostVisible) return;
        vmaInvalidateAllocation(m_Device.GetAllocator(), m_Allocation, static_cast<VkDeviceSize>(offset),
                                static_cast<VkDeviceSize>(size));
    }

    void VulkanBuffer::Flush(size_t offset, size_t size)
    {
        if (!m_Allocation || !m_IsHostVisible) return;
        vmaFlushAllocation(m_Device.GetAllocator(), m_Allocation, static_cast<VkDeviceSize>(offset),
                           static_cast<VkDeviceSize>(size));
    }
}

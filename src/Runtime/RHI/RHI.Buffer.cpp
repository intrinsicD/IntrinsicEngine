module;
#include <vk_mem_alloc.h>
#include <cstring> // memcpy
#include <memory>

module RHI:Buffer.Impl;
import :Buffer;
import Core;

namespace RHI {

    VulkanBuffer::VulkanBuffer(std::shared_ptr<VulkanDevice> device, size_t size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage)
        : m_Device(device)
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = memoryUsage;
        // Use VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT for CPU->GPU mapping
        if (memoryUsage == VMA_MEMORY_USAGE_AUTO_PREFER_HOST || memoryUsage == VMA_MEMORY_USAGE_CPU_TO_GPU) {
            allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        }

        VmaAllocationInfo resultInfo{};

        if (vmaCreateBuffer(device->GetAllocator(), &bufferInfo, &allocInfo, &m_Buffer, &m_Allocation, &resultInfo) != VK_SUCCESS) {
            Core::Log::Error("Failed to create buffer!");
            return;
        }

        if (resultInfo.pMappedData != nullptr) {
            m_MappedData = resultInfo.pMappedData;
            m_IsPersistent = true;
        }
    }

    VulkanBuffer::~VulkanBuffer() {
        if (m_Buffer) {
            VkBuffer buffer = m_Buffer;
            VmaAllocation allocation = m_Allocation;
            VmaAllocator allocator = m_Device->GetAllocator();

            m_Device->SafeDestroy([allocator, buffer, allocation]() {
                vmaDestroyBuffer(allocator, buffer, allocation);
            });
        }
    }

    void* VulkanBuffer::Map() {
        if (m_MappedData) {
            return m_MappedData;
        }

        vmaMapMemory(m_Device->GetAllocator(), m_Allocation, &m_MappedData);
        return m_MappedData;
    }

    void VulkanBuffer::Unmap() {
        if (m_IsPersistent) return;

        if (m_MappedData) {
            vmaUnmapMemory(m_Device->GetAllocator(), m_Allocation);
            m_MappedData = nullptr;
        }
    }
}
module;
#include <vk_mem_alloc.h>
#include <cstring> // memcpy
#include <memory>


module Runtime.RHI.Buffer;
import Core.Logging;

namespace Runtime::RHI {

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
            allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
        }

        if (vmaCreateBuffer(device->GetAllocator(), &bufferInfo, &allocInfo, &m_Buffer, &m_Allocation, nullptr) != VK_SUCCESS) {
            Core::Log::Error("Failed to create buffer!");
        }
    }

    VulkanBuffer::~VulkanBuffer() {
        vmaDestroyBuffer(m_Device->GetAllocator(), m_Buffer, m_Allocation);
    }

    void* VulkanBuffer::Map() {
        void* data;
        vmaMapMemory(m_Device->GetAllocator(), m_Allocation, &data);
        return data;
    }

    void VulkanBuffer::Unmap() {
        vmaUnmapMemory(m_Device->GetAllocator(), m_Allocation);
    }
}
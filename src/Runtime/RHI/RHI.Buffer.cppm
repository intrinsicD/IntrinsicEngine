module;
#include <cstring>
#include <memory>
#include "RHI.Vulkan.hpp"

export module RHI:Buffer;

import :Device;

export namespace RHI {
    class VulkanBuffer {
    public:
        // usage: VertexBuffer, IndexBuffer, TransferSrc, etc.
        // properties: DeviceLocal (GPU only) or HostVisible (CPU writable)
        VulkanBuffer(std::shared_ptr<VulkanDevice> device, size_t size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
        ~VulkanBuffer();

        // Disable copy
        VulkanBuffer(const VulkanBuffer&) = delete;
        VulkanBuffer& operator=(const VulkanBuffer&) = delete;

        // Enable move for efficient container storage
        VulkanBuffer(VulkanBuffer&& other) noexcept;
        VulkanBuffer& operator=(VulkanBuffer&& other) noexcept;

        [[nodiscard]] VkBuffer GetHandle() const { return m_Buffer; }
        
        // Map memory (only works for HostVisible)
        void* Map();
        void Unmap();

        [[nodiscard]] void* GetMappedData() const { return m_MappedData; }

        void Write(const void* data, size_t size, size_t offset = 0) {
            void* ptr = Map();
            std::memcpy(static_cast<uint8_t*>(ptr) + offset, data, size);
            Unmap();
        }

    private:
        std::shared_ptr<VulkanDevice> m_Device;
        VkBuffer m_Buffer = VK_NULL_HANDLE;
        VmaAllocation m_Allocation = VK_NULL_HANDLE;

        // The persistent pointer. nullptr if memory is GPU-only.
        void* m_MappedData = nullptr;
        bool m_IsPersistent = false;
    };
}
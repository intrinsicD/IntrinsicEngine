module;
#include <cstring>
#include <limits>
#include "RHI.Vulkan.hpp"

export module RHI:Buffer;

import :Device;

export namespace RHI {
    class VulkanBuffer {
    public:
        // usage: VertexBuffer, IndexBuffer, TransferSrc, etc.
        // properties: DeviceLocal (GPU only) or HostVisible (CPU writable)
        VulkanBuffer(VulkanDevice& device, size_t size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
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

        [[nodiscard]] uint64_t GetDeviceAddress() const;

        void Write(const void* data, size_t size, size_t offset = 0) {
            void* ptr = Map();
            std::memcpy(static_cast<uint8_t*>(ptr) + offset, data, size);
            Unmap();
        }

        // Explicit cache management for host-visible memory.
        // Needed for GPU->CPU readbacks when memory is not HOST_COHERENT.
        void Invalidate(size_t offset = 0, size_t size = std::numeric_limits<size_t>::max());
        void Flush(size_t offset = 0, size_t size = std::numeric_limits<size_t>::max());

    private:
        VulkanDevice& m_Device;
        VkBuffer m_Buffer = VK_NULL_HANDLE;
        VmaAllocation m_Allocation = VK_NULL_HANDLE;

        // The persistent pointer. nullptr if memory is GPU-only.
        void* m_MappedData = nullptr;
        bool m_IsPersistent = false;
    };
}
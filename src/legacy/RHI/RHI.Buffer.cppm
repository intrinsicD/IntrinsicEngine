module;
#include <cstring>
#include <limits>
#include <mutex>
#include <memory>
#include "RHI.Vulkan.hpp"

export module RHI.Buffer;

import Core.Handle;
import Core.ResourcePool;
import RHI.Device;
import Core.Logging; // For Core::Log::Error in inline Write/Read methods

export namespace RHI {
    class VulkanBuffer {
    public:
        // usage: VertexBuffer, IndexBuffer, TransferSrc, etc.
        // properties: DeviceLocal (GPU only) or HostVisible (CPU writable)
        // Map/Unmap manage host mapping on demand for host-visible buffers.
        VulkanBuffer(VulkanDevice& device, size_t size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
        ~VulkanBuffer();

        // Disable copy
        VulkanBuffer(const VulkanBuffer&) = delete;
        VulkanBuffer& operator=(const VulkanBuffer&) = delete;

        // Enable move for efficient container storage
        VulkanBuffer(VulkanBuffer&& other) noexcept;
        VulkanBuffer& operator=(VulkanBuffer&& other) noexcept;

        [[nodiscard]] VkBuffer GetHandle() const { return m_Buffer; }
        [[nodiscard]] bool IsValid() const { return m_Buffer != VK_NULL_HANDLE; }

        // Returns a mapped pointer for host-visible buffers.
        // Thread-safe: can be called concurrently. Pointer is valid until
        // caller unmaps or buffer destruction.
        // Returns nullptr if memory is DeviceLocal.
        void* Map();
        // Releases host mapping for this buffer.
        void Unmap();

        [[nodiscard]] void* GetMappedData() const { return m_MappedData; }

        [[nodiscard]] uint64_t GetDeviceAddress() const;

        void Write(const void* data, size_t size, size_t offset = 0)
        {
            if (!data || size == 0) return;
            if (offset + size > m_SizeBytes) return; // hard guard; validated in impl with a log

            if (!m_IsHostVisible)
            {
                Core::Log::Error("VulkanBuffer::Write(): buffer={} is not host-visible (GPU-only). size={} offset={} cap={}. Use TransferManager/StagingBelt to upload, or create with CPU_TO_GPU/AUTO_PREFER_HOST.",
                                (void*)m_Buffer, size, offset, m_SizeBytes);
                return;
            }

            if (!m_MappedData) return;
            std::memcpy(static_cast<uint8_t*>(m_MappedData) + offset, data, size);

            // Flush host writes. Safe for coherent memory too (no-op in driver/VMA).
            Flush(offset, size);
        }

        // Explicit cache management for host-visible memory.
        // Needed for GPU->CPU readbacks when memory is not HOST_COHERENT.
        void Invalidate(size_t offset = 0, size_t size = std::numeric_limits<size_t>::max());
        void Flush(size_t offset = 0, size_t size = std::numeric_limits<size_t>::max());

        [[nodiscard]] bool IsHostVisible() const { return m_IsHostVisible; }
        [[nodiscard]] size_t GetSizeBytes() const { return m_SizeBytes; }

        // Helper to read data from a mapped buffer (GPU->CPU).
        // Safely handles invalidation and copying.
        template<typename T>
        void Read(T* outData, size_t count = 1, size_t byteOffset = 0)
        {
            if (!outData || count == 0) return;
            const size_t byteSize = count * sizeof(T);
            
            if (byteOffset + byteSize > m_SizeBytes)
            {
                Core::Log::Error("VulkanBuffer::Read(): Out of bounds. size={} offset={} cap={}", byteSize, byteOffset, m_SizeBytes);
                return;
            }

            // Invalidate CPU cache to see GPU writes (if non-coherent).
            Invalidate(byteOffset, byteSize);

            if (!m_MappedData) return;
            std::memcpy(outData, static_cast<uint8_t*>(m_MappedData) + byteOffset, byteSize);
        }

    private:
        VulkanDevice& m_Device;
        VkBuffer m_Buffer = VK_NULL_HANDLE;
        VmaAllocation m_Allocation = VK_NULL_HANDLE;

        // The persistent pointer. nullptr if memory is GPU-only.
        void* m_MappedData = nullptr;
        bool m_IsMapped = false;
        bool m_UnmapOnDestroy = false;

        size_t m_SizeBytes = 0;
        bool m_IsHostVisible = false;
    };

    struct BufferTag {};
    using BufferHandle = Core::StrongHandle<BufferTag>;

    class BufferManager
    {
    public:
        class Lease
        {
        public:
            Lease() = default;
            ~Lease();

            Lease(const Lease&) = delete;
            Lease& operator=(const Lease&) = delete;
            Lease(Lease&& other) noexcept;
            Lease& operator=(Lease&& other) noexcept;

            [[nodiscard]] bool IsValid() const { return m_Manager && m_Handle.IsValid(); }
            [[nodiscard]] BufferHandle GetHandle() const { return m_Handle; }
            [[nodiscard]] VulkanBuffer* Get() const;
            [[nodiscard]] Lease Share() const;

            void Reset();

        private:
            friend class BufferManager;

            Lease(BufferManager* manager, BufferHandle handle, bool adopt);

            BufferManager* m_Manager = nullptr;
            BufferHandle m_Handle{};
        };

        explicit BufferManager(VulkanDevice& device);
        ~BufferManager();

        BufferManager(const BufferManager&) = delete;
        BufferManager& operator=(const BufferManager&) = delete;
        BufferManager(BufferManager&&) = delete;
        BufferManager& operator=(BufferManager&&) = delete;

        [[nodiscard]] BufferHandle Create(size_t size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
        [[nodiscard]] Lease CreateLease(size_t size, VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage);
        [[nodiscard]] Lease AcquireLease(BufferHandle handle);

        [[nodiscard]] VulkanBuffer* Get(BufferHandle handle) const;
        [[nodiscard]] VulkanBuffer* GetIfValid(BufferHandle handle) const;

        void Retain(BufferHandle handle);
        void Release(BufferHandle handle);
        void ProcessDeletions();
        void Clear();

    private:
        struct BufferRecord
        {
            std::unique_ptr<VulkanBuffer> Buffer{};
            uint32_t RefCount = 1;
        };

        VulkanDevice& m_Device;
        Core::ResourcePool<BufferRecord, BufferHandle, 3> m_Pool;
        mutable std::mutex m_Mutex;
    };

    using BufferLease = BufferManager::Lease;
}

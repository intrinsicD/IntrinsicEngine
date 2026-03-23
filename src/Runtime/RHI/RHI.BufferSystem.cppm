module;

#include "RHI.Vulkan.hpp"

export module RHI.BufferSystem;

import RHI.Device;
import RHI.BufferHandle;
import Core.ResourcePool; // For Core::ResourcePool

export namespace RHI
{
    //TODO flesh this out later... i want a better pool and handle based design to be more flexible
    struct BufferGpuData
    {
        VkBuffer m_Buffer = VK_NULL_HANDLE;
        VmaAllocation m_Allocation = VK_NULL_HANDLE;

        // The persistent pointer. nullptr if memory is GPU-only.
        void* m_MappedData = nullptr;

        size_t m_Offset = 0;
        size_t m_SizeBytes = 0;
        bool m_IsHostVisible = false;
    };

    class BufferSystem
    {
    public:
        BufferSystem(VulkanDevice& device);

        BufferHandle Create(size_t size, bool mapped = false, bool hostVisible = false);
        void Destroy(BufferHandle handle);

        void Clear();

        [[nodiscard]] const BufferGpuData* Get(BufferHandle handle) const;
        [[nodiscard]] const BufferGpuData* GetIfValid(BufferHandle handle) const;

    private:
        Core::ResourcePool<BufferGpuData, BufferHandle, 3> m_Pool;
    };
}

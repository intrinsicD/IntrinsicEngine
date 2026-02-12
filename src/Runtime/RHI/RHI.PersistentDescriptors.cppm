module;

#include <vector>
#include "RHI.Vulkan.hpp"

export module RHI:PersistentDescriptors;

import :Device;

export namespace RHI
{
    // A tiny helper for "long-lived" descriptor sets.
    //
    // Why this exists:
    // - DescriptorAllocator is intended for transient allocations and may reset pools.
    // - Some systems (forward instance SSBO sets, future retained submission bindings)
    //   need descriptor sets that remain valid across frames.
    class PersistentDescriptorPool
    {
    public:
        struct Stats
        {
            uint32_t PoolCount = 0;
            uint64_t AllocationCount = 0;
            uint32_t CurrentMaxSets = 0;
            uint32_t CurrentStorageBufferCount = 0;
        };

        explicit PersistentDescriptorPool(VulkanDevice& device,
                                          uint32_t maxSets = 64,
                                          uint32_t storageBufferCount = 256,
                                          const char* debugName = nullptr);
        ~PersistentDescriptorPool();

        PersistentDescriptorPool(const PersistentDescriptorPool&) = delete;
        PersistentDescriptorPool& operator=(const PersistentDescriptorPool&) = delete;

        [[nodiscard]] VkDescriptorSet Allocate(VkDescriptorSetLayout layout);

        [[nodiscard]] Stats GetStats() const;

    private:
        [[nodiscard]] VkDescriptorPool CreatePool(uint32_t maxSets, uint32_t storageBufferCount) const;
        bool Grow();

        VulkanDevice& m_Device;

        const char* m_DebugName = nullptr;

        uint32_t m_MaxSets = 0;
        uint32_t m_StorageBufferCount = 0;

        VkDescriptorPool m_CurrentPool = VK_NULL_HANDLE;
        std::vector<VkDescriptorPool> m_AllPools;

        uint64_t m_AllocationCount = 0;
    };
}

module;
#include "RHI.Vulkan.hpp"

export module RHI:PersistentDescriptors;

import :Device;
import Core;

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
        explicit PersistentDescriptorPool(VulkanDevice& device,
                                          uint32_t maxSets = 64,
                                          uint32_t storageBufferCount = 256);
        ~PersistentDescriptorPool();

        PersistentDescriptorPool(const PersistentDescriptorPool&) = delete;
        PersistentDescriptorPool& operator=(const PersistentDescriptorPool&) = delete;

        [[nodiscard]] VkDescriptorSet Allocate(VkDescriptorSetLayout layout);

    private:
        VulkanDevice& m_Device;
        VkDescriptorPool m_Pool = VK_NULL_HANDLE;
    };
}

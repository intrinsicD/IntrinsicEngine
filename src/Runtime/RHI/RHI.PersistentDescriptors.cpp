module;

#include <vector>
#include <algorithm>

#include "RHI.Vulkan.hpp"

module RHI:PersistentDescriptors.Impl;

import :PersistentDescriptors;

namespace RHI
{
    static const char* VkResultToString(const VkResult r)
    {
        switch (r)
        {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_NOT_READY: return "VK_NOT_READY";
        case VK_TIMEOUT: return "VK_TIMEOUT";
        case VK_EVENT_SET: return "VK_EVENT_SET";
        case VK_EVENT_RESET: return "VK_EVENT_RESET";
        case VK_INCOMPLETE: return "VK_INCOMPLETE";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        case VK_ERROR_FRAGMENTED_POOL: return "VK_ERROR_FRAGMENTED_POOL";
        case VK_ERROR_OUT_OF_POOL_MEMORY: return "VK_ERROR_OUT_OF_POOL_MEMORY";
        default: return "VK_ERROR_<unknown>";
        }
    }

    VkDescriptorPool PersistentDescriptorPool::CreatePool(const uint32_t maxSets, const uint32_t storageBufferCount) const
    {
        // NOTE: counts are NUMBER OF DESCRIPTORS, and maxSets limits NUMBER OF SETS.
        const VkDescriptorPoolSize sizes[] = {
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, storageBufferCount },
        };

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = 0; // Persistent: we don't free individual sets, and never reset.
        poolInfo.maxSets = maxSets;
        poolInfo.poolSizeCount = static_cast<uint32_t>(std::size(sizes));
        poolInfo.pPoolSizes = sizes;

        VkDescriptorPool pool = VK_NULL_HANDLE;
        const VkResult res = vkCreateDescriptorPool(m_Device.GetLogicalDevice(), &poolInfo, nullptr, &pool);
        if (res != VK_SUCCESS)
        {
            Core::Log::Error("PersistentDescriptorPool{}: vkCreateDescriptorPool failed ({}, {}) [maxSets={}, storageBuffers={}]",
                            (m_DebugName ? m_DebugName : ""),
                            static_cast<int>(res), VkResultToString(res),
                            maxSets, storageBufferCount);
            return VK_NULL_HANDLE;
        }

        return pool;
    }

    bool PersistentDescriptorPool::Grow()
    {
        // Geometric growth, clamped to avoid accidental runaway from bugs.
        constexpr uint32_t kMaxMaxSets = 1u << 20;
        constexpr uint32_t kMaxStorageBuffers = 1u << 22;

        const uint32_t newMaxSets = std::min(std::max(1u, m_MaxSets) * 2u, kMaxMaxSets);
        const uint32_t newStorageBuffers = std::min(std::max(1u, m_StorageBufferCount) * 2u, kMaxStorageBuffers);

        VkDescriptorPool newPool = CreatePool(newMaxSets, newStorageBuffers);
        if (newPool == VK_NULL_HANDLE)
        {
            return false;
        }

        m_MaxSets = newMaxSets;
        m_StorageBufferCount = newStorageBuffers;
        m_CurrentPool = newPool;
        m_AllPools.push_back(newPool);

        Core::Log::Warn("PersistentDescriptorPool{}: grew pool chain -> pools={}, maxSets={}, storageBuffers={}, allocations={}",
                          (m_DebugName ? m_DebugName : ""),
                          static_cast<uint32_t>(m_AllPools.size()), m_MaxSets, m_StorageBufferCount, m_AllocationCount);

        return true;
    }

    PersistentDescriptorPool::PersistentDescriptorPool(VulkanDevice& device,
                                                       const uint32_t maxSets,
                                                       const uint32_t storageBufferCount,
                                                       const char* debugName)
        : m_Device(device)
        , m_DebugName(debugName)
        , m_MaxSets(maxSets)
        , m_StorageBufferCount(storageBufferCount)
    {
        // Stage 1 only needs a couple of storage buffer descriptors per frame,
        // but we size this to avoid allocator churn.
        m_CurrentPool = CreatePool(m_MaxSets, m_StorageBufferCount);
        if (m_CurrentPool != VK_NULL_HANDLE)
        {
            m_AllPools.push_back(m_CurrentPool);
        }
    }

    PersistentDescriptorPool::~PersistentDescriptorPool()
    {
        if (m_AllPools.empty())
            return;

        const VkDevice dev = m_Device.GetLogicalDevice();
        const std::vector<VkDescriptorPool> pools = m_AllPools;
        m_Device.SafeDestroy([dev, pools]()
        {
            for (VkDescriptorPool pool : pools)
            {
                if (pool != VK_NULL_HANDLE)
                    vkDestroyDescriptorPool(dev, pool, nullptr);
            }
        });

        m_AllPools.clear();
        m_CurrentPool = VK_NULL_HANDLE;
    }

    VkDescriptorSet PersistentDescriptorPool::Allocate(VkDescriptorSetLayout layout)
    {
        if (layout == VK_NULL_HANDLE)
        {
            Core::Log::Error("PersistentDescriptorPool{}: Allocate called with null layout",
                            (m_DebugName ? m_DebugName : ""));
            return VK_NULL_HANDLE;
        }

        if (m_CurrentPool == VK_NULL_HANDLE)
        {
            Core::Log::Error("PersistentDescriptorPool{}: Allocate called with no valid pool",
                            (m_DebugName ? m_DebugName : ""));
            return VK_NULL_HANDLE;
        }

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_CurrentPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &layout;

        VkDescriptorSet set = VK_NULL_HANDLE;
        VkResult res = vkAllocateDescriptorSets(m_Device.GetLogicalDevice(), &allocInfo, &set);

        // Pool exhausted/fragmented: create a new pool and retry once.
        if (res == VK_ERROR_OUT_OF_POOL_MEMORY || res == VK_ERROR_FRAGMENTED_POOL)
        {
            Core::Log::Warn("PersistentDescriptorPool{}: allocation hit {} ({}); growing and retrying [layout={}, pools={}, maxSets={}, storageBuffers={}, allocations={}]",
                              (m_DebugName ? m_DebugName : ""),
                              static_cast<int>(res), VkResultToString(res),
                              (void*)layout,
                              static_cast<uint32_t>(m_AllPools.size()), m_MaxSets, m_StorageBufferCount, m_AllocationCount);

            if (Grow())
            {
                allocInfo.descriptorPool = m_CurrentPool;
                set = VK_NULL_HANDLE;
                res = vkAllocateDescriptorSets(m_Device.GetLogicalDevice(), &allocInfo, &set);
            }
        }

        if (res != VK_SUCCESS)
        {
            Core::Log::Error("PersistentDescriptorPool{}: vkAllocateDescriptorSets failed ({}, {}) [layout={}, pools={}, maxSets={}, storageBuffers={}, allocations={}]",
                            (m_DebugName ? m_DebugName : ""),
                            static_cast<int>(res), VkResultToString(res),
                            (void*)layout,
                            static_cast<uint32_t>(m_AllPools.size()), m_MaxSets, m_StorageBufferCount, m_AllocationCount);
            return VK_NULL_HANDLE;
        }

        ++m_AllocationCount;
        return set;
    }

    PersistentDescriptorPool::Stats PersistentDescriptorPool::GetStats() const
    {
        Stats s{};
        s.PoolCount = static_cast<uint32_t>(m_AllPools.size());
        s.AllocationCount = m_AllocationCount;
        s.CurrentMaxSets = m_MaxSets;
        s.CurrentStorageBufferCount = m_StorageBufferCount;
        return s;
    }
}

module;

#include <vector>

#include "RHI.Vulkan.hpp"

module RHI:PersistentDescriptors.Impl;

import :PersistentDescriptors;

namespace RHI
{
    PersistentDescriptorPool::PersistentDescriptorPool(VulkanDevice& device,
                                                       uint32_t maxSets,
                                                       uint32_t storageBufferCount)
        : m_Device(device)
    {
        // Stage 1 only needs a couple of storage buffer descriptors per frame,
        // but we size this to avoid allocator churn.
        std::vector<VkDescriptorPoolSize> sizes;
        sizes.push_back({VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, storageBufferCount});

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = 0;
        poolInfo.maxSets = maxSets;
        poolInfo.poolSizeCount = static_cast<uint32_t>(sizes.size());
        poolInfo.pPoolSizes = sizes.data();

        VK_CHECK(vkCreateDescriptorPool(m_Device.GetLogicalDevice(), &poolInfo, nullptr, &m_Pool));
    }

    PersistentDescriptorPool::~PersistentDescriptorPool()
    {
        if (m_Pool == VK_NULL_HANDLE)
            return;

        VkDescriptorPool pool = m_Pool;
        VkDevice dev = m_Device.GetLogicalDevice();
        m_Device.SafeDestroy([dev, pool]()
        {
            vkDestroyDescriptorPool(dev, pool, nullptr);
        });

        m_Pool = VK_NULL_HANDLE;
    }

    VkDescriptorSet PersistentDescriptorPool::Allocate(VkDescriptorSetLayout layout)
    {
        if (layout == VK_NULL_HANDLE || m_Pool == VK_NULL_HANDLE)
            return VK_NULL_HANDLE;

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_Pool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &layout;

        VkDescriptorSet set = VK_NULL_HANDLE;
        const VkResult res = vkAllocateDescriptorSets(m_Device.GetLogicalDevice(), &allocInfo, &set);
        if (res != VK_SUCCESS)
        {
            Core::Log::Error("PersistentDescriptorPool: vkAllocateDescriptorSets failed ({})", static_cast<int>(res));
            return VK_NULL_HANDLE;
        }

        return set;
    }
}

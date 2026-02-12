module;
#include "RHI.Vulkan.hpp"
#include <vector>

module RHI:Descriptors.Impl;
import :Descriptors;
import :Device;
import Core;

namespace RHI {

    // --- Descriptor Layout ---
    DescriptorLayout::DescriptorLayout(VulkanDevice& device) : m_Device(device) {
        VkDescriptorSetLayoutBinding uboBinding{};
        uboBinding.binding = 0;
        uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        uboBinding.descriptorCount = 1;
        uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &uboBinding;

        if (vkCreateDescriptorSetLayout(m_Device.GetLogicalDevice(), &layoutInfo, nullptr, &m_Layout) != VK_SUCCESS) {
            Core::Log::Error("Failed to create descriptor set layout!");
            m_IsValid = false;
        }
    }

    DescriptorLayout::~DescriptorLayout() {
        if (m_Layout == VK_NULL_HANDLE) return;
        VkDevice logicalDevice = m_Device.GetLogicalDevice();
        VkDescriptorSetLayout layout = m_Layout;

        m_Device.SafeDestroy([logicalDevice, layout]()
        {
            vkDestroyDescriptorSetLayout(logicalDevice, layout, nullptr);
        });
    }

    // --- Descriptor Allocator (growing pools) ---
    DescriptorAllocator::DescriptorAllocator(VulkanDevice& device)
        : m_Device(device)
    {}

    DescriptorAllocator::~DescriptorAllocator()
    {
        const VkDevice logicalDevice = m_Device.GetLogicalDevice();

        // NOTE: m_CurrentPool is also stored in m_UsedPools when active.
        // Destroy pools exactly once to avoid validation errors.
        m_CurrentPool = VK_NULL_HANDLE;

        for (VkDescriptorPool p : m_UsedPools)
        {
            if (p != VK_NULL_HANDLE)
                vkDestroyDescriptorPool(logicalDevice, p, nullptr);
        }
        for (VkDescriptorPool p : m_FreePools)
        {
            if (p != VK_NULL_HANDLE)
                vkDestroyDescriptorPool(logicalDevice, p, nullptr);
        }
    }

    VkDescriptorPool DescriptorAllocator::GrabPool()
    {
        if (!m_FreePools.empty())
        {
            VkDescriptorPool pool = m_FreePools.back();
            m_FreePools.pop_back();
            return pool;
        }

        // Generous, general-purpose pool sizes.
        // Note: counts are NUMBER OF DESCRIPTORS, and maxSets limits NUMBER OF SETS.
        // This is intended for transient per-frame allocations; if you add new descriptor types,
        // extend the size table.
        const VkDescriptorPoolSize sizes[] = {
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1024 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1024 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1024 },
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4096 },
            { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4096 },
            { VK_DESCRIPTOR_TYPE_SAMPLER, 2048 },
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1024 },
        };

        VkDescriptorPoolCreateInfo poolInfo{  };
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = 0; // We reset whole pools; no per-set free.
        poolInfo.maxSets = 2048;
        poolInfo.poolSizeCount = static_cast<uint32_t>(std::size(sizes));
        poolInfo.pPoolSizes = sizes;

        VkDescriptorPool pool = VK_NULL_HANDLE;
        const VkResult res = vkCreateDescriptorPool(m_Device.GetLogicalDevice(), &poolInfo, nullptr, &pool);
        if (res != VK_SUCCESS)
        {
            Core::Log::Error("DescriptorAllocator: vkCreateDescriptorPool failed ({})", static_cast<int>(res));
            m_IsValid = false;
            return VK_NULL_HANDLE;
        }

        return pool;
    }

    VkDescriptorSet DescriptorAllocator::Allocate(VkDescriptorSetLayout layout)
    {
        if (!m_IsValid)
        {
            return VK_NULL_HANDLE;
        }

        if (layout == VK_NULL_HANDLE)
        {
            Core::Log::Error("DescriptorAllocator: Allocate called with null layout");
            return VK_NULL_HANDLE;
        }

        if (m_CurrentPool == VK_NULL_HANDLE)
        {
            m_CurrentPool = GrabPool();
            if (m_CurrentPool == VK_NULL_HANDLE)
            {
                return VK_NULL_HANDLE;
            }
            m_UsedPools.push_back(m_CurrentPool);
        }

        VkDescriptorSetAllocateInfo allocInfo{  };
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_CurrentPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &layout;

        VkDescriptorSet set = VK_NULL_HANDLE;
        VkResult result = vkAllocateDescriptorSets(m_Device.GetLogicalDevice(), &allocInfo, &set);

        // Pool exhausted/fragmented: rotate to a new pool and retry.
        if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL)
        {
            m_CurrentPool = GrabPool();
            if (m_CurrentPool == VK_NULL_HANDLE)
            {
                return VK_NULL_HANDLE;
            }
            m_UsedPools.push_back(m_CurrentPool);

            allocInfo.descriptorPool = m_CurrentPool;
            set = VK_NULL_HANDLE;
            result = vkAllocateDescriptorSets(m_Device.GetLogicalDevice(), &allocInfo, &set);
        }

        if (result != VK_SUCCESS)
        {
            Core::Log::Error("DescriptorAllocator: vkAllocateDescriptorSets failed ({})", static_cast<int>(result));
            return VK_NULL_HANDLE;
        }

        return set;
    }

    void DescriptorAllocator::Reset()
    {

         const VkDevice logicalDevice = m_Device.GetLogicalDevice();

         for (VkDescriptorPool p : m_UsedPools)
         {
             vkResetDescriptorPool(logicalDevice, p, 0);
             m_FreePools.push_back(p);
         }

         m_UsedPools.clear();
         m_CurrentPool = VK_NULL_HANDLE;
     }
}
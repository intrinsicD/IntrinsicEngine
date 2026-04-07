module;
#include "RHI.Vulkan.hpp"
#include "RHI.DestructionUtils.hpp"
#include <vector>

module RHI.Descriptors;
import RHI.Device;
import Core.Logging;

namespace RHI {

    // --- Descriptor Layout ---
    DescriptorLayout::DescriptorLayout(VulkanDevice& device) : m_Device(device) {
        VkDescriptorSetLayoutBinding uboBinding{};
        uboBinding.binding = 0;
        uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        uboBinding.descriptorCount = 1;
        uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        // Binding 1: shadow atlas comparison sampler (PCF shadow mapping).
        // Uses VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT so the shadow atlas
        // view can be updated between frames while a previous frame's command
        // buffer (which references this same descriptor set) is still pending on
        // the GPU. Without this flag vkUpdateDescriptorSets fires VUID-03047.
        VkDescriptorSetLayoutBinding shadowSamplerBinding{};
        shadowSamplerBinding.binding = 1;
        shadowSamplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        shadowSamplerBinding.descriptorCount = 1;
        shadowSamplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        const VkDescriptorSetLayoutBinding bindings[] = { uboBinding, shadowSamplerBinding };

        // Per-binding flags: binding 0 is plain; binding 1 is update-after-bind.
        const VkDescriptorBindingFlags bindingFlags[] = {
            0,
            VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
        };
        VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{};
        flagsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
        flagsInfo.bindingCount = static_cast<uint32_t>(std::size(bindingFlags));
        flagsInfo.pBindingFlags = bindingFlags;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        // Required whenever any binding carries UPDATE_AFTER_BIND_BIT.
        layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
        layoutInfo.bindingCount = static_cast<uint32_t>(std::size(bindings));
        layoutInfo.pBindings = bindings;
        layoutInfo.pNext = &flagsInfo;

        if (vkCreateDescriptorSetLayout(m_Device.GetLogicalDevice(), &layoutInfo, nullptr, &m_Layout) != VK_SUCCESS) {
            Core::Log::Error("Failed to create descriptor set layout!");
            m_IsValid = false;
        }
    }

    DescriptorLayout::~DescriptorLayout() {
        DestructionUtils::SafeDestroyVk(m_Device, m_Layout, vkDestroyDescriptorSetLayout);
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
        // VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT is required to allocate
        // descriptor sets whose layout carries VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT
        // (e.g. the global set with the shadow atlas binding 1).
        // It is harmless for sets that do not use update-after-bind bindings.
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
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
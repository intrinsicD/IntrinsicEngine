module;
#include "RHI.Vulkan.hpp"
#include <memory>
#include <algorithm>
#include <mutex>
#include <vector>
#include <iostream>

module RHI:Bindless.Impl;
import :Bindless;
import :Device;
import :Texture;
import Core.Logging;

namespace RHI
{
    BindlessDescriptorSystem::BindlessDescriptorSystem(VulkanDevice& device)
        : m_Device(device)
    {
        CreateLayout();
        CreatePoolAndSet();
        m_PendingUpdates.reserve(1024);
    }

    BindlessDescriptorSystem::~BindlessDescriptorSystem()
    {
        vkDestroyDescriptorPool(m_Device.GetLogicalDevice(), m_Pool, nullptr);
        vkDestroyDescriptorSetLayout(m_Device.GetLogicalDevice(), m_Layout, nullptr);
    }

    void BindlessDescriptorSystem::CreateLayout()
    {
        VkPhysicalDeviceProperties2 props2{};
        props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        VkPhysicalDeviceDescriptorIndexingProperties indexingProps{};
        indexingProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES;
        props2.pNext = &indexingProps;

        if (vkGetPhysicalDeviceProperties2 == nullptr) {
            Core::Log::Error("CRITICAL: vkGetPhysicalDeviceProperties2 is NULL. Vulkan 1.1+ not loaded correctly via Volk.");
            std::abort();
        }

        vkGetPhysicalDeviceProperties2(m_Device.GetPhysicalDevice(), &props2);

        uint32_t hwLimit = indexingProps.maxDescriptorSetUpdateAfterBindSampledImages;

        // FAILSAFE: If the query failed (hwLimit == 0), default to a safe value.
        // This prevents vkCreateDescriptorPool from crashing on size 0.
        if (hwLimit == 0) {
            Core::Log::Warn("[Bindless] Hardware reported 0 descriptors. This usually indicates a driver issue or missing extension. Defaulting to 4096.");
            hwLimit = 4096;
        }

        // Clamp to 64k or HW limit
        m_MaxDescriptors = std::min(hwLimit, 65536u);

        Core::Log::Info("Bindless System: Allocating {} slots (HW Limit: {}).",
                        m_MaxDescriptors, hwLimit);
        std::cout << std::flush;

        // Binding 0: Bindless Texture Array
        VkDescriptorSetLayoutBinding textureBinding{};
        textureBinding.binding = 0;
        textureBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        textureBinding.descriptorCount = m_MaxDescriptors;
        textureBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        // Allows array to be not fully filled
        // Allows shaders to index dynamically
        // Allows updating while bound (UPDATE_AFTER_BIND)
        // PARTIALLY_BOUND is critical!
        VkDescriptorBindingFlags flags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
            VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;

        VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlags{};
        bindingFlags.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
        bindingFlags.bindingCount = 1;
        bindingFlags.pBindingFlags = &flags;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &textureBinding;
        layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
        layoutInfo.pNext = &bindingFlags;

        VK_CHECK(vkCreateDescriptorSetLayout(m_Device.GetLogicalDevice(), &layoutInfo, nullptr, &m_Layout));
    }

    void BindlessDescriptorSystem::CreatePoolAndSet()
    {
        VkDescriptorPoolSize poolSize;
        poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSize.descriptorCount = m_MaxDescriptors;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT; // Critical
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        poolInfo.maxSets = 1;

        VK_CHECK(vkCreateDescriptorPool(m_Device.GetLogicalDevice(), &poolInfo, nullptr, &m_Pool));

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_Pool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_Layout;

        VK_CHECK(vkAllocateDescriptorSets(m_Device.GetLogicalDevice(), &allocInfo, &m_GlobalSet));
    }

    void BindlessDescriptorSystem::SetTexture(uint32_t index, const Texture& texture)
    {
        EnqueueUpdate(index, texture.GetView(), texture.GetSampler(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    void BindlessDescriptorSystem::EnqueueUpdate(uint32_t index, VkImageView view, VkSampler sampler, VkImageLayout layout)
    {
        if (index >= m_MaxDescriptors)
        {
            Core::Log::Error("Bindless update out of bounds: {} >= {}", index, m_MaxDescriptors);
            return;
        }

        // Vulkan validation: writing VK_NULL_HANDLE into COMBINED_IMAGE_SAMPLER is illegal unless nullDescriptor is enabled.
        if (view == VK_NULL_HANDLE || sampler == VK_NULL_HANDLE)
        {
            Core::Log::Warn("[Bindless] Ignoring EnqueueUpdate({}, view={}, sampler={}) - null handles are not writable without nullDescriptor.",
                           index, static_cast<void*>(view), static_cast<void*>(sampler));
            return;
        }

        std::lock_guard lock(m_UpdateMutex);
        m_PendingUpdates.push_back({index, view, sampler, layout});
    }

    void BindlessDescriptorSystem::FlushPending()
    {
        std::vector<PendingUpdate> updatesToApply;
        {
            std::lock_guard lock(m_UpdateMutex);
            if (m_PendingUpdates.empty())
                return;
            updatesToApply.swap(m_PendingUpdates);
        }

        std::vector<VkDescriptorImageInfo> imageInfos;
        imageInfos.reserve(updatesToApply.size());

        std::vector<VkWriteDescriptorSet> writes;
        writes.reserve(updatesToApply.size());

        for (const auto& update : updatesToApply)
        {
            imageInfos.push_back({update.Sampler, update.View, update.Layout});

            VkWriteDescriptorSet descriptorWrite{};
            descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrite.dstSet = m_GlobalSet;
            descriptorWrite.dstBinding = 0;
            descriptorWrite.dstArrayElement = update.Index;
            descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrite.descriptorCount = 1;
            descriptorWrite.pImageInfo = &imageInfos.back();

            writes.push_back(descriptorWrite);
        }

        vkUpdateDescriptorSets(m_Device.GetLogicalDevice(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    void BindlessDescriptorSystem::UnregisterTexture(uint32_t index)
    {
        // Do NOT write VK_NULL_HANDLE unless VK_EXT_robustness2/nullDescriptor is enabled.
        // Higher-level code should keep a valid default bound for any potentially-sampled index.
        if (index >= m_MaxDescriptors) return;
    }
}

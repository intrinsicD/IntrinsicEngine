module;
#include "RHI.Vulkan.hpp"
#include <memory>
#include <algorithm>
#include <mutex>
#include <iostream>

module RHI:Bindless.Impl;
import :Bindless;

namespace RHI
{
    BindlessDescriptorSystem::BindlessDescriptorSystem(std::shared_ptr<VulkanDevice> device)
        : m_Device(device)
    {
        CreateLayout();
        CreatePoolAndSet();
    }

    BindlessDescriptorSystem::~BindlessDescriptorSystem()
    {
        vkDestroyDescriptorPool(m_Device->GetLogicalDevice(), m_Pool, nullptr);
        vkDestroyDescriptorSetLayout(m_Device->GetLogicalDevice(), m_Layout, nullptr);
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

        vkGetPhysicalDeviceProperties2(m_Device->GetPhysicalDevice(), &props2);

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

        VK_CHECK(vkCreateDescriptorSetLayout(m_Device->GetLogicalDevice(), &layoutInfo, nullptr, &m_Layout));
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

        VK_CHECK(vkCreateDescriptorPool(m_Device->GetLogicalDevice(), &poolInfo, nullptr, &m_Pool));

        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_Pool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_Layout;

        VK_CHECK(vkAllocateDescriptorSets(m_Device->GetLogicalDevice(), &allocInfo, &m_GlobalSet));
    }

    uint32_t BindlessDescriptorSystem::RegisterTexture(const Texture& texture)
    {
        uint32_t index;
        {
            std::lock_guard lock(m_Mutex);
            if (!m_FreeSlots.empty())
            {
                index = m_FreeSlots.front();
                m_FreeSlots.pop();
            }
            else
            {
                if (m_HighWaterMark >= m_MaxDescriptors)
                {
                    Core::Log::Error("Bindless Texture Limit Reached!");
                    return 0; // Return dummy slot 0
                }
                index = m_HighWaterMark++;
            }
        }

        UpdateTexture(index, texture);
        return index;
    }

    void BindlessDescriptorSystem::UpdateTexture(uint32_t index, const Texture& texture)
    {
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = texture.GetView();
        imageInfo.sampler = texture.GetSampler();

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = m_GlobalSet;
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = index; // The magic index
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(m_Device->GetLogicalDevice(), 1, &descriptorWrite, 0, nullptr);
    }

    void BindlessDescriptorSystem::UnregisterTexture(uint32_t index)
    {
        m_Device->SafeDestroy([weak = weak_from_this(), index]()
        {
            if (auto self = weak.lock())
            {
                std::lock_guard lock(self->m_Mutex);
                self->m_FreeSlots.push(index);
            }
        });
    }
}

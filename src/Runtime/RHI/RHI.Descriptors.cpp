module;
#include "RHI/RHI.Vulkan.hpp"
#include <vector>
#include <memory>

module Runtime.RHI.Descriptors;
import Core.Logging;

namespace Runtime::RHI {

    // --- Descriptor Layout ---
    DescriptorLayout::DescriptorLayout(std::shared_ptr<VulkanDevice> device) : m_Device(device) {
        VkDescriptorSetLayoutBinding uboBinding{};
        uboBinding.binding = 0;
        uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        uboBinding.descriptorCount = 1;
        uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &uboBinding;

        if (vkCreateDescriptorSetLayout(m_Device->GetLogicalDevice(), &layoutInfo, nullptr, &m_Layout) != VK_SUCCESS) {
            Core::Log::Error("Failed to create descriptor set layout!");
            m_IsValid = false;
        }
    }

    DescriptorLayout::~DescriptorLayout() {
        vkDestroyDescriptorSetLayout(m_Device->GetLogicalDevice(), m_Layout, nullptr);
    }

    // --- Descriptor Pool ---
    DescriptorPool::DescriptorPool(std::shared_ptr<VulkanDevice> device) : m_Device(device) {
        // We allocate enough space for a few frames.
        VkDescriptorPoolSize uboPoolSize;
        uboPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        uboPoolSize.descriptorCount = 100;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &uboPoolSize;
        poolInfo.maxSets = 100; 

        if (vkCreateDescriptorPool(m_Device->GetLogicalDevice(), &poolInfo, nullptr, &m_Pool) != VK_SUCCESS) {
            Core::Log::Error("Failed to create descriptor pool!");
            m_IsValid = false;
        }
    }

    DescriptorPool::~DescriptorPool() {
        vkDestroyDescriptorPool(m_Device->GetLogicalDevice(), m_Pool, nullptr);
    }

    VkDescriptorSet DescriptorPool::Allocate(VkDescriptorSetLayout layout) {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_Pool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &layout;

        VkDescriptorSet set;
        if (vkAllocateDescriptorSets(m_Device->GetLogicalDevice(), &allocInfo, &set) != VK_SUCCESS) {
            Core::Log::Error("Failed to allocate descriptor set!");
            return VK_NULL_HANDLE;
        }
        return set;
    }
}
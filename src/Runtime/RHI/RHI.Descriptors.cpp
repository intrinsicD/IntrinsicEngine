module;
#include "RHI/RHI.Vulkan.hpp"
#include <vector>
#include <memory>

module Runtime.RHI.Descriptors;
import Core.Logging;

namespace Runtime::RHI {

    // --- Descriptor Layout ---
    DescriptorLayout::DescriptorLayout(std::shared_ptr<VulkanDevice> device) : m_Device(device) {
        std::vector<VkDescriptorSetLayoutBinding> bindings(2);

        // 0: UBO
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        // 1: Texture Sampler
        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings[1].pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 2;
        layoutInfo.pBindings = bindings.data();

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
        std::vector<VkDescriptorPoolSize> poolSizes(2);
        poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        poolSizes[0].descriptorCount = 100;

        poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSizes[1].descriptorCount = 100;

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = 2;
        poolInfo.pPoolSizes = poolSizes.data();
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
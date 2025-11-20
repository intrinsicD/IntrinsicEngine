module;
#include "RHI/RHI.Vulkan.hpp"

module Runtime.RHI.Descriptors;
import Core.Logging;

namespace Runtime::RHI {

    // --- Descriptor Layout ---
    DescriptorLayout::DescriptorLayout(VulkanDevice& device) : m_Device(device) {
        // Define Binding 0: Vertex Shader Uniform Buffer
        VkDescriptorSetLayoutBinding uboLayoutBinding{};
        uboLayoutBinding.binding = 0;
        uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboLayoutBinding.descriptorCount = 1;
        uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        uboLayoutBinding.pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &uboLayoutBinding;

        if (vkCreateDescriptorSetLayout(m_Device.GetLogicalDevice(), &layoutInfo, nullptr, &m_Layout) != VK_SUCCESS) {
            Core::Log::Error("Failed to create descriptor set layout!");
        }
    }

    DescriptorLayout::~DescriptorLayout() {
        vkDestroyDescriptorSetLayout(m_Device.GetLogicalDevice(), m_Layout, nullptr);
    }

    // --- Descriptor Pool ---
    DescriptorPool::DescriptorPool(VulkanDevice& device) : m_Device(device) {
        // We allocate enough space for a few frames.
        VkDescriptorPoolSize poolSize{};
        poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSize.descriptorCount = 100; // Arbitrary size for sandbox

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        poolInfo.maxSets = 100; 

        if (vkCreateDescriptorPool(m_Device.GetLogicalDevice(), &poolInfo, nullptr, &m_Pool) != VK_SUCCESS) {
            Core::Log::Error("Failed to create descriptor pool!");
        }
    }

    DescriptorPool::~DescriptorPool() {
        vkDestroyDescriptorPool(m_Device.GetLogicalDevice(), m_Pool, nullptr);
    }

    VkDescriptorSet DescriptorPool::Allocate(VkDescriptorSetLayout layout) {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = m_Pool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &layout;

        VkDescriptorSet set;
        if (vkAllocateDescriptorSets(m_Device.GetLogicalDevice(), &allocInfo, &set) != VK_SUCCESS) {
            Core::Log::Error("Failed to allocate descriptor set!");
            return VK_NULL_HANDLE;
        }
        return set;
    }
}
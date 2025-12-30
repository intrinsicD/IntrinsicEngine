module;
#include "RHI.Vulkan.hpp"
#include <memory>

export module RHI:Descriptors;

import :Device;

export namespace RHI
{
    class DescriptorLayout
    {
    public:
        DescriptorLayout(std::shared_ptr<VulkanDevice> device);
        ~DescriptorLayout();

        [[nodiscard]] VkDescriptorSetLayout GetHandle() const { return m_Layout; }
        [[nodiscard]] bool IsValid() const { return m_IsValid; }

    private:
        std::shared_ptr<VulkanDevice> m_Device;
        bool m_IsValid = true;
        VkDescriptorSetLayout m_Layout = VK_NULL_HANDLE;
    };

    class DescriptorPool
    {
    public:
        DescriptorPool(std::shared_ptr<VulkanDevice> device);
        ~DescriptorPool();

        // Allocate a single set from the pool using the given layout
        [[nodiscard]] VkDescriptorSet Allocate(VkDescriptorSetLayout layout);
        [[nodiscard]] bool IsValid() const { return m_IsValid; }

    private:
        std::shared_ptr<VulkanDevice> m_Device;
        bool m_IsValid = true;
        VkDescriptorPool m_Pool = VK_NULL_HANDLE;
    };
}

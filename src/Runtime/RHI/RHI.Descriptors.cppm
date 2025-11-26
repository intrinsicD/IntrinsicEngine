module;
#include <RHI/RHI.Vulkan.hpp>
#include <memory>

export module Runtime.RHI.Descriptors;

import Runtime.RHI.Device;

export namespace Runtime::RHI
{
    class DescriptorLayout
    {
    public:
        DescriptorLayout(std::shared_ptr<VulkanDevice> device);
        ~DescriptorLayout();

        [[nodiscard]] VkDescriptorSetLayout GetHandle() const { return m_Layout; }

    private:
        std::shared_ptr<VulkanDevice> m_Device;
        VkDescriptorSetLayout m_Layout = VK_NULL_HANDLE;
    };

    class DescriptorPool
    {
    public:
        DescriptorPool(std::shared_ptr<VulkanDevice> device);
        ~DescriptorPool();

        // Allocate a single set from the pool using the given layout
        [[nodiscard]] VkDescriptorSet Allocate(VkDescriptorSetLayout layout);

    private:
        std::shared_ptr<VulkanDevice> m_Device;
        VkDescriptorPool m_Pool = VK_NULL_HANDLE;
    };
}

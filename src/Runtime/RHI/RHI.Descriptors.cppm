module;
#include <RHI/RHI.Vulkan.hpp>
#include <vector>

export module Runtime.RHI.Descriptors;

import Runtime.RHI.Device;

export namespace Runtime::RHI
{
    class DescriptorLayout
    {
    public:
        DescriptorLayout(VulkanDevice& device);
        ~DescriptorLayout();

        [[nodiscard]] VkDescriptorSetLayout GetHandle() const { return m_Layout; }

    private:
        VulkanDevice& m_Device;
        VkDescriptorSetLayout m_Layout = VK_NULL_HANDLE;
    };

    class DescriptorPool
    {
    public:
        DescriptorPool(VulkanDevice& device);
        ~DescriptorPool();

        // Allocate a single set from the pool using the given layout
        [[nodiscard]] VkDescriptorSet Allocate(VkDescriptorSetLayout layout);

    private:
        VulkanDevice& m_Device;
        VkDescriptorPool m_Pool = VK_NULL_HANDLE;
    };
}

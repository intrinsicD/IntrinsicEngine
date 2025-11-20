module;
#include "RHI.Vulkan.hpp"

export module Runtime.RHI.Image;

import Runtime.RHI.Device;

export namespace Runtime::RHI
{
    class VulkanImage
    {
    public:
        VulkanImage(VulkanDevice& device, uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage,
                    VkImageAspectFlags aspect);
        ~VulkanImage();

        [[nodiscard]] VkImage GetHandle() const { return m_Image; }
        [[nodiscard]] VkImageView GetView() const { return m_ImageView; }
        [[nodiscard]] VkFormat GetFormat() const { return m_Format; }

        // Helper to find a supported depth format
        static VkFormat FindDepthFormat(VulkanDevice& device);

    private:
        VulkanDevice& m_Device;
        VkImage m_Image = VK_NULL_HANDLE;
        VkImageView m_ImageView = VK_NULL_HANDLE;
        VmaAllocation m_Allocation = VK_NULL_HANDLE;
        VkFormat m_Format;
    };
}

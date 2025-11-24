module;
#include "RHI.Vulkan.hpp"

export module Runtime.RHI.Image;

import Runtime.RHI.Device;

export namespace Runtime::RHI
{
    class VulkanImage
    {
    public:
        VulkanImage(VulkanDevice& device, uint32_t width, uint32_t height, uint32_t mipLevels, VkFormat format, VkImageUsageFlags usage,
                    VkImageAspectFlags aspect);
        ~VulkanImage();

        [[nodiscard]] VkImage GetHandle() const { return m_Image; }
        [[nodiscard]] VkImageView GetView() const { return m_ImageView; }
        [[nodiscard]] VkFormat GetFormat() const { return m_Format; }
        [[nodiscard]] uint32_t GetMipLevels() const { return m_MipLevels; }

        [[nodiscard]] uint32_t GetWidth() const{ return m_Width; }
        [[nodiscard]] uint32_t GetHeight() const{ return m_Height; }

        // Helper to find a supported depth format
        static VkFormat FindDepthFormat(VulkanDevice& device);

    private:
        VulkanDevice& m_Device;
        VkImage m_Image = VK_NULL_HANDLE;
        VkImageView m_ImageView = VK_NULL_HANDLE;
        VmaAllocation m_Allocation = VK_NULL_HANDLE;
        VkFormat m_Format;
        uint32_t m_MipLevels = 1;
        uint32_t m_Width = 0;
        uint32_t m_Height = 0;
    };
}

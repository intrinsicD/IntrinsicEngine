module;
#include "RHI.Vulkan.hpp"
#include <vector>

module Runtime.RHI.Image;
import Core.Logging;

namespace Runtime::RHI
{
    VulkanImage::VulkanImage(VulkanDevice& device, uint32_t width, uint32_t height, uint32_t mipLevels, VkFormat format,
                             VkImageUsageFlags usage, VkImageAspectFlags aspect)
        : m_Device(device), m_Format(format), m_MipLevels(mipLevels), m_Width(width), m_Height(height)
    {
        // 1. Create Image
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent.width = width;
        imageInfo.extent.height = height;
        imageInfo.extent.depth = 1;
        imageInfo.mipLevels = m_MipLevels;
        imageInfo.arrayLayers = 1;
        imageInfo.format = format;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL; // GPU-optimal layout
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = usage;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        allocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

        if (vmaCreateImage(device.GetAllocator(), &imageInfo, &allocInfo, &m_Image, &m_Allocation, nullptr) !=
            VK_SUCCESS)
        {
            Core::Log::Error("Failed to create image!");
            return;
        }

        // 2. Create View
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_Image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = aspect;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = m_MipLevels;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device.GetLogicalDevice(), &viewInfo, nullptr, &m_ImageView) != VK_SUCCESS)
        {
            Core::Log::Error("Failed to create image view!");
        }
    }

    VulkanImage::~VulkanImage()
    {
        vkDestroyImageView(m_Device.GetLogicalDevice(), m_ImageView, nullptr);
        vmaDestroyImage(m_Device.GetAllocator(), m_Image, m_Allocation);
    }

    VkFormat VulkanImage::FindDepthFormat(VulkanDevice& device)
    {
        std::vector<VkFormat> candidates = {
            VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT
        };
        for (VkFormat format : candidates)
        {
            VkFormatProperties props;
            vkGetPhysicalDeviceFormatProperties(device.GetPhysicalDevice(), format, &props);
            if ((props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) ==
                VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
            {
                return format;
            }
        }
        Core::Log::Error("Failed to find supported depth format!");
        return VK_FORMAT_UNDEFINED;
    }
}

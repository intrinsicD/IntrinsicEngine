module;
#include "RHI.Vulkan.hpp"
#include <vector>
#include <memory>
#include <optional>

module Runtime.RHI.Image;
import Core.Logging;

namespace Runtime::RHI
{
    VulkanImage::VulkanImage(std::shared_ptr<VulkanDevice> device, uint32_t width, uint32_t height, uint32_t mipLevels, VkFormat format,
                             VkImageUsageFlags usage, VkImageAspectFlags aspect, VkSharingMode sharingMode)
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
        imageInfo.sharingMode = sharingMode;

        std::vector<uint32_t> queueIndices;
        if (sharingMode == VK_SHARING_MODE_CONCURRENT) {
            auto indices = m_Device->GetQueueIndices();
            if (indices.GraphicsFamily.has_value())
            {
                queueIndices.push_back(indices.GraphicsFamily.value());
            }
            // Avoid duplicate if transfer == graphics
            if (indices.TransferFamily.has_value() && indices.TransferFamily != indices.GraphicsFamily) {
                queueIndices.push_back(indices.TransferFamily.value());
            }
            imageInfo.queueFamilyIndexCount = static_cast<uint32_t>(queueIndices.size());
            imageInfo.pQueueFamilyIndices = queueIndices.data();
        }

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        allocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

        if (vmaCreateImage(device->GetAllocator(), &imageInfo, &allocInfo, &m_Image, &m_Allocation, nullptr) !=
            VK_SUCCESS)
        {
            Core::Log::Error("Failed to create image!");
            m_IsValid = false;
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

        if (vkCreateImageView(device->GetLogicalDevice(), &viewInfo, nullptr, &m_ImageView) != VK_SUCCESS)
        {
            Core::Log::Error("Failed to create image view!");
            m_IsValid = false;
            return;
        }
    }

    VulkanImage::~VulkanImage()
    {
        VkDevice logicalDevice = m_Device->GetLogicalDevice();
        VmaAllocator allocator = m_Device->GetAllocator();

        if (m_ImageView)
        {
            VkImageView view = m_ImageView;
            m_Device->SafeDestroy([logicalDevice, view]()
            {
                vkDestroyImageView(logicalDevice, view, nullptr);
            });
        }

        if (m_Image)
        {
            VkImage image = m_Image;
            VmaAllocation allocation = m_Allocation;
            m_Device->SafeDestroy([allocator, image, allocation]()
            {
                vmaDestroyImage(allocator, image, allocation);
            });
        }
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

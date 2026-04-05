module;
#include "RHI.Vulkan.hpp"
#include "RHI.DestructionUtils.hpp"
#include <vector>
#include <optional>
#include <utility>

module RHI.Image;
import RHI.Device;
import Core.Logging;

namespace RHI
{
    VulkanImage::VulkanImage(VulkanDevice& device, uint32_t width, uint32_t height, uint32_t mipLevels, VkFormat format,
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
        if (sharingMode == VK_SHARING_MODE_CONCURRENT)
        {
            auto indices = m_Device.GetQueueIndices();
            queueIndices.push_back(indices.Graphics());
            if (indices.HasDistinctTransfer())
            {
                queueIndices.push_back(indices.Transfer());
            }
            imageInfo.queueFamilyIndexCount = static_cast<uint32_t>(queueIndices.size());
            imageInfo.pQueueFamilyIndices = queueIndices.data();
        }

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        allocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

        if (vmaCreateImage(device.GetAllocator(), &imageInfo, &allocInfo, &m_Image, &m_Allocation, nullptr) !=
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

        if (vkCreateImageView(device.GetLogicalDevice(), &viewInfo, nullptr, &m_ImageView) != VK_SUCCESS)
        {
            Core::Log::Error("Failed to create image view!");
            m_IsValid = false;
            return;
        }
    }

    VulkanImage::VulkanImage(VulkanDevice& device, uint32_t width, uint32_t height, VkFormat format)
        : m_Device(device), m_Format(format), m_Width(width), m_Height(height), m_IsValid(true), m_OwnsMemory(false)
    {
    }

    // 4. Move Constructor (CRITICAL FIX)
    VulkanImage::VulkanImage(VulkanImage&& other) noexcept
        : m_Device(other.m_Device)
          , m_Image(std::exchange(other.m_Image, VK_NULL_HANDLE))
          , m_ImageView(std::exchange(other.m_ImageView, VK_NULL_HANDLE))
          , m_Allocation(std::exchange(other.m_Allocation, VK_NULL_HANDLE))
          , m_Format(other.m_Format)
          , m_MipLevels(other.m_MipLevels)
          , m_Width(other.m_Width)
          , m_Height(other.m_Height)
          , m_IsValid(other.m_IsValid)
          , m_OwnsMemory(other.m_OwnsMemory)
    {
    }

    VulkanImage& VulkanImage::operator=(VulkanImage&& other) noexcept
    {
        if (this != &other)
        {
            // Release current resources (if any)
            DestructionUtils::SafeDestroyVk(m_Device, m_ImageView, vkDestroyImageView);
            if (m_OwnsMemory)
                DestructionUtils::SafeDestroyVma(m_Device, m_Image, m_Allocation, vmaDestroyImage);
            else
                DestructionUtils::SafeDestroyVk(m_Device, m_Image, vkDestroyImage);
            m_OwnsMemory = false;

            // Move data
            // Note: m_Device is a reference, cannot be reassigned.
            m_Image = std::exchange(other.m_Image, VK_NULL_HANDLE);
            m_ImageView = std::exchange(other.m_ImageView, VK_NULL_HANDLE);
            m_Allocation = std::exchange(other.m_Allocation, VK_NULL_HANDLE);
            m_Format = other.m_Format;
            m_MipLevels = other.m_MipLevels;
            m_Width = other.m_Width;
            m_Height = other.m_Height;
            m_IsValid = other.m_IsValid;
            m_OwnsMemory = other.m_OwnsMemory;

            // Leave the moved-from object in a consistent inert state.
            other.m_OwnsMemory = false;
            other.m_IsValid = false;
        }
        return *this;
    }

    VulkanImage::~VulkanImage()
    {
        // 1. Always destroy the Image View
        DestructionUtils::SafeDestroyVk(m_Device, m_ImageView, vkDestroyImageView);

        // 2. Destroy the Image Handle (and Memory if owned)
        if (m_OwnsMemory)
        {
            // CASE A: Standard VMA Allocation — vmaDestroyImage frees both VkImage and allocation.
            DestructionUtils::SafeDestroyVma(m_Device, m_Image, m_Allocation, vmaDestroyImage);
        }
        else
        {
            // CASE B: Aliased / Unbound (RenderGraph) — destroy VkImage only, memory is external.
            DestructionUtils::SafeDestroyVk(m_Device, m_Image, vkDestroyImage);
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

    VulkanImage VulkanImage::CreateUnbound(VulkanDevice& device, uint32_t width, uint32_t height,
                                           VkFormat format, VkImageUsageFlags usage)
    {
        // Use private constructor to initialize clean state
        VulkanImage img(device, width, height, format);

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = {width, height, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = format;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = usage;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

        // ALIAS_BIT is required for memory aliasing
        imageInfo.flags = VK_IMAGE_CREATE_ALIAS_BIT;

        if (vkCreateImage(device.GetLogicalDevice(), &imageInfo, nullptr, &img.m_Image) != VK_SUCCESS)
        {
            Core::Log::Error("Failed to create unbound vkImage");
            img.m_IsValid = false;
        }

        return img; // RVO or Move
    }

    VkMemoryRequirements VulkanImage::GetMemoryRequirements() const
    {
        VkMemoryRequirements memReqs{};
        if (m_Image != VK_NULL_HANDLE)
            vkGetImageMemoryRequirements(m_Device.GetLogicalDevice(), m_Image, &memReqs);
        return memReqs;
    }

    void VulkanImage::BindMemory(VkDeviceMemory memory, VkDeviceSize offset)
    {
        if (vkBindImageMemory(m_Device.GetLogicalDevice(), m_Image, memory, offset) != VK_SUCCESS)
        {
            Core::Log::Error("VulkanImage::BindMemory: vkBindImageMemory failed");
            m_IsValid = false;
            return;
        }

        // Create view AFTER memory is bound
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_Image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_Format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; // Simplified
        if (m_Format == VK_FORMAT_D32_SFLOAT || m_Format == VK_FORMAT_D24_UNORM_S8_UINT)
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(m_Device.GetLogicalDevice(), &viewInfo, nullptr, &m_ImageView) != VK_SUCCESS)
        {
            Core::Log::Error("Failed to create bound image view");
        }
    }
}

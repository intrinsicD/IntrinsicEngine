module;
#include "RHI.Vulkan.hpp"

export module RHI:Image;

import :Device;

export namespace RHI
{
    class VulkanImage
    {
    public:
        VulkanImage(VulkanDevice& device, uint32_t width, uint32_t height, uint32_t mipLevels,
                    VkFormat format, VkImageUsageFlags usage,
                    VkImageAspectFlags aspect, VkSharingMode sharingMode = VK_SHARING_MODE_EXCLUSIVE);
        ~VulkanImage();

        VulkanImage(const VulkanImage&) = delete;
        VulkanImage& operator=(const VulkanImage&) = delete;

        VulkanImage(VulkanImage&& other) noexcept;
        VulkanImage& operator=(VulkanImage&& other) noexcept;

        [[nodiscard]] VkImage GetHandle() const { return m_Image; }
        [[nodiscard]] VkImageView GetView() const { return m_ImageView; }
        [[nodiscard]] VkFormat GetFormat() const { return m_Format; }
        [[nodiscard]] uint32_t GetMipLevels() const { return m_MipLevels; }

        [[nodiscard]] uint32_t GetWidth() const { return m_Width; }
        [[nodiscard]] uint32_t GetHeight() const { return m_Height; }
        [[nodiscard]] bool IsValid() const { return m_IsValid; }

        // Helper to find a supported depth format
        static VkFormat FindDepthFormat(VulkanDevice& device);

        // --- NEW: Aliasing Support ---

        // 1. Create the Handle ONLY (No memory)
        static VulkanImage CreateUnbound(VulkanDevice& device, uint32_t width, uint32_t height,
                                         VkFormat format, VkImageUsageFlags usage);

        // 2. Query what this image needs
        [[nodiscard]] VkMemoryRequirements GetMemoryRequirements() const;

        // 3. Bind to existing memory (The "Aliasing" magic)
        // We use a raw VkDeviceMemory + offset here, or a VmaAllocationInfo
        void BindMemory(VkDeviceMemory memory, VkDeviceSize offset);

    private:
        VulkanImage(VulkanDevice& device, uint32_t width, uint32_t height, VkFormat format);

        VulkanDevice& m_Device;
        VkImage m_Image = VK_NULL_HANDLE;
        VkImageView m_ImageView = VK_NULL_HANDLE;
        VmaAllocation m_Allocation = VK_NULL_HANDLE;
        VkFormat m_Format;
        uint32_t m_MipLevels = 1;
        uint32_t m_Width = 0;
        uint32_t m_Height = 0;
        bool m_IsValid = true;
        bool m_OwnsMemory = true;
    };
}

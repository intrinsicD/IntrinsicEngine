module;
#include "RHI.Vulkan.hpp"
#include <string>
#include <vector>
#include <memory>

export module RHI:Texture;

import :Device;
import :Image;

export namespace RHI
{
    class Texture
    {
    public:
        Texture(VulkanDevice& device, const std::string& filepath);
        Texture(VulkanDevice& device, const std::vector<uint8_t>& data, uint32_t width,
                uint32_t height);
        Texture(VulkanDevice& device, uint32_t width, uint32_t height, VkFormat format);
        ~Texture();

        [[nodiscard]] VkImage GetImage() const { return m_Image->GetHandle(); }
        [[nodiscard]] VkImageView GetView() const { return m_Image->GetView(); }
        [[nodiscard]] VkSampler GetSampler() const { return m_Sampler; }

    private:
        VulkanDevice& m_Device;
        std::unique_ptr<VulkanImage> m_Image;
        VkSampler m_Sampler = VK_NULL_HANDLE;

        void CreateSampler();
        void Upload(const void* data, uint32_t width, uint32_t height);
    };
}

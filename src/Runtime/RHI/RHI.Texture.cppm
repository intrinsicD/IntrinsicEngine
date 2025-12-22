module;
#include <RHI/RHI.Vulkan.hpp>
#include <string>
#include <memory>
#include <vector>

export module Runtime.RHI.Texture;

import Runtime.RHI.Device;
import Runtime.RHI.Image;

export namespace Runtime::RHI {

    class Texture {
    public:
        Texture(std::shared_ptr<VulkanDevice> device, const std::string& filepath);
        Texture(std::shared_ptr<VulkanDevice> device, const std::vector<uint8_t>& data, uint32_t width, uint32_t height);
        Texture(std::shared_ptr<VulkanDevice> device, uint32_t width, uint32_t height, VkFormat format);
        ~Texture();

        [[nodiscard]] VkImage GetImage() const { return m_Image->GetHandle(); }
        [[nodiscard]] VkImageView GetView() const { return m_Image->GetView(); }
        [[nodiscard]] VkSampler GetSampler() const { return m_Sampler; }

    private:
        std::shared_ptr<VulkanDevice> m_Device;
        std::unique_ptr<VulkanImage> m_Image;
        VkSampler m_Sampler = VK_NULL_HANDLE;

        void CreateSampler();
        void Upload(const void* data, uint32_t width, uint32_t height);
    };
}
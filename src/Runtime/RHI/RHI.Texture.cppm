module;
#include <RHI/RHI.Vulkan.hpp>
#include <string>

export module Runtime.RHI.Texture;

import Runtime.RHI.Device;
import Runtime.RHI.Image;

export namespace Runtime::RHI {

    class Texture {
    public:
        Texture(VulkanDevice& device, const std::string& filepath);
        ~Texture();

        [[nodiscard]] VkImageView GetView() const { return m_Image->GetView(); }
        [[nodiscard]] VkSampler GetSampler() const { return m_Sampler; }

    private:
        VulkanDevice& m_Device;
        VulkanImage* m_Image = nullptr;
        VkSampler m_Sampler = VK_NULL_HANDLE;

        void CreateSampler();
    };
}
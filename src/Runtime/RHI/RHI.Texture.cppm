module;
#include "RHI.Vulkan.hpp"
#include <string>
#include <vector>
#include <memory>

export module RHI:Texture;

import :Device;
import :Image;
import Core;

export namespace RHI
{
    // Forward declare the System
    class TextureSystem;

    struct TextureTag {};
    using TextureHandle = Core::StrongHandle<TextureTag>;

    // Heavy GPU data (lives in TextureSystem pool)
    struct TextureGpuData
    {
        std::unique_ptr<VulkanImage> Image;
        VkSampler Sampler = VK_NULL_HANDLE;
    };

    // Public RAII handle held by AssetManager (via shared_ptr<T> slot).
    // Move-only: asset lifetime/sharing is managed by AssetManager, not by copying Texture objects.
    class Texture
    {
    public:
        Texture(TextureSystem& system, VulkanDevice& device, const std::string& filepath);
        Texture(TextureSystem& system, VulkanDevice& device, const std::vector<uint8_t>& data,
                uint32_t width, uint32_t height, VkFormat format = VK_FORMAT_R8G8B8A8_SRGB);
        Texture(TextureSystem& system, VulkanDevice& device, uint32_t width, uint32_t height, VkFormat format);

        ~Texture();

        Texture(const Texture&) = delete;
        Texture& operator=(const Texture&) = delete;
        Texture(Texture&&) noexcept;
        Texture& operator=(Texture&&) noexcept;

        [[nodiscard]] TextureHandle GetHandle() const { return m_Handle; }
        [[nodiscard]] uint32_t GetBindlessIndex() const { return m_Handle.Index; }

        // Convenience accessors for tools/UI code paths.
        [[nodiscard]] VkImage GetImage() const;
        [[nodiscard]] VkImageView GetView() const;
        [[nodiscard]] VkSampler GetSampler() const;

    private:
        TextureSystem* m_System = nullptr;
        VulkanDevice* m_Device = nullptr;
        TextureHandle m_Handle{};
    };
}

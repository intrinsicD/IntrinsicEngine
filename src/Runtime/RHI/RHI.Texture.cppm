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

        // Shader-visible stable index into the global bindless texture array.
        // NOTE: This is intentionally NOT TextureHandle::Index because pool indices can be reused.
        uint32_t BindlessSlot = 0;
    };

    // Public RAII handle held by AssetManager (via shared_ptr<T> slot).
    // Move-only: asset lifetime/sharing is managed by AssetManager, not by copying Texture objects.
    class Texture
    {
    public:
        // New: lightweight wrapper around an already-allocated texture handle.
        // Contract: handle must come from TextureSystem.
        Texture(TextureSystem& system, VulkanDevice& device, TextureHandle handle);

        // Removed: synchronous I/O + blocking upload constructors.
        // Streaming must go through TransferManager + TextureSystem.
        // Texture(TextureSystem& system, VulkanDevice& device, const std::string& filepath);
        // Texture(TextureSystem& system, VulkanDevice& device, const std::vector<uint8_t>& data,
        //         uint32_t width, uint32_t height, VkFormat format = VK_FORMAT_R8G8B8A8_SRGB);

        // Kept: Create an empty GPU texture (used for default/error or render targets).
        Texture(TextureSystem& system, VulkanDevice& device, uint32_t width, uint32_t height, VkFormat format);

        ~Texture();

        Texture(const Texture&) = delete;
        Texture& operator=(const Texture&) = delete;
        Texture(Texture&&) noexcept;
        Texture& operator=(Texture&&) noexcept;

        [[nodiscard]] TextureHandle GetHandle() const { return m_Handle; }

        // Stable shader-visible index.
        [[nodiscard]] uint32_t GetBindlessIndex() const;

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

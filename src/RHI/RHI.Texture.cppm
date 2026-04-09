module;
#include "RHI.Vulkan.hpp"
#include <memory>

export module RHI.Texture;

import RHI.Device;
import RHI.TextureManager;
import RHI.TextureFwd;
import RHI.Image;
import RHI.TextureHandle; // For Core::StrongHandle

export namespace RHI
{
    // Public RAII handle held by AssetManager (via shared_ptr<T> slot).
    // Move-only: asset lifetime/sharing is managed by AssetManager, not by copying Texture objects.
    class Texture
    {
    public:
        // New: lightweight wrapper around an already-allocated texture handle.
        // Contract: handle must come from TextureManager.
        Texture(TextureManager& system, VulkanDevice& device, TextureHandle handle);

        // Streaming must go through TransferManager + TextureManager.

        // Kept: Create an empty GPU texture (used for default/error or render targets).
        Texture(TextureManager& system, VulkanDevice& device, uint32_t width, uint32_t height, VkFormat format);

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
        TextureManager* m_System = nullptr;
        VulkanDevice* m_Device = nullptr;
        TextureHandle m_Handle{};
    };
}

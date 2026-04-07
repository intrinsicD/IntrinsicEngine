module;
#include <filesystem>
#include <memory>
#include <expected>
#include <span>

export module Graphics.TextureLoader;

import Graphics.AssetErrors;
import RHI.Device;
import RHI.Texture;
import RHI.TextureFwd;
import RHI.TextureManager;
import RHI.Transfer;

export namespace Graphics
{
    struct TextureLoadResult
    {
        std::shared_ptr<RHI::Texture> Texture{};
        RHI::TextureHandle TextureHandle{}; // Needed for completion publish
        RHI::TransferToken Token{};
    };

    class TextureLoader
    {
    public:
        // Fallible operation (I/O + decode + GPU upload): return expected with a typed error.
        [[nodiscard]]
        static std::expected<TextureLoadResult, AssetError> LoadAsync(
            const std::filesystem::path& filepath,
            RHI::VulkanDevice& device,
            RHI::TransferManager& transferManager,
            RHI::TextureManager& textureManager,
            bool isSRGB = true);

        // Upload already-decoded RGBA8 pixels directly to the GPU.
        [[nodiscard]]
        static std::expected<TextureLoadResult, AssetError> LoadAsyncFromRGBA8(
            std::span<const std::byte> pixels,
            uint32_t width,
            uint32_t height,
            RHI::VulkanDevice& device,
            RHI::TransferManager& transferManager,
            RHI::TextureManager& textureManager,
            bool isSRGB = true);
    };
}

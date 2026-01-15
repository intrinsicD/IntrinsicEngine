module;
#include <filesystem>
#include <memory>
#include <expected>

export module Graphics:TextureLoader;

import :AssetErrors;
import RHI;

export namespace Graphics {

    struct TextureLoadResult {
        std::unique_ptr<RHI::Texture> Resource;
        RHI::TransferToken Token;
    };

    class TextureLoader {
    public:
        // Fallible operation (I/O + decode + GPU upload): return expected with a typed error.
        [[nodiscard]]
        static std::expected<TextureLoadResult, AssetError> LoadAsync(
            const std::filesystem::path& filepath,
            RHI::VulkanDevice& device,
            RHI::TransferManager& transferManager,
            bool isSRGB = true);
    };
}
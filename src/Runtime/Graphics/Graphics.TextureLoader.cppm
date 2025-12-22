module;
#include <filesystem>
#include <memory>
#include <optional>

export module Runtime.Graphics.TextureLoader;

import Runtime.RHI.Device;
import Runtime.RHI.Texture;
import Runtime.RHI.Transfer;

export namespace Runtime::Graphics {

    struct TextureLoadResult {
        std::shared_ptr<RHI::Texture> Resource;
        RHI::TransferToken Token;
    };

    class TextureLoader {
    public:
        // Returns nullopt if file not found or decode failed.
        // Returns the Resource + The Token to wait on.
        [[nodiscard]] 
        static std::optional<TextureLoadResult> LoadAsync(
            const std::filesystem::path& filepath,
            std::shared_ptr<RHI::VulkanDevice> device,
            RHI::TransferManager& transferManager
        );
    };
}
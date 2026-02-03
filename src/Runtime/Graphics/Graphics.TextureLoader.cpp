module;
#include "stb_image.h"
#include <filesystem>
#include <expected>
#include <vector>
#include "RHI.Vulkan.hpp"

module Graphics:TextureLoader.Impl;
import :TextureLoader;

import RHI;

namespace Graphics
{
    std::expected<TextureLoadResult, AssetError> TextureLoader::LoadAsync(
        const std::filesystem::path& filepath,
        RHI::VulkanDevice& device,
        RHI::TextureSystem& textureSystem,
        bool isSRGB)
    {
        // 1) IO & Decode
        int w = 0;
        int h = 0;
        int c = 0;

        stbi_uc* pixels = stbi_load(filepath.string().c_str(), &w, &h, &c, STBI_rgb_alpha);
        if (!pixels)
            return std::unexpected(AssetError::DecodeFailed);

        if (w <= 0 || h <= 0)
        {
            stbi_image_free(pixels);
            return std::unexpected(AssetError::InvalidData);
        }

        const VkFormat format = isSRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;

        // 2) Create texture (GPU resource + descriptor slot).
        // NOTE: This currently performs a blocking upload internally (BeginSingleTimeCommands).
        // We keep TransferManager path for future refactor; for now token is a no-op placeholder.
        auto texture = std::make_shared<RHI::Texture>(
            textureSystem,
            device,
            std::vector<uint8_t>(pixels, pixels + static_cast<size_t>(w) * static_cast<size_t>(h) * 4u),
            static_cast<uint32_t>(w),
            static_cast<uint32_t>(h),
            format);

        stbi_image_free(pixels);

        // No async submission in current handle-body path.
        RHI::TransferToken token{};

        return TextureLoadResult{ std::move(texture), token };
    }
}
module;
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <filesystem>
#include <optional>
#include <vector>
#include <RHI/RHI.Vulkan.hpp>

module Runtime.Graphics.TextureLoader;

import Core.Logging;
import Core.Filesystem;
import Runtime.RHI.Buffer;
import Runtime.RHI.CommandUtils;

namespace Runtime::Graphics {

    std::optional<TextureLoadResult> TextureLoader::LoadAsync(
        const std::filesystem::path& filepath,
        std::shared_ptr<RHI::VulkanDevice> device,
        RHI::TransferManager& transferManager)
    {
        // 1. IO & Decode
        // (Note: In a full engine, we'd use Core::Filesystem to read into memory buffer first)
        int w, h, c;
        // Ensure we force 4 channels (RGBA) for Vulkan compatibility
        stbi_uc* pixels = stbi_load(filepath.string().c_str(), &w, &h, &c, STBI_rgb_alpha); 
        
        if (!pixels) {
            Core::Log::Error("TextureLoader: Failed to load {}", filepath.string());
            return std::nullopt;
        }

        // 2. Prepare Staging
        size_t imageSize = w * h * 4;
        auto staging = std::make_unique<RHI::VulkanBuffer>(
            device, imageSize, 
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 
            VMA_MEMORY_USAGE_CPU_ONLY
        );
        
        memcpy(staging->Map(), pixels, imageSize);
        staging->Unmap();
        stbi_image_free(pixels);

        // 3. Allocate GPU Resource
        auto texture = std::make_shared<RHI::Texture>(
            device, static_cast<uint32_t>(w), static_cast<uint32_t>(h), VK_FORMAT_R8G8B8A8_SRGB
        );

        // 4. Record Transfer Commands
        VkCommandBuffer cmd = transferManager.Begin();

        // Transition: Undefined -> Transfer Dst
        RHI::CommandUtils::TransitionImageLayout(cmd, texture->GetImage(), 
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkBufferImageCopy region{};
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.imageExtent = { (uint32_t)w, (uint32_t)h, 1 };

        vkCmdCopyBufferToImage(cmd, staging->GetHandle(), texture->GetImage(), 
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        // Transition: Transfer Dst -> Shader Read Only
        RHI::CommandUtils::TransitionImageLayout(cmd, texture->GetImage(), 
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        // 5. Submit
        std::vector<std::unique_ptr<RHI::VulkanBuffer>> stagingBuffers;
        stagingBuffers.push_back(std::move(staging));

        RHI::TransferToken token = transferManager.Submit(cmd, std::move(stagingBuffers));

        return TextureLoadResult{ texture, token };
    }
}
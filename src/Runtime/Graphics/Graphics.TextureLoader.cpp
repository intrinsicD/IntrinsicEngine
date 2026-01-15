module;
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <filesystem>
#include <expected>
#include <vector>
#include <cstring>
#include "RHI.Vulkan.hpp"

module Graphics:TextureLoader.Impl;
import :TextureLoader;

import RHI;

namespace Graphics
{
    std::expected<TextureLoadResult, AssetError> TextureLoader::LoadAsync(
        const std::filesystem::path& filepath,
        RHI::VulkanDevice& device,
        RHI::TransferManager& transferManager,
        bool isSRGB)
    {
        // Device is a non-null reference by contract.

        // 1) IO & Decode
        int w = 0;
        int h = 0;
        int c = 0;

        // Force 4 channels (RGBA) for Vulkan compatibility.
        stbi_uc* pixels = stbi_load(filepath.string().c_str(), &w, &h, &c, STBI_rgb_alpha);
        if (!pixels)
            return std::unexpected(AssetError::DecodeFailed);

        if (w <= 0 || h <= 0)
        {
            stbi_image_free(pixels);
            return std::unexpected(AssetError::InvalidData);
        }

        // 2) Prepare Staging
        const size_t imageSize = static_cast<size_t>(w) * static_cast<size_t>(h) * 4u;

        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(device.GetPhysicalDevice(), &props);

        constexpr size_t texelSize = 4; // RGBA8
        const size_t rowPitch = static_cast<size_t>(w) * texelSize;

        auto alloc = transferManager.AllocateStagingForImage(
            imageSize,
            texelSize,
            rowPitch,
            static_cast<size_t>(props.limits.optimalBufferCopyOffsetAlignment),
            static_cast<size_t>(props.limits.optimalBufferCopyRowPitchAlignment));

        std::vector<std::unique_ptr<RHI::VulkanBuffer>> stagingBuffers;
        VkBuffer stagingHandle = VK_NULL_HANDLE;
        VkDeviceSize stagingOffset = 0;

        if (alloc.Buffer != VK_NULL_HANDLE)
        {
            std::memcpy(alloc.MappedPtr, pixels, imageSize);
            stagingHandle = alloc.Buffer;
            stagingOffset = static_cast<VkDeviceSize>(alloc.Offset);
        }
        else
        {
            // Slow fallback: allocate a dedicated staging buffer.
            auto staging = std::make_unique<RHI::VulkanBuffer>(
                device,
                imageSize,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VMA_MEMORY_USAGE_CPU_ONLY);

            std::memcpy(staging->Map(), pixels, imageSize);
            staging->Unmap();

            stagingHandle = staging->GetHandle();
            stagingOffset = 0;
            stagingBuffers.push_back(std::move(staging));
        }

        stbi_image_free(pixels);

        const VkFormat format = isSRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;

        // 3) Allocate GPU Resource
        auto texture = std::make_unique<RHI::Texture>(
            device,
            static_cast<uint32_t>(w),
            static_cast<uint32_t>(h),
            format);

        // 4) Record Transfer Commands
        VkCommandBuffer cmd = transferManager.Begin();

        // Transition: Undefined -> Transfer Dst
        RHI::CommandUtils::TransitionImageLayout(
            cmd,
            texture->GetImage(),
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkBufferImageCopy region{};
        region.bufferOffset = stagingOffset;
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.imageExtent = { static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1u };

        vkCmdCopyBufferToImage(
            cmd,
            stagingHandle,
            texture->GetImage(),
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &region);

        // Transition: Transfer Dst -> Shader Read Only
        RHI::CommandUtils::TransitionImageLayout(
            cmd,
            texture->GetImage(),
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        // 5) Submit
        RHI::TransferToken token = stagingBuffers.empty()
            ? transferManager.Submit(cmd)
            : transferManager.Submit(cmd, std::move(stagingBuffers));

        return TextureLoadResult{ std::move(texture), token };
    }
}
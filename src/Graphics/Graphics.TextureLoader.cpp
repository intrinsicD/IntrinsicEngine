module;
#include "stb_image.h"
#include <cstddef>
#include <filesystem>
#include <expected>
#include <cstring>
#include <span>
#include "RHI.Vulkan.hpp"

module Graphics.TextureLoader;
import Asset.Errors;

import RHI.Device;
import RHI.Texture;
import RHI.TextureManager;
import RHI.Transfer;

namespace Graphics
{
    std::expected<TextureLoadResult, AssetError> TextureLoader::LoadAsyncFromRGBA8(
        std::span<const std::byte> pixels,
        uint32_t width,
        uint32_t height,
        RHI::VulkanDevice& device,
        RHI::TransferManager& transferManager,
        RHI::TextureManager& textureManager,
        bool isSRGB)
    {
        if (width == 0 || height == 0)
            return std::unexpected(AssetError::InvalidData);

        const size_t sizeBytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
        if (pixels.size() < sizeBytes)
            return std::unexpected(AssetError::InvalidData);

        const VkFormat format = isSRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;

        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(device.GetPhysicalDevice(), &props);

        const size_t texelBlockSize = 4;
        const size_t rowPitchBytes = static_cast<size_t>(width) * texelBlockSize;

        auto alloc = transferManager.AllocateStagingForImage(
            sizeBytes,
            texelBlockSize,
            rowPitchBytes,
            static_cast<size_t>(props.limits.optimalBufferCopyOffsetAlignment),
            static_cast<size_t>(props.limits.optimalBufferCopyRowPitchAlignment));

        if (alloc.Buffer == VK_NULL_HANDLE || alloc.MappedPtr == nullptr)
            return std::unexpected(AssetError::UploadFailed);

        std::memcpy(alloc.MappedPtr, pixels.data(), sizeBytes);

        const RHI::TextureHandle handle = textureManager.CreatePending(width, height, format);
        auto texture = std::make_shared<RHI::Texture>(textureManager, device, handle);

        VkCommandBuffer cmd = transferManager.Begin();

        VkImage dstImage = texture->GetImage();
        if (dstImage == VK_NULL_HANDLE)
        {
            auto token = transferManager.Submit(cmd);
            return TextureLoadResult{std::move(texture), handle, token};
        }

        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        barrier.srcAccessMask = 0;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = dstImage;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        VkDependencyInfo dep{};
        dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(cmd, &dep);

        VkBufferImageCopy region{};
        region.bufferOffset = static_cast<VkDeviceSize>(alloc.Offset);
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {width, height, 1};

        vkCmdCopyBufferToImage(cmd,
                               alloc.Buffer,
                               dstImage,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               1,
                               &region);

        VkImageMemoryBarrier2 readBarrier = barrier;
        readBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        readBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        readBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        readBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        readBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        readBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDependencyInfo dep2{};
        dep2.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep2.imageMemoryBarrierCount = 1;
        dep2.pImageMemoryBarriers = &readBarrier;
        vkCmdPipelineBarrier2(cmd, &dep2);

        RHI::TransferToken token = transferManager.Submit(cmd);
        return TextureLoadResult{std::move(texture), handle, token};
    }

    std::expected<TextureLoadResult, AssetError> TextureLoader::LoadAsync(
        const std::filesystem::path& filepath,
        RHI::VulkanDevice& device,
        RHI::TransferManager& transferManager,
        RHI::TextureManager& textureManager,
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

        const size_t sizeBytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 4u;
        auto result = LoadAsyncFromRGBA8(std::span<const std::byte>(reinterpret_cast<const std::byte*>(pixels), sizeBytes),
                                  static_cast<uint32_t>(w),
                                  static_cast<uint32_t>(h),
                                  device,
                                  transferManager,
                                  textureManager,
                                  isSRGB);

        stbi_image_free(pixels);
        return result;
    }
}
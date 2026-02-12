module;
#include "stb_image.h"
#include <filesystem>
#include <expected>
#include <vector>
#include <cstring>
#include "RHI.Vulkan.hpp"

module Graphics:TextureLoader.Impl;
import :TextureLoader;
import :AssetErrors;

import RHI;

namespace Graphics
{
    std::expected<TextureLoadResult, AssetError> TextureLoader::LoadAsync(
        const std::filesystem::path& filepath,
        RHI::VulkanDevice& device,
        RHI::TransferManager& transferManager,
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

        const size_t sizeBytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 4u;

        // 2) Reserve a bindless-visible texture handle immediately (still default-bound).
        const RHI::TextureHandle handle = textureSystem.CreatePending(static_cast<uint32_t>(w), static_cast<uint32_t>(h), format);
        auto texture = std::make_shared<RHI::Texture>(textureSystem, device, handle);

        // 3) Staging allocation
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(device.GetPhysicalDevice(), &props);

        const size_t texelBlockSize = 4; // RGBA8
        const size_t rowPitchBytes = static_cast<size_t>(w) * texelBlockSize;

        auto alloc = transferManager.AllocateStagingForImage(
            sizeBytes,
            texelBlockSize,
            rowPitchBytes,
            static_cast<size_t>(props.limits.optimalBufferCopyOffsetAlignment),
            static_cast<size_t>(props.limits.optimalBufferCopyRowPitchAlignment));

        if (alloc.Buffer == VK_NULL_HANDLE || alloc.MappedPtr == nullptr)
        {
            stbi_image_free(pixels);
            return std::unexpected(AssetError::UploadFailed);
        }

        std::memcpy(alloc.MappedPtr, pixels, sizeBytes);
        stbi_image_free(pixels);

        // 4) Record copy on transfer queue
        VkCommandBuffer cmd = transferManager.Begin();

        VkImage dstImage = texture->GetImage();
        if (dstImage == VK_NULL_HANDLE)
        {
            // Can't upload; keep texture default-bound.
            // Submit empty work to keep token semantics predictable.
            auto token = transferManager.Submit(cmd);
            return TextureLoadResult{std::move(texture), handle, token};
        }

        // Transition: UNDEFINED -> TRANSFER_DST
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
        region.imageExtent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};

        vkCmdCopyBufferToImage(cmd,
                               alloc.Buffer,
                               dstImage,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               1,
                               &region);

        // Transition: TRANSFER_DST -> SHADER_READ
        VkImageMemoryBarrier2 readBarrier = barrier;
        readBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        readBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        // Fix: Use TRANSFER stage for queue-local barrier.
        // Visibility to Graphics/Compute queues is handled by the TransferManager's semaphore signal.
        readBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        readBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT; // Dummy access
        readBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        readBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDependencyInfo dep2{};
        dep2.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep2.imageMemoryBarrierCount = 1;
        dep2.pImageMemoryBarriers = &readBarrier;
        vkCmdPipelineBarrier2(cmd, &dep2);

        // 5) Submit and return token
        RHI::TransferToken token = transferManager.Submit(cmd);

        return TextureLoadResult{std::move(texture), handle, token};
    }
}
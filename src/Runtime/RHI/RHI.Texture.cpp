module;
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <RHI/RHI.Vulkan.hpp>
#include <string>
#include <cstring>
#include <memory>
#include <vector>

module Runtime.RHI.Texture;
import Runtime.RHI.Buffer;
import Runtime.RHI.Image;
import Runtime.RHI.CommandUtils;
import Core.Logging;
import Core.Filesystem;

namespace Runtime::RHI
{
    // Helper to generate mips
    inline void GenerateMipmaps(VkCommandBuffer cmd, VkImage image, int32_t texWidth, int32_t texHeight,
                                uint32_t mipLevels)
    {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.image = image;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.subresourceRange.levelCount = 1;

        int32_t mipWidth = texWidth;
        int32_t mipHeight = texHeight;

        for (uint32_t i = 1; i < mipLevels; i++)
        {
            // Transition level i-1 to TRANSFER_SRC_OPTIMAL
            barrier.subresourceRange.baseMipLevel = i - 1;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

            vkCmdPipelineBarrier(cmd,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                                 0, nullptr,
                                 0, nullptr,
                                 1, &barrier);

            // Blit i-1 to i
            VkImageBlit blit{};
            blit.srcOffsets[0] = {0, 0, 0};
            blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
            blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.srcSubresource.mipLevel = i - 1;
            blit.srcSubresource.baseArrayLayer = 0;
            blit.srcSubresource.layerCount = 1;
            blit.dstOffsets[0] = {0, 0, 0};
            blit.dstOffsets[1] = {mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1};
            blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.dstSubresource.mipLevel = i;
            blit.dstSubresource.baseArrayLayer = 0;
            blit.dstSubresource.layerCount = 1;

            vkCmdBlitImage(cmd,
                           image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &blit,
                           VK_FILTER_LINEAR);

            // Transition level i-1 to SHADER_READ_ONLY_OPTIMAL (We are done with it)
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier(cmd,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                                 0, nullptr,
                                 0, nullptr,
                                 1, &barrier);

            if (mipWidth > 1) mipWidth /= 2;
            if (mipHeight > 1) mipHeight /= 2;
        }

        // Transition the LAST level to SHADER_READ_ONLY_OPTIMAL
        barrier.subresourceRange.baseMipLevel = mipLevels - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                             0, nullptr,
                             0, nullptr,
                             1, &barrier);
    }

    Texture::Texture(std::shared_ptr<VulkanDevice> device, const std::string& filepath)
        : m_Device(device)
    {
        int w, h, c;
        std::string fullPath = Core::Filesystem::GetAssetPath(filepath);
        stbi_uc* pixels = stbi_load(fullPath.c_str(), &w, &h, &c, STBI_rgb_alpha);
        if (!pixels)
        {
            Core::Log::Error("Failed to load texture: {}", filepath);
            // Create a pink fallback 1x1
            uint32_t pink = 0xFF00FFFF;
            Upload(&pink, 1, 1);
            return;
        }
        Upload(pixels, w, h);
        stbi_image_free(pixels);
    }

    Texture::Texture(std::shared_ptr<VulkanDevice> device, const std::vector<uint8_t>& data, uint32_t width,
                     uint32_t height) : m_Device(device)
    {
        if (data.size() != width * height * 4)
        {
            Core::Log::Error("Texture data size mismatch!");
            return;
        }
        Upload(data.data(), width, height);
    }

    Texture::~Texture()
    {
        if (m_Sampler)
        {
            VkDevice logicalDevice = m_Device->GetLogicalDevice();
            VkSampler sampler = m_Sampler;

            m_Device->SafeDestroy([logicalDevice, sampler]()
            {
                vkDestroySampler(logicalDevice, sampler, nullptr);
            });
        }
    }

    void Texture::CreateSampler()
    {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.anisotropyEnable = VK_TRUE;

        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(m_Device->GetPhysicalDevice(), &properties);
        samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;

        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;

        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = static_cast<float>(m_Image->GetMipLevels());

        if (vkCreateSampler(m_Device->GetLogicalDevice(), &samplerInfo, nullptr, &m_Sampler) != VK_SUCCESS)
        {
            Core::Log::Error("Failed to create texture sampler!");
        }
    }

    void Texture::Upload(const void* data, uint32_t width, uint32_t height)
    {
        uint32_t mipLevels = 1;

        VkDeviceSize imageSize = width * height * 4;

        VulkanBuffer stagingBuffer(m_Device, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
        std::memcpy(stagingBuffer.Map(), data, static_cast<size_t>(imageSize));
        stagingBuffer.Unmap();

        m_Image = std::make_unique<VulkanImage>(
            m_Device, width, height, mipLevels, VK_FORMAT_R8G8B8A8_SRGB,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, // Removed SRC_BIT as we aren't blitting
            VK_IMAGE_ASPECT_COLOR_BIT
        );

        CommandUtils::ExecuteImmediate(*m_Device, [&](VkCommandBuffer cmd)
        {
            // 1. Transition Undefined -> Transfer Dst
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.image = m_Image->GetHandle();
            barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 1};

            vkCmdPipelineBarrier(cmd,
                                 VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &barrier);

            // 2. Copy Buffer to Image
            VkBufferImageCopy region{};
            region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.imageExtent = {width, height, 1};
            vkCmdCopyBufferToImage(cmd, stagingBuffer.GetHandle(), m_Image->GetHandle(),
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

            // 3. Transition Transfer Dst -> Shader Read Only
            VkImageMemoryBarrier readBarrier = barrier;
            readBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            readBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            readBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            readBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier(cmd,
                                 VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &readBarrier);
        });

        CreateSampler();
    }
}

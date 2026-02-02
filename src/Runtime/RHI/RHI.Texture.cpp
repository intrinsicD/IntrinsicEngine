module;
#include "stb_image.h"
#include "RHI.Vulkan.hpp"
#include <string>
#include <cstring>
#include <memory>
#include <vector>

module RHI:Texture.Impl;
import :Texture;
import :TextureSystem;
import :Buffer;
import :Image;
import :CommandUtils;
import Core;

namespace RHI
{
    static void CreateSampler(VulkanDevice& device, uint32_t mipLevels, VkSampler& outSampler)
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
        vkGetPhysicalDeviceProperties(device.GetPhysicalDevice(), &properties);
        samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;

        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = static_cast<float>(mipLevels);

        if (vkCreateSampler(device.GetLogicalDevice(), &samplerInfo, nullptr, &outSampler) != VK_SUCCESS)
        {
            Core::Log::Error("Failed to create texture sampler!");
            outSampler = VK_NULL_HANDLE;
        }
    }

    static std::unique_ptr<TextureGpuData> UploadTextureData(
        VulkanDevice& device,
        const void* pixels,
        uint32_t width,
        uint32_t height,
        VkFormat format)
    {
        auto gpu = std::make_unique<TextureGpuData>();

        const VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4u;
        VulkanBuffer stagingBuffer(device, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);
        std::memcpy(stagingBuffer.Map(), pixels, static_cast<size_t>(imageSize));
        stagingBuffer.Unmap();

        const uint32_t mipLevels = 1;

        gpu->Image = std::make_unique<VulkanImage>(
            device,
            width,
            height,
            mipLevels,
            format,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT);

        VkCommandBuffer cmd = CommandUtils::BeginSingleTimeCommands(device);

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.image = gpu->Image->GetHandle();
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 1};

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);

        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {width, height, 1};
        vkCmdCopyBufferToImage(cmd, stagingBuffer.GetHandle(), gpu->Image->GetHandle(),
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        VkImageMemoryBarrier readBarrier = barrier;
        readBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        readBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        readBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        readBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &readBarrier);

        CommandUtils::EndSingleTimeCommands(device, cmd);

        CreateSampler(device, mipLevels, gpu->Sampler);

        return gpu;
    }

    Texture::Texture(TextureSystem& system, VulkanDevice& device, const std::string& filepath)
        : m_System(&system), m_Device(&device)
    {
        int w = 0, h = 0, c = 0;
        std::string fullPath = Core::Filesystem::GetAssetPath(filepath);
        stbi_uc* pixels = stbi_load(fullPath.c_str(), &w, &h, &c, STBI_rgb_alpha);

        if (!pixels || w <= 0 || h <= 0)
        {
            Core::Log::Error("Failed to load texture: {}", filepath);
            uint32_t pink = 0xFF00FFFF;
            auto gpu = UploadTextureData(device, &pink, 1, 1, VK_FORMAT_R8G8B8A8_SRGB);
            if (pixels) stbi_image_free(pixels);
            m_Handle = m_System->CreateFromData(std::move(gpu));
            return;
        }

        auto gpu = UploadTextureData(device, pixels, static_cast<uint32_t>(w), static_cast<uint32_t>(h), VK_FORMAT_R8G8B8A8_SRGB);
        stbi_image_free(pixels);

        m_Handle = m_System->CreateFromData(std::move(gpu));
    }

    Texture::Texture(TextureSystem& system, VulkanDevice& device, const std::vector<uint8_t>& data,
                     uint32_t width, uint32_t height, VkFormat format)
        : m_System(&system), m_Device(&device)
    {
        if (data.size() != static_cast<size_t>(width) * static_cast<size_t>(height) * 4u)
        {
            Core::Log::Error("Texture data size mismatch!");
            return;
        }

        auto gpu = UploadTextureData(device, data.data(), width, height, format);
        m_Handle = m_System->CreateFromData(std::move(gpu));
    }

    Texture::Texture(TextureSystem& system, VulkanDevice& device, uint32_t width, uint32_t height, VkFormat format)
        : m_System(&system), m_Device(&device)
    {
        auto gpu = std::make_unique<TextureGpuData>();

        auto indices = device.GetQueueIndices();
        bool distinctQueues = false;
        if (indices.GraphicsFamily.has_value() && indices.TransferFamily.has_value())
        {
            distinctQueues = (indices.GraphicsFamily.value() != indices.TransferFamily.value());
        }

        VkSharingMode sharingMode = distinctQueues ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE;

        gpu->Image = std::make_unique<VulkanImage>(
            device, width, height, 1, format,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,
            sharingMode);

        CreateSampler(device, gpu->Image->GetMipLevels(), gpu->Sampler);

        m_Handle = m_System->CreateFromData(std::move(gpu));
    }

    Texture::~Texture()
    {
        if (!m_System || !m_Device || !m_Handle.IsValid())
            return;

        // Defer sampler destruction in the same way old Texture did.
        // The pool stores TextureGpuData; we need to ensure VkSampler is destroyed safely.
        // We do that by scheduling sampler destruction now and then removing the pool entry.
        if (const TextureGpuData* data = m_System->Get(m_Handle))
        {
            if (data->Sampler)
            {
                VkDevice logicalDevice = m_Device->GetLogicalDevice();
                VkSampler sampler = data->Sampler;
                m_Device->SafeDestroy([logicalDevice, sampler]()
                {
                    vkDestroySampler(logicalDevice, sampler, nullptr);
                });
            }
        }

        m_System->Destroy(m_Handle);
        m_Handle = {};
        m_System = nullptr;
        m_Device = nullptr;
    }

    Texture::Texture(Texture&& other) noexcept
        : m_System(other.m_System)
        , m_Device(other.m_Device)
        , m_Handle(other.m_Handle)
    {
        other.m_System = nullptr;
        other.m_Device = nullptr;
        other.m_Handle = {};
    }

    Texture& Texture::operator=(Texture&& other) noexcept
    {
        if (this == &other) return *this;

        // Release current
        this->~Texture();

        m_System = other.m_System;
        m_Device = other.m_Device;
        m_Handle = other.m_Handle;

        other.m_System = nullptr;
        other.m_Device = nullptr;
        other.m_Handle = {};

        return *this;
    }

    uint32_t Texture::GetBindlessIndex() const
    {
        if (!m_System || !m_Handle.IsValid()) return 0u;
        if (const auto* data = m_System->Get(m_Handle)) return data->BindlessSlot;
        return 0u;
    }

    VkImage Texture::GetImage() const
    {
        if (!m_System || !m_Handle.IsValid()) return VK_NULL_HANDLE;
        if (const auto* data = m_System->Get(m_Handle)) return data->Image->GetHandle();
        return VK_NULL_HANDLE;
    }

    VkImageView Texture::GetView() const
    {
        if (!m_System || !m_Handle.IsValid()) return VK_NULL_HANDLE;
        if (const auto* data = m_System->Get(m_Handle)) return data->Image->GetView();
        return VK_NULL_HANDLE;
    }

    VkSampler Texture::GetSampler() const
    {
        if (!m_System || !m_Handle.IsValid()) return VK_NULL_HANDLE;
        if (const auto* data = m_System->Get(m_Handle)) return data->Sampler;
        return VK_NULL_HANDLE;
    }
}

module;

#include "RHI.Vulkan.hpp"
#include <memory>

module RHI:Texture.Impl;
import :Texture;
import :Device;
import :TextureSystem;
import :Image;
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

    Texture::Texture(TextureSystem& system, VulkanDevice& device, TextureHandle handle)
        : m_System(&system), m_Device(&device), m_Handle(handle)
    {
    }

    Texture::Texture(TextureSystem& system, VulkanDevice& device, uint32_t width, uint32_t height, VkFormat format)
        : m_System(&system), m_Device(&device)
    {
        auto gpu = std::make_unique<TextureGpuData>();

        auto indices = device.GetQueueIndices();
        bool distinctQueues = false;
        if (indices.GraphicsFamily.has_value() && indices.TransferFamily.has_value())
            distinctQueues = (indices.GraphicsFamily.value() != indices.TransferFamily.value());

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

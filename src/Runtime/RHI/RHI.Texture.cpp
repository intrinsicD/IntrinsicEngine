module;
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <RHI/RHI.Vulkan.hpp>
#include <string>
#include <cstring>

module Runtime.RHI.Texture;
import Runtime.RHI.Buffer;
import Runtime.RHI.CommandUtils;
import Core.Logging;

namespace Runtime::RHI {

    Texture::Texture(VulkanDevice& device, const std::string& filepath)
        : m_Device(device)
    {
        // 1. Load Pixels from Disk
        int texWidth, texHeight, texChannels;
        stbi_set_flip_vertically_on_load(true); // Vulkan UVs are usually flipped relative to OpenGL, but standard convention varies.
        
        stbi_uc* pixels = stbi_load(filepath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
        VkDeviceSize imageSize = texWidth * texHeight * 4;

        if (!pixels) {
            Core::Log::Error("Failed to load texture image: {}", filepath);
            return;
        }

        // 2. Staging Buffer
        VulkanBuffer stagingBuffer(
            device, 
            imageSize, 
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 
            VMA_MEMORY_USAGE_CPU_ONLY
        );

        void* data = stagingBuffer.Map();
        std::memcpy(data, pixels, static_cast<size_t>(imageSize));
        stagingBuffer.Unmap();

        stbi_image_free(pixels);

        // 3. Create GPU Image
        m_Image = new VulkanImage(
            device, 
            texWidth, texHeight, 
            VK_FORMAT_R8G8B8A8_SRGB, 
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT
        );

        // 4. Upload (Complex Barrier Dance)
        CommandUtils::ExecuteImmediate(device, [&](VkCommandBuffer cmd) {
            
            // A. Transition Undefined -> Transfer Dst
            CommandUtils::TransitionImageLayout(
                cmd, m_Image->GetHandle(), 
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL
            );

            // B. Copy Buffer to Image
            VkBufferImageCopy region{};
            region.bufferOffset = 0;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = {0, 0, 0};
            region.imageExtent = { static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight), 1 };

            vkCmdCopyBufferToImage(cmd, stagingBuffer.GetHandle(), m_Image->GetHandle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

            // C. Transition Transfer Dst -> Shader Read
            CommandUtils::TransitionImageLayout(
                cmd, m_Image->GetHandle(), 
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            );
        });

        CreateSampler();
    }

    Texture::~Texture() {
        vkDestroySampler(m_Device.GetLogicalDevice(), m_Sampler, nullptr);
        delete m_Image;
    }

    void Texture::CreateSampler() {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        samplerInfo.anisotropyEnable = VK_TRUE;
        
        // Query limits for anisotropy
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(m_Device.GetPhysicalDevice(), &properties);
        samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
        
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

        if (vkCreateSampler(m_Device.GetLogicalDevice(), &samplerInfo, nullptr, &m_Sampler) != VK_SUCCESS) {
            Core::Log::Error("Failed to create texture sampler!");
        }
    }
}
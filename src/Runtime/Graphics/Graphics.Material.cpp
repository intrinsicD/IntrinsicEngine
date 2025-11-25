module;
#include <RHI/RHI.Vulkan.hpp>
#include <memory>
#include <vector>

module Runtime.Graphics.Material;
import Core.Filesystem;

namespace Runtime::Graphics {

    Material::Material(RHI::VulkanDevice& device,
                       RHI::DescriptorPool& pool,
                       const RHI::DescriptorLayout& layout,
                       Core::Assets::AssetHandle textureHandle)
        : m_Device(device), m_TextureHandle(textureHandle)
    {
        m_DescriptorSet = pool.Allocate(layout.GetHandle());
    }

    void Material::WriteDescriptor(VkBuffer cameraBuffer, VkDeviceSize range) {

        m_PendingBuffer = cameraBuffer;
        m_PendingRange = range;
        m_IsDescriptorWritten = false; // Mark dirty
    }

    bool Material::Prepare(Core::Assets::AssetManager& assetManager)
    {
        // If already written, we are good.
        if (m_IsDescriptorWritten) return true;

        // Check dependencies
        auto texture = assetManager.Get<RHI::Texture>(m_TextureHandle);
        if (!texture) {
            // Texture not ready yet.
            // In a real engine, we would bind a "default white" texture here temporarily.
            return false;
        }

        if (m_PendingBuffer == VK_NULL_HANDLE) return false;

        // 1. Camera UBO Write
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = m_PendingBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = m_PendingRange;

        VkWriteDescriptorSet descriptorWriteUBO{};
        descriptorWriteUBO.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWriteUBO.dstSet = m_DescriptorSet;
        descriptorWriteUBO.dstBinding = 0;
        descriptorWriteUBO.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        descriptorWriteUBO.descriptorCount = 1;
        descriptorWriteUBO.pBufferInfo = &bufferInfo;

        // 2. Texture Write (Now using the asset!)
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = texture->GetView();
        imageInfo.sampler = texture->GetSampler();

        VkWriteDescriptorSet descriptorWriteImage{};
        descriptorWriteImage.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWriteImage.dstSet = m_DescriptorSet;
        descriptorWriteImage.dstBinding = 1;
        descriptorWriteImage.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWriteImage.descriptorCount = 1;
        descriptorWriteImage.pImageInfo = &imageInfo;

        std::vector<VkWriteDescriptorSet> writes = {descriptorWriteUBO, descriptorWriteImage};
        vkUpdateDescriptorSets(m_Device.GetLogicalDevice(), (uint32_t)writes.size(), writes.data(), 0, nullptr);

        m_IsDescriptorWritten = true;
        return true;
    }

}
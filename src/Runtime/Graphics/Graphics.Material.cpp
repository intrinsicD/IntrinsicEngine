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
                       const std::string& texturePath)
        : m_Device(device)
    {
        // 1. Load Texture
        std::string fullPath = Core::Filesystem::GetAssetPath(texturePath);
        m_Texture = std::make_unique<RHI::Texture>(device, fullPath);

        // 2. Allocate Set
        m_DescriptorSet = pool.Allocate(layout.GetHandle());
    }

    void Material::WriteDescriptor(VkBuffer cameraBuffer, VkDeviceSize range) {
        // 1. Camera UBO Write
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = cameraBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = range;

        VkWriteDescriptorSet descriptorWriteUBO{};
        descriptorWriteUBO.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWriteUBO.dstSet = m_DescriptorSet;
        descriptorWriteUBO.dstBinding = 0;
        descriptorWriteUBO.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        descriptorWriteUBO.descriptorCount = 1;
        descriptorWriteUBO.pBufferInfo = &bufferInfo;

        // 2. Texture Write
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = m_Texture->GetView();
        imageInfo.sampler = m_Texture->GetSampler();

        VkWriteDescriptorSet descriptorWriteImage{};
        descriptorWriteImage.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWriteImage.dstSet = m_DescriptorSet;
        descriptorWriteImage.dstBinding = 1;
        descriptorWriteImage.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWriteImage.descriptorCount = 1;
        descriptorWriteImage.pImageInfo = &imageInfo;

        std::vector<VkWriteDescriptorSet> writes = {descriptorWriteUBO, descriptorWriteImage};
        vkUpdateDescriptorSets(m_Device.GetLogicalDevice(), 2, writes.data(), 0, nullptr);
    }
}
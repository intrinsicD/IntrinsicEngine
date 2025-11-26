module;
#include <RHI/RHI.Vulkan.hpp>
#include <memory>
#include <vector>

module Runtime.Graphics.Material;
import Core.Filesystem;
import Core.Logging;

namespace Runtime::Graphics {

    Material::Material(std::shared_ptr<RHI::VulkanDevice> device,
                       RHI::DescriptorPool& pool,
                       const RHI::DescriptorLayout& layout,
                       Core::Assets::AssetHandle targetTextureHandle,
                       std::shared_ptr<RHI::Texture> defaultTexture,
                       Core::Assets::AssetManager& assetManager)
        : m_Device(device), m_CurrentTexture(defaultTexture)
    {
        // 1. Allocate Descriptor Set
        m_DescriptorSet = pool.Allocate(layout.GetHandle());

        // 2. Initial State: Use Default Texture (White Pixel)
        // This ensures the material is valid for rendering IMMEDIATELY.
        UpdateImageWrite(*m_CurrentTexture);

        // 3. Async Upgrade: Register callback for the real texture
        // We capture 'this' safely. In a real engine, we'd use weak_from_this()
        // to ensure the material still exists, but here we assume lifetime is managed by scene.
        assetManager.RequestNotify(targetTextureHandle, [this, assetManager = &assetManager](Core::Assets::AssetHandle handle)
        {
            // This runs on the Main Thread via AssetManager::Update()
            auto newTexture = assetManager->Get<RHI::Texture>(handle);
            if (newTexture)
            {
                m_CurrentTexture = newTexture; // Keep reference alive
                UpdateImageWrite(*newTexture);
                Core::Log::Info("Material updated to high-res texture.");
            }
        });
    }

    void Material::UpdateImageWrite(const RHI::Texture& texture)
    {
        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = texture.GetView();
        imageInfo.sampler = texture.GetSampler();

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = m_DescriptorSet;
        descriptorWrite.dstBinding = 1; // Binding 1 = Sampler
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pImageInfo = &imageInfo;

        // Note: Updating a descriptor set currently in use by a command buffer
        // that is pending execution is technically a race condition.
        // For this research engine, we rely on the fact that this happens at the start of the frame
        // (AssetManager::Update) before the render pass records commands.
        vkUpdateDescriptorSets(m_Device->GetLogicalDevice(), 1, &descriptorWrite, 0, nullptr);
    }


    void Material::WriteDescriptor(VkBuffer cameraBuffer, VkDeviceSize range) {
        // Update the UBO part (Binding 0)
        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = cameraBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = range;

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = m_DescriptorSet;
        descriptorWrite.dstBinding = 0; // Binding 0 = Camera
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferInfo;

        vkUpdateDescriptorSets(m_Device->GetLogicalDevice(), 1, &descriptorWrite, 0, nullptr);
    }
}
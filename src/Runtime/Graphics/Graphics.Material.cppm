module;
#include <volk.h>
#include <memory>
#include <string>

export module Runtime.Graphics.Material;

import Runtime.RHI.Device;
import Runtime.RHI.Texture;
import Runtime.RHI.Pipeline;
import Runtime.RHI.Descriptors;
import Core.Assets;

export namespace Runtime::Graphics
{
    class Material
    {
    public:
        Material(RHI::VulkanDevice& device,
                 RHI::DescriptorPool& pool,
                 const RHI::DescriptorLayout& layout, // We share the layout
                 Core::Assets::AssetHandle textureHandle);

        bool Prepare(Core::Assets::AssetManager &assetManager);

        // We need to update the UBO (Camera) descriptor
        void WriteDescriptor(VkBuffer cameraBuffer, VkDeviceSize range);

        [[nodiscard]] VkDescriptorSet GetDescriptorSet() const { return m_DescriptorSet; }
        [[nodiscard]] Core::Assets::AssetHandle GetTextureHandle() const { return m_TextureHandle; }

    private:
        RHI::VulkanDevice& m_Device;
        VkDescriptorSet m_DescriptorSet = VK_NULL_HANDLE;
        Core::Assets::AssetHandle m_TextureHandle;

        bool m_IsDescriptorWritten = false;
        VkBuffer m_PendingBuffer = VK_NULL_HANDLE;
        VkDeviceSize m_PendingRange = 0;
    };
}

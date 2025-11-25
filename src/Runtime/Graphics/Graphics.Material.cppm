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
                 Core::Assets::AssetHandle textureHandle,
                 std::shared_ptr<RHI::Texture> defaultTexture,
                 Core::Assets::AssetManager& assetManager);

        // We need to update the UBO (Camera) descriptor
        void WriteDescriptor(VkBuffer cameraBuffer, VkDeviceSize range);

        [[nodiscard]] VkDescriptorSet GetDescriptorSet() const { return m_DescriptorSet; }

    private:
        RHI::VulkanDevice& m_Device;
        VkDescriptorSet m_DescriptorSet = VK_NULL_HANDLE;
        std::shared_ptr<RHI::Texture> m_CurrentTexture;

        // Internal helper to perform the Vulkan write
        void UpdateImageWrite(const RHI::Texture& texture);
    };
}

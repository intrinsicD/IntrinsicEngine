module;
#include <volk.h>
#include <memory>
#include <string>

export module Runtime.Graphics.Material;

import Runtime.RHI.Device;
import Runtime.RHI.Texture;
import Runtime.RHI.Pipeline;
import Runtime.RHI.Descriptors;

export namespace Runtime::Graphics
{
    class Material
    {
    public:
        Material(RHI::VulkanDevice& device,
                 RHI::DescriptorPool& pool,
                 const RHI::DescriptorLayout& layout, // We share the layout
                 const std::string& texturePath);

        // We need to update the UBO (Camera) descriptor
        void WriteDescriptor(VkBuffer cameraBuffer, VkDeviceSize range);

        [[nodiscard]] VkDescriptorSet GetDescriptorSet() const { return m_DescriptorSet; }
        [[nodiscard]] RHI::Texture* GetTexture() const { return m_Texture.get(); }

    private:
        std::unique_ptr<RHI::Texture> m_Texture;
        VkDescriptorSet m_DescriptorSet = VK_NULL_HANDLE;
        RHI::VulkanDevice& m_Device;
    };
}

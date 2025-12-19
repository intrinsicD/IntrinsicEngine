module;
#include <volk.h>
#include <memory>

export module Runtime.Graphics.Material;

import Runtime.RHI.Device;
import Runtime.RHI.Bindless; // New import
import Runtime.RHI.Texture;
import Core.Assets;

export namespace Runtime::Graphics
{
    class Material
    {
    public:
        Material(std::shared_ptr<RHI::VulkanDevice> device,
                 RHI::BindlessDescriptorSystem& bindlessSystem, // Pass bindless system
                 Core::Assets::AssetHandle textureHandle,
                 std::shared_ptr<RHI::Texture> defaultTexture,
                 Core::Assets::AssetManager& assetManager);

        ~Material();

        // Returns the index into the bindless array
        [[nodiscard]] uint32_t GetTextureIndex() const { return m_TextureIndex; }

    private:
        std::shared_ptr<RHI::VulkanDevice> m_Device;
        RHI::BindlessDescriptorSystem& m_BindlessSystem;

        uint32_t m_TextureIndex = 0;
        std::shared_ptr<RHI::Texture> m_CurrentTexture;
    };
}
module;
#include <volk.h>

export module Graphics:Material;

import RHI;
import Core;

export namespace Graphics
{
    class Material
    {
    public:
        Material(RHI::VulkanDevice& device,
                 RHI::BindlessDescriptorSystem& bindlessSystem,
                 RHI::TextureSystem& textureSystem,
                 Core::Assets::AssetHandle textureHandle,
                 uint32_t defaultTextureIndex,
                 Core::Assets::AssetManager& assetManager);

        ~Material();

        Material(const Material&) = delete;
        Material& operator=(const Material&) = delete;
        Material(Material&&) = delete;
        Material& operator=(Material&&) = delete;

        // Returns the index into the bindless array
        [[nodiscard]] uint32_t GetTextureIndex() const { return m_TextureIndex; }

    private:
        RHI::VulkanDevice& m_Device;
        RHI::BindlessDescriptorSystem& m_BindlessSystem;
        RHI::TextureSystem& m_TextureSystem;

        Core::Assets::AssetHandle m_TextureHandle{};

        // Hot-path binding data
        uint32_t m_TextureIndex = 0;
    };
}
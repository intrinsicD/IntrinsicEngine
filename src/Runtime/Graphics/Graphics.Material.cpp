module;
#include "RHI.Vulkan.hpp"
#include <memory>

module Graphics:Material.Impl;
import :Material;
import Core;

namespace Graphics {

    Material::Material(RHI::VulkanDevice& device,
                       Core::Assets::AssetHandle textureHandle,
                       uint32_t defaultTextureIndex,
                       Core::Assets::AssetManager& assetManager)
        : m_Device(device)
        , m_TextureHandle(textureHandle)
        , m_TextureIndex(defaultTextureIndex)
    {
        // 1. Initial Setup: If texture is already loaded, grab its index immediately
        if (auto* tex = assetManager.TryGetFast<RHI::Texture>(textureHandle)) {
            m_TextureIndex = tex->GetBindlessIndex();
        }

        // 2. Listener: Update index when asset loads/reloads
        assetManager.Listen(textureHandle, [this, &assetManager](Core::Assets::AssetHandle handle)
        {
            if (auto* tex = assetManager.TryGetFast<RHI::Texture>(handle))
            {
                // ATOMIC UPDATE: Just swap the integer index.
                // The TextureSystem already ensured the descriptor at this index is valid.
                m_TextureIndex = tex->GetBindlessIndex();
            }
        });
    }

    Material::~Material()
    {

    }
}
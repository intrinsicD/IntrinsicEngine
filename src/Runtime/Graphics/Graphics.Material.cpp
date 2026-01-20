module;
#include "RHI.Vulkan.hpp"
#include <memory>

module Graphics:Material.Impl;
import :Material;
import Core;

namespace Graphics {

    Material::Material(RHI::VulkanDevice& device,
                       RHI::BindlessDescriptorSystem& bindlessSystem,
                       Core::Assets::AssetHandle textureHandle,
                       uint32_t defaultTextureIndex,
                       Core::Assets::AssetManager& assetManager)
        : m_Device(device)
        , m_BindlessSystem(bindlessSystem)
        , m_TextureHandle(textureHandle)
        , m_TextureIndex(defaultTextureIndex)
    {
        // Keep a stable bindless slot index for the lifetime of this material.
        // We only update the slot contents when the texture asset becomes ready/reloads.
        assetManager.Listen(textureHandle, [this, &assetManager](Core::Assets::AssetHandle handle)
        {
            // Hot-path safe: no shared_ptr copies; just a pointer lookup.
            if (auto* tex = assetManager.TryGetFast<RHI::Texture>(handle))
            {
                m_BindlessSystem.UpdateTexture(m_TextureIndex, *tex);
            }
        });
    }

    Material::~Material()
    {
        // NOTE: this does not destroy the texture; it only releases the bindless slot.
        // Slot recycling is deferred inside BindlessDescriptorSystem via SafeDestroy().
        m_BindlessSystem.UnregisterTexture(m_TextureIndex);
    }
}
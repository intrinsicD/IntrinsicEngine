module;
#include "RHI.Vulkan.hpp"
#include <memory>

module Graphics:Material.Impl;
import :Material;
import Core;

namespace Graphics {

    Material::Material(RHI::VulkanDevice& device,
                       RHI::BindlessDescriptorSystem& bindlessSystem,
                       RHI::TextureSystem& textureSystem,
                       Core::Assets::AssetHandle textureHandle,
                       uint32_t defaultTextureIndex,
                       Core::Assets::AssetManager& assetManager)
        : m_Device(device)
        , m_BindlessSystem(bindlessSystem)
        , m_TextureSystem(textureSystem)
        , m_TextureHandle(textureHandle)
        , m_TextureIndex(defaultTextureIndex)
    {
        // Keep a stable bindless slot index for the lifetime of this material.
        // We only update the slot contents when the texture asset becomes ready/reloads.
        assetManager.Listen(textureHandle, [this, &assetManager](Core::Assets::AssetHandle handle)
        {
            if (auto* tex = assetManager.TryGetFast<RHI::Texture>(handle))
            {
                const auto h = tex->GetHandle();
                if (const auto* data = m_TextureSystem.Get(h))
                {
                    m_BindlessSystem.UpdateTexture(m_TextureIndex, data->Image->GetView(), data->Sampler);
                }
            }
        });
    }

    Material::~Material()
    {
        // NOTE: this does not destroy the texture; it only releases the bindless slot.
        m_BindlessSystem.UnregisterTexture(m_TextureIndex);
    }
}
module;
#include "RHI.Vulkan.hpp"
#include <memory>

module Graphics:Material.Impl;
import :Material;
import Core;

namespace Graphics {

    Material::Material(std::shared_ptr<RHI::VulkanDevice> device,
                       RHI::BindlessDescriptorSystem& bindlessSystem,
                       Core::Assets::AssetHandle textureHandle,
                       std::shared_ptr<RHI::Texture> defaultTexture,
                       Core::Assets::AssetManager& assetManager)
        : m_Device(device), m_BindlessSystem(bindlessSystem), m_CurrentTexture(defaultTexture)
    {
        if (!m_CurrentTexture)
        {
            Core::Log::Error("Material initialized with null default texture!");
        }
        else if (m_CurrentTexture->GetView() == VK_NULL_HANDLE)
        {
            Core::Log::Error("Material initialized with default texture having NULL view!");
        }

        // Register default texture immediately
        m_TextureIndex = m_BindlessSystem.RegisterTexture(*m_CurrentTexture);

        assetManager.Listen(textureHandle, [this, &assetManager](Core::Assets::AssetHandle handle)
        {
            auto newTexture = assetManager.Get<RHI::Texture>(handle);
            if (newTexture)
            {
                m_CurrentTexture = newTexture;
                // Update the existing slot! No new descriptor set needed.
                m_BindlessSystem.UpdateTexture(m_TextureIndex, *m_CurrentTexture);
            }
        });
    }

    Material::~Material() {
        // Optional: Release slot if we want to recycle indices
        m_BindlessSystem.UnregisterTexture(m_TextureIndex);
    }
}
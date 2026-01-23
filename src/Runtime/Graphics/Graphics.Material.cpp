// src/Runtime/Graphics/Graphics.Material.cpp
module;
#include <utility>

module Graphics:Material.Impl;
import :Material;
import :MaterialSystem;

namespace Graphics
{
    Material::Material(MaterialSystem& system, const MaterialData& initialData)
        : m_System(&system)
    {
        m_Handle = m_System->Create(initialData);
    }

    Material::~Material()
    {
        if (m_System && m_Handle.IsValid())
        {
            m_System->Destroy(m_Handle);
        }
    }

    Material::Material(Material&& other) noexcept
        : m_System(other.m_System), m_Handle(other.m_Handle)
    {
        other.m_System = nullptr;
        other.m_Handle = {};
    }

    Material& Material::operator=(Material&& other) noexcept
    {
        if (this != &other)
        {
            if (m_System && m_Handle.IsValid()) m_System->Destroy(m_Handle);
            m_System = other.m_System;
            m_Handle = other.m_Handle;
            other.m_System = nullptr;
            other.m_Handle = {};
        }
        return *this;
    }

    void Material::SetAlbedoTexture(Core::Assets::AssetHandle textureAsset)
    {
        if (m_System && m_Handle.IsValid())
        {
            m_System->SetAlbedoAsset(m_Handle, textureAsset);
        }
    }
}

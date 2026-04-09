// src/Runtime/Graphics/Graphics.MaterialRegistry.cpp
module;
#include <mutex>
#include <string_view>

module Graphics.Material;

import Core.Assets;
import Core.Logging;
import RHI.Texture;
import RHI.TextureManager;

namespace Graphics
{
    MaterialRegistry::MaterialRegistry(RHI::TextureManager& textureManager, Core::Assets::AssetManager& assetManager)
        : m_TextureManager(textureManager), m_AssetManager(assetManager)
    {
        m_Revisions.resize(1024u, 1u);
    }

    MaterialRegistry::~MaterialRegistry()
    {
        std::lock_guard lock(m_ListenerMutex);
        for(auto& [mat, listeners] : m_Listeners) {
            for(auto& entry : listeners) {
                m_AssetManager.Unlisten(entry.Asset, entry.CallbackID);
            }
        }
        m_Pool.Clear();
    }

    uint32_t MaterialRegistry::GetRevision(MaterialHandle handle) const
    {
        if (!handle.IsValid())
            return 0u;
        if (handle.Index >= m_Revisions.size())
            return 0u;
        return m_Revisions[handle.Index];
    }

    MaterialHandle MaterialRegistry::Create(const MaterialData& data)
    {
        MaterialHandle h = m_Pool.Create(data);
        if (h.IsValid())
        {
            if (h.Index >= m_Revisions.size())
                m_Revisions.resize(static_cast<size_t>(h.Index) + 1u, 1u);
            ++m_Revisions[h.Index];
        }
        return h;
    }

    void MaterialRegistry::Destroy(MaterialHandle handle)
    {
        if(!handle.IsValid()) return;

        // Bump revision so any cached per-entity state will refresh if the handle is (incorrectly) reused.
        if (handle.Index < m_Revisions.size())
            ++m_Revisions[handle.Index];

        {
            std::lock_guard lock(m_ListenerMutex);
            auto it = m_Listeners.find(handle);
            if(it != m_Listeners.end()) {
                for(auto& entry : it->second) {
                    m_AssetManager.Unlisten(entry.Asset, entry.CallbackID);
                }
                m_Listeners.erase(it);
            }
        }

        // Mark for deletion.
        // NOTE: We assume Engine calls ProcessDeletions with valid frame index.
        // If called from ~Material(), we might not know the exact frame index easily
        // without passing it in. Using 0 implies "delete when safe" if logic allows,
        // but ideally we pass m_Device.GetGlobalFrameNumber().
        // For now, we assume 0 is handled or we update signature later.
        m_Pool.Remove(handle, 0);
    }

    void MaterialRegistry::ProcessDeletions(uint64_t currentFrame)
    {
        m_Pool.ProcessDeletions(currentFrame);
    }

    const MaterialData* MaterialRegistry::GetData(MaterialHandle handle) const
    {
        auto res = m_Pool.Get(handle);
        return res ? *res : nullptr;
    }

    MaterialData* MaterialRegistry::GetData(MaterialHandle handle)
    {
        auto res = m_Pool.Get(handle);
        return res ? *res : nullptr;
    }

    void MaterialRegistry::SetAlbedoAsset(MaterialHandle material, Core::Assets::AssetHandle textureAsset)
    {
        BindTextureAsset(material, textureAsset, TextureSlot::Albedo);
    }

    void MaterialRegistry::SetNormalAsset(MaterialHandle material, Core::Assets::AssetHandle textureAsset)
    {
        BindTextureAsset(material, textureAsset, TextureSlot::Normal);
    }

    void MaterialRegistry::SetMetallicRoughnessAsset(MaterialHandle material, Core::Assets::AssetHandle textureAsset)
    {
        BindTextureAsset(material, textureAsset, TextureSlot::MetallicRoughness);
    }

    void MaterialRegistry::BindTextureAsset(MaterialHandle material, Core::Assets::AssetHandle textureAsset, TextureSlot slot)
    {
        std::lock_guard lock(m_ListenerMutex);

        auto callback = [this, material, slot](Core::Assets::AssetHandle texHandle) {
            this->OnTextureLoad(material, texHandle, slot);
        };

        const auto listenerID = m_AssetManager.Listen(textureAsset, callback);
        m_Listeners[material].push_back({textureAsset, listenerID});
    }

    void MaterialRegistry::OnTextureLoad(MaterialHandle matHandle, Core::Assets::AssetHandle texHandle, TextureSlot slot)
    {
        // 1. Get the RHI Texture (Asset Payload)
        auto* tex = m_AssetManager.TryGet<RHI::Texture>(texHandle);
        if (!tex) return;

        // 2. Get the Bindless Index
        uint32_t bindlessID = tex->GetBindlessIndex();

        // DEBUG: trace material->texture binding updates.
        constexpr auto slotToString = [](TextureSlot textureSlot) -> std::string_view {
            switch (textureSlot)
            {
                case TextureSlot::Albedo: return "Albedo";
                case TextureSlot::Normal: return "Normal";
                case TextureSlot::MetallicRoughness: return "MetallicRoughness";
            }
            return "Unknown";
        };

        Core::Log::Info("[MaterialRegistry] OnTextureLoad: mat(index={}, gen={}) texAsset(id={}) -> bindlessSlot={} slot={}",
                        matHandle.Index, matHandle.Generation,
                        static_cast<uint32_t>(texHandle.ID),
                        bindlessID, slotToString(slot));

        // 3. Update Material Data in Pool
        if (auto* data = GetData(matHandle))
        {
            switch (slot)
            {
                case TextureSlot::Albedo: data->AlbedoID = bindlessID; break;
                case TextureSlot::Normal: data->NormalID = bindlessID; break;
                case TextureSlot::MetallicRoughness: data->MetallicRoughnessID = bindlessID; break;
            }

            if (matHandle.Index >= m_Revisions.size())
                m_Revisions.resize(static_cast<size_t>(matHandle.Index) + 1u, 1u);
            ++m_Revisions[matHandle.Index];
        }
    }
}
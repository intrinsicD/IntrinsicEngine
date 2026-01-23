// src/Runtime/Graphics/Graphics.MaterialSystem.cpp
module;
#include <mutex>

module Graphics:MaterialSystem.Impl;
import :MaterialSystem;
import Core;
import RHI;

namespace Graphics
{
    MaterialSystem::MaterialSystem(RHI::TextureSystem& textureSystem, Core::Assets::AssetManager& assetManager)
        : m_TextureSystem(textureSystem), m_AssetManager(assetManager)
    {
        m_Pool.Initialize(2); // 2 Frames in flight
    }

    MaterialSystem::~MaterialSystem()
    {
        std::lock_guard lock(m_ListenerMutex);
        for(auto& [mat, listeners] : m_Listeners) {
            for(auto& entry : listeners) {
                m_AssetManager.Unlisten(entry.Asset, entry.CallbackID);
            }
        }
        m_Pool.Clear();
    }

    MaterialHandle MaterialSystem::Create(const MaterialData& data)
    {
        return m_Pool.Create(data);
    }

    void MaterialSystem::Destroy(MaterialHandle handle)
    {
        if(!handle.IsValid()) return;

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

    void MaterialSystem::ProcessDeletions(uint64_t currentFrame)
    {
        m_Pool.ProcessDeletions(currentFrame);
    }

    const MaterialData* MaterialSystem::GetData(MaterialHandle handle) const
    {
        auto res = m_Pool.Get(handle);
        return res ? *res : nullptr;
    }

    MaterialData* MaterialSystem::GetData(MaterialHandle handle)
    {
        auto res = m_Pool.Get(handle);
        return res ? *res : nullptr;
    }

    void MaterialSystem::SetAlbedoAsset(MaterialHandle material, Core::Assets::AssetHandle textureAsset)
    {
        std::lock_guard lock(m_ListenerMutex);

        auto callback = [this, material](Core::Assets::AssetHandle texHandle) {
            this->OnTextureLoad(material, texHandle, 0);
        };

        // Fire immediately if ready, otherwise register
        auto listenerID = m_AssetManager.Listen(textureAsset, callback);
        m_Listeners[material].push_back({textureAsset, listenerID});
    }

    void MaterialSystem::OnTextureLoad(MaterialHandle matHandle, Core::Assets::AssetHandle texHandle, int slotType)
    {
        // 1. Get the RHI Texture (Asset Payload)
        auto* tex = m_AssetManager.TryGet<RHI::Texture>(texHandle);
        if (!tex) return;

        // 2. Get the Bindless Index
        uint32_t bindlessID = tex->GetBindlessIndex();

        // 3. Update Material Data in Pool
        // We use GetUnchecked here because we are inside a callback potentially off-frame,
        // but we want to ensure the slot is still valid (Get checks generation).
        // const_cast is necessary because listeners update data.
        if (auto* data = const_cast<MaterialData*>(GetData(matHandle)))
        {
            if (slotType == 0) data->AlbedoID = bindlessID;
            // if (slotType == 1) data->NormalID = bindlessID;
        }
    }
}
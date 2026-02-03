// src/Runtime/Graphics/Graphics.MaterialSystem.cppm
module;
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

export module Graphics:MaterialSystem;

import :Material;
import Core;
import RHI;

export namespace Graphics
{
    class MaterialSystem
    {
    public:
        MaterialSystem(RHI::TextureSystem& textureSystem, Core::Assets::AssetManager& assetManager);
        ~MaterialSystem();

        // Lifecycle (Called by Material RAII wrapper)
        [[nodiscard]] MaterialHandle Create(const MaterialData& data);
        void Destroy(MaterialHandle handle);

        // Maintenance (Called by Engine loop)
        void ProcessDeletions(uint64_t currentFrame);

        // Data Access (Fast Path for Render Passes)
        // Returns nullptr if handle is invalid/stale
        [[nodiscard]] const MaterialData* GetData(MaterialHandle handle) const;
        [[nodiscard]] MaterialData* GetData(MaterialHandle handle);

        // Binding Logic
        void SetAlbedoAsset(MaterialHandle material, Core::Assets::AssetHandle textureAsset);

        // Signal that a material's GPU-facing data changed (e.g. bindless texture index) and instances should refresh.
        [[nodiscard]] uint32_t GetRevision(MaterialHandle handle) const;

    private:
        RHI::TextureSystem& m_TextureSystem;
        Core::Assets::AssetManager& m_AssetManager;

        Core::ResourcePool<MaterialData, MaterialHandle> m_Pool;

        struct ListenerEntry
        {
            Core::Assets::AssetHandle Asset;
            Core::Assets::ListenerHandle CallbackID;
        };

        std::mutex m_ListenerMutex;
        std::unordered_map<MaterialHandle, std::vector<ListenerEntry>> m_Listeners;

        // Monotonic per-material revision used for fast dirty checks.
        // Index is MaterialHandle::Index.
        std::vector<uint32_t> m_Revisions;

        void OnTextureLoad(MaterialHandle matHandle, Core::Assets::AssetHandle texHandle, int slotType);
    };
}
// src/Runtime/Graphics/Graphics.Material.cppm
module;
#include <mutex>
#include <unordered_map>
#include <vector>

#include <cstdint>
#include <glm/glm.hpp>

export module Graphics.Material;

import Core.Assets;
import Core.Handle;
import Core.ResourcePool;
import RHI;

export namespace Graphics
{
    // 1. Strong Handle (The "Key")
    struct MaterialTag {};
    using MaterialHandle = Core::StrongHandle<MaterialTag>;

    // 2. The Data (The "Body" - POD for the Pool)
    struct MaterialData
    {
        glm::vec4 BaseColorFactor{1.0f};
        float MetallicFactor = 1.0f;
        float RoughnessFactor = 1.0f;

        // Bindless Indices (0 = default/error texture)
        uint32_t AlbedoID = 0;
        uint32_t NormalID = 0;
        uint32_t MetallicRoughnessID = 0;
    };

    class MaterialSystem
    {
    public:
        enum class TextureSlot : uint8_t
        {
            Albedo,
            Normal,
            MetallicRoughness,
        };

        MaterialSystem(RHI::TextureSystem& textureSystem, Core::Assets::AssetManager& assetManager);
        ~MaterialSystem();

        [[nodiscard]] MaterialHandle Create(const MaterialData& data);
        void Destroy(MaterialHandle handle);
        void ProcessDeletions(uint64_t currentFrame);

        [[nodiscard]] const MaterialData* GetData(MaterialHandle handle) const;
        [[nodiscard]] MaterialData* GetData(MaterialHandle handle);

        void SetAlbedoAsset(MaterialHandle material, Core::Assets::AssetHandle textureAsset);
        void SetNormalAsset(MaterialHandle material, Core::Assets::AssetHandle textureAsset);
        void SetMetallicRoughnessAsset(MaterialHandle material, Core::Assets::AssetHandle textureAsset);

        [[nodiscard]] uint32_t GetRevision(MaterialHandle handle) const;

    private:
        RHI::TextureSystem& m_TextureSystem;
        Core::Assets::AssetManager& m_AssetManager;
        Core::ResourcePool<MaterialData, MaterialHandle, 3> m_Pool;

        struct ListenerEntry
        {
            Core::Assets::AssetHandle Asset;
            Core::Assets::ListenerHandle CallbackID;
        };

        std::mutex m_ListenerMutex;
        std::unordered_map<MaterialHandle, std::vector<ListenerEntry>> m_Listeners;
        std::vector<uint32_t> m_Revisions;

        void BindTextureAsset(MaterialHandle material, Core::Assets::AssetHandle textureAsset, TextureSlot slot);
        void OnTextureLoad(MaterialHandle matHandle, Core::Assets::AssetHandle texHandle, TextureSlot slot);
    };

    // 3. The Public Resource (RAII Wrapper)
    // AssetManager holds std::shared_ptr<Material>.
    // This class ensures the pool slot is freed when the Asset is unloaded.
    class Material
    {
    public:
        Material(MaterialSystem& system, const MaterialData& initialData);
        ~Material();

        Material(const Material&) = delete;
        Material& operator=(const Material&) = delete;
        Material(Material&&) noexcept;
        Material& operator=(Material&&) noexcept;

        [[nodiscard]] MaterialHandle GetHandle() const { return m_Handle; }

        // Link texture assets to specific material slots.
        void SetAlbedoTexture(Core::Assets::AssetHandle textureAsset);
        void SetNormalTexture(Core::Assets::AssetHandle textureAsset);
        void SetMetallicRoughnessTexture(Core::Assets::AssetHandle textureAsset);

    private:
        MaterialSystem* m_System = nullptr;
        MaterialHandle m_Handle;
    };
}
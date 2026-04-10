// src/Runtime/Graphics/Graphics.Material.cppm
module;
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <cstdint>
#include <glm/glm.hpp>

#include "RHI.Vulkan.hpp"

export module Graphics.Material;

import Asset.Manager;
import Core.Handle;
import Core.ResourcePool;
import RHI.Buffer;
import RHI.Device;
import RHI.Texture;
import RHI.TextureManager;

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

    // GPU-side material layout (std430-compatible, 48 bytes per entry).
    // Matches the MaterialSSBO binding in surface shaders (set=3, binding=0).
    struct GpuMaterialData
    {
        glm::vec4 BaseColorFactor{1.0f};     // 16 bytes
        float MetallicFactor = 1.0f;          //  4 bytes
        float RoughnessFactor = 1.0f;         //  4 bytes
        uint32_t AlbedoID = 0;                //  4 bytes (bindless texture index)
        uint32_t NormalID = 0;                //  4 bytes
        uint32_t MetallicRoughnessID = 0;     //  4 bytes
        uint32_t Flags = 0;                   //  4 bytes (reserved for alpha mode, double-sided, etc.)
        uint32_t _pad0 = 0;                   //  4 bytes
        uint32_t _pad1 = 0;                   //  4 bytes
    };
    static_assert(sizeof(GpuMaterialData) == 48, "GpuMaterialData must be 48 bytes for SSBO alignment");

    class MaterialRegistry
    {
    public:
        enum class TextureSlot : uint8_t
        {
            Albedo,
            Normal,
            MetallicRoughness,
        };

        MaterialRegistry(RHI::VulkanDevice& device,
                         RHI::TextureManager& textureManager,
                         Core::Assets::AssetManager& assetManager);
        ~MaterialRegistry();

        [[nodiscard]] MaterialHandle Create(const MaterialData& data);
        void Destroy(MaterialHandle handle);
        void ProcessDeletions(uint64_t currentFrame);

        [[nodiscard]] const MaterialData* GetData(MaterialHandle handle) const;
        [[nodiscard]] MaterialData* GetData(MaterialHandle handle);

        void SetAlbedoAsset(MaterialHandle material, Core::Assets::AssetHandle textureAsset);
        void SetNormalAsset(MaterialHandle material, Core::Assets::AssetHandle textureAsset);
        void SetMetallicRoughnessAsset(MaterialHandle material, Core::Assets::AssetHandle textureAsset);

        [[nodiscard]] uint32_t GetRevision(MaterialHandle handle) const;

        // --- GPU Material Buffer ---

        // Upload dirty material data to the GPU SSBO. Call once per frame
        // before rendering (after all ECS systems that modify materials).
        void SyncGpuBuffer();

        // Descriptor set containing the material SSBO (set=3, binding=0).
        [[nodiscard]] VkDescriptorSet GetMaterialDescriptorSet() const { return m_MaterialDescriptorSet; }

        // Descriptor set layout for the material SSBO binding.
        [[nodiscard]] VkDescriptorSetLayout GetMaterialSetLayout() const { return m_MaterialSetLayout; }

    private:
        RHI::VulkanDevice& m_Device;
        RHI::TextureManager& m_TextureManager;
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

        // GPU material buffer state.
        std::unique_ptr<RHI::VulkanBuffer> m_GpuBuffer;
        uint32_t m_GpuBufferCapacity = 0;
        VkDescriptorSetLayout m_MaterialSetLayout = VK_NULL_HANDLE;
        VkDescriptorPool m_MaterialDescriptorPool = VK_NULL_HANDLE;
        VkDescriptorSet m_MaterialDescriptorSet = VK_NULL_HANDLE;
        bool m_GpuDirty = true;

        void InitGpuResources();
        void EnsureGpuBufferCapacity(uint32_t requiredSlots);
        void UpdateDescriptorSet();

        void BindTextureAsset(MaterialHandle material, Core::Assets::AssetHandle textureAsset, TextureSlot slot);
        void OnTextureLoad(MaterialHandle matHandle, Core::Assets::AssetHandle texHandle, TextureSlot slot);
    };

    // 3. The Public Resource (RAII Wrapper)
    // AssetManager holds std::shared_ptr<Material>.
    // This class ensures the pool slot is freed when the Asset is unloaded.
    class Material
    {
    public:
        Material(MaterialRegistry& system, const MaterialData& initialData);
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
        MaterialRegistry* m_System = nullptr;
        MaterialHandle m_Handle;
    };
}
// src/Runtime/Graphics/Graphics.Material.cppm
module;
#include <glm/glm.hpp>

export module Graphics:Material;

import Core.Handle;
import Core.Assets;

export namespace Graphics
{
    // Forward declare System to break dependency cycle
    class MaterialSystem;

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

        // Link a texture asset to a specific slot on this material
        void SetAlbedoTexture(Core::Assets::AssetHandle textureAsset);

    private:
        MaterialSystem* m_System = nullptr;
        MaterialHandle m_Handle;
    };
}
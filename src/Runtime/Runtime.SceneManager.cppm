module;
#include <entt/fwd.hpp>
#include <glm/glm.hpp>

export module Runtime.SceneManager;

import Core;
import Graphics;
import ECS;

export namespace Runtime
{
    // Owns the ECS scene, entity lifecycle management, and EnTT hooks
    // for GPU resource reclamation.  Extracted from Engine following
    // the GraphicsBackend / AssetPipeline pattern.
    class SceneManager
    {
    public:
        SceneManager();
        ~SceneManager();

        // Non-copyable, non-movable (owns EnTT registry + hook state).
        SceneManager(const SceneManager&) = delete;
        SceneManager& operator=(const SceneManager&) = delete;
        SceneManager(SceneManager&&) = delete;
        SceneManager& operator=(SceneManager&&) = delete;

        // --- Scene access ---
        [[nodiscard]] ECS::Scene& GetScene() { return m_Scene; }
        [[nodiscard]] const ECS::Scene& GetScene() const { return m_Scene; }
        [[nodiscard]] entt::registry& GetRegistry() { return m_Scene.GetRegistry(); }
        [[nodiscard]] const entt::registry& GetRegistry() const { return m_Scene.GetRegistry(); }

        // --- GPU hook management ---

        // Connect the EnTT on_destroy<MeshRenderer> hook so that GPU slots
        // are reclaimed immediately when entities are destroyed.
        void ConnectGpuHooks(Graphics::GPUScene& gpuScene);
        void DisconnectGpuHooks();

        // --- Entity lifetime ---

        // Spawn an entity hierarchy for a loaded model asset.
        entt::entity SpawnModel(Core::Assets::AssetManager& assetManager,
                                Core::Assets::AssetHandle modelHandle,
                                Core::Assets::AssetHandle materialHandle,
                                glm::vec3 position,
                                glm::vec3 scale = glm::vec3(1.0f));

        // --- Shutdown ---

        // Clear all entities from the registry.
        void Clear();

    private:
        ECS::Scene m_Scene;
    };
}

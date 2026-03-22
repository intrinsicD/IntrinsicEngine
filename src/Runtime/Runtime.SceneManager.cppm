module;
#include <cstdint>
#include <entt/fwd.hpp>
#include <glm/glm.hpp>

export module Runtime.SceneManager;

import Core.Assets;
import Graphics.Components;
import Graphics.Geometry;
import Graphics.GPUScene;
import ECS;
#ifdef INTRINSIC_HAS_CUDA
import RHI.CudaDevice;
#endif

export namespace Runtime
{
    struct WorldSnapshot
    {
        const ECS::Scene* Scene = nullptr;
        const entt::registry* Registry = nullptr;
        uint64_t CommittedTick = 0;

        [[nodiscard]] bool IsValid() const { return Scene != nullptr && Registry != nullptr; }
    };

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
        [[nodiscard]] uint64_t GetCommittedTick() const { return m_CommittedTick; }
        [[nodiscard]] WorldSnapshot CreateReadonlySnapshot() const;

        // Marks the authoritative ECS world state as consistent after a completed
        // fixed simulation tick. Rendering/extraction code can key snapshot reads
        // against this monotonically increasing commit number.
        void CommitFixedTick();

        // --- GPU hook management ---

        // Connect the EnTT on_destroy<MeshRenderer> hook so that GPU slots
        // are reclaimed immediately when entities are destroyed.
        void ConnectGpuHooks(Graphics::GPUScene& gpuScene
#ifdef INTRINSIC_HAS_CUDA
                             , RHI::CudaDevice* cudaDevice = nullptr
#endif
        );
        void DisconnectGpuHooks();

        // Provide the geometry pool so SpawnModel can inspect topology.
        void SetGeometryStorage(Graphics::GeometryPool* storage) { m_GeometryStorage = storage; }

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
        struct GpuHookContext
        {
            Graphics::GPUScene* GpuScene = nullptr;
#ifdef INTRINSIC_HAS_CUDA
            RHI::CudaDevice* CudaDevice = nullptr;
#endif
        };

        template <typename T>
        void OnGpuComponentDestroyed(entt::registry& registry, entt::entity entity);

        ECS::Scene m_Scene;
        Graphics::GeometryPool* m_GeometryStorage = nullptr;
        GpuHookContext m_GpuHookContext{};
        uint64_t m_CommittedTick = 0;
    };
}

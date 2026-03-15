module;
#include <string>
#include <cstdint>
#include <entt/entity/registry.hpp>
#include <entt/signal/dispatcher.hpp>
#include <glm/glm.hpp>

module Runtime.SceneManager;

import Core.Logging;
import Core.Assets;
import Graphics;
import ECS;

namespace
{
    // File-static pointer for the EnTT on_destroy hook.
    // Safe because there is exactly one SceneManager instance per process.
    Graphics::GPUScene* g_GpuSceneForDestroyHook = nullptr;

    // Shared GPU slot cleanup for all component types that own a GPUScene slot.
    // All such components initialize GpuSlot to ECS::kInvalidGpuSlot.
    template <typename T>
    void OnGpuComponentDestroyed(entt::registry& registry, entt::entity entity)
    {
        if (!g_GpuSceneForDestroyHook)
            return;

        auto& comp = registry.get<T>(entity);
        if (comp.GpuSlot == ECS::kInvalidGpuSlot)
            return;

        // Deactivate slot (radius = 0 => culler skips it) and free.
        Graphics::GpuInstanceData inst{};
        g_GpuSceneForDestroyHook->QueueUpdate(comp.GpuSlot, inst, /*sphere*/ {0.0f, 0.0f, 0.0f, 0.0f});
        g_GpuSceneForDestroyHook->FreeSlot(comp.GpuSlot);
        comp.GpuSlot = ECS::kInvalidGpuSlot;
    }
}

namespace Runtime
{
    SceneManager::SceneManager()
    {
        Core::Log::Info("SceneManager: Initialized.");
    }

    SceneManager::~SceneManager()
    {
        DisconnectGpuHooks();
        Core::Log::Info("SceneManager: Shutdown.");
    }

    void SceneManager::ConnectGpuHooks(Graphics::GPUScene& gpuScene)
    {
        g_GpuSceneForDestroyHook = &gpuScene;
        auto& reg = m_Scene.GetRegistry();
        reg.on_destroy<ECS::Surface::Component>().connect<&OnGpuComponentDestroyed<ECS::Surface::Component>>();
        reg.on_destroy<ECS::MeshEdgeView::Component>().connect<&OnGpuComponentDestroyed<ECS::MeshEdgeView::Component>>();
        reg.on_destroy<ECS::MeshVertexView::Component>().connect<&OnGpuComponentDestroyed<ECS::MeshVertexView::Component>>();
        reg.on_destroy<ECS::Graph::Data>().connect<&OnGpuComponentDestroyed<ECS::Graph::Data>>();
        reg.on_destroy<ECS::PointCloud::Data>().connect<&OnGpuComponentDestroyed<ECS::PointCloud::Data>>();
    }

    void SceneManager::DisconnectGpuHooks()
    {
        auto& reg = m_Scene.GetRegistry();
        reg.on_destroy<ECS::Surface::Component>().disconnect<&OnGpuComponentDestroyed<ECS::Surface::Component>>();
        reg.on_destroy<ECS::MeshEdgeView::Component>().disconnect<&OnGpuComponentDestroyed<ECS::MeshEdgeView::Component>>();
        reg.on_destroy<ECS::MeshVertexView::Component>().disconnect<&OnGpuComponentDestroyed<ECS::MeshVertexView::Component>>();
        reg.on_destroy<ECS::Graph::Data>().disconnect<&OnGpuComponentDestroyed<ECS::Graph::Data>>();
        reg.on_destroy<ECS::PointCloud::Data>().disconnect<&OnGpuComponentDestroyed<ECS::PointCloud::Data>>();
        g_GpuSceneForDestroyHook = nullptr;
    }

    entt::entity SceneManager::SpawnModel(Core::Assets::AssetManager& assetManager,
                                          Core::Assets::AssetHandle modelHandle,
                                          Core::Assets::AssetHandle materialHandle,
                                          glm::vec3 position,
                                          glm::vec3 scale)
    {
        // 1. Resolve Model
        if (auto* model = assetManager.TryGet<Graphics::Model>(modelHandle))
        {
            // 2. Create Root
            std::string name = "Model";

            entt::entity root = m_Scene.CreateEntity(name);
            auto& t = m_Scene.GetRegistry().get<ECS::Components::Transform::Component>(root);
            t.Position = position;
            t.Scale = scale;

            // Assign stable pick IDs (monotonic, never reused during runtime).
            static uint32_t s_NextPickId = 1u;
            if (!m_Scene.GetRegistry().all_of<ECS::Components::Selection::PickID>(root))
            {
                m_Scene.GetRegistry().emplace<ECS::Components::Selection::PickID>(root, s_NextPickId++);
            }

            // 3. Create Submeshes / Point Clouds
            for (size_t i = 0; i < model->Meshes.size(); i++)
            {
                entt::entity targetEntity = root;

                // If complex model, create children. If single mesh, put it on root.
                if (model->Meshes.size() > 1)
                {
                    targetEntity = m_Scene.CreateEntity(model->Meshes[i]->Name);
                    ECS::Components::Hierarchy::Attach(m_Scene.GetRegistry(), targetEntity, root);
                }

                // Route point topologies to PointCloud::Data for PointPass
                // rendering. Meshes/lines use the standard Surface path.
                const auto handle = model->Meshes[i]->Handle;
                const auto* geo = m_GeometryStorage ? m_GeometryStorage->GetIfValid(handle) : nullptr;

                if (geo && geo->GetTopology() == Graphics::PrimitiveTopology::Points)
                {
                    auto& pcd = m_Scene.GetRegistry().emplace<ECS::PointCloud::Data>(targetEntity);
                    pcd.GpuGeometry = handle;
                    pcd.GpuDirty = false; // Already uploaded by ModelLoader.
                    pcd.CloudRef.reset();
                    pcd.HasGpuNormals = (geo->GetLayout().NormalsSize > 0);
                    pcd.GpuPointCount = static_cast<uint32_t>(geo->GetLayout().PositionsSize / sizeof(glm::vec3));
                }
                else
                {
                    auto& sc = m_Scene.GetRegistry().emplace<ECS::Surface::Component>(targetEntity);
                    sc.Geometry = handle;
                    sc.Material = materialHandle;

                    // Add Collider
                    if (model->Meshes[i]->CollisionGeometry)
                    {
                        auto& col = m_Scene.GetRegistry().emplace<ECS::MeshCollider::Component>(targetEntity);
                        col.CollisionRef = model->Meshes[i]->CollisionGeometry;
                        col.WorldOBB.Center = col.CollisionRef->LocalAABB.GetCenter();
                    }
                }

                // Add Selectable Tag
                m_Scene.GetRegistry().emplace<ECS::Components::Selection::SelectableTag>(targetEntity);

                // Stable pick ID for each selectable entity.
                if (!m_Scene.GetRegistry().all_of<ECS::Components::Selection::PickID>(targetEntity))
                {
                    m_Scene.GetRegistry().emplace<ECS::Components::Selection::PickID>(targetEntity, s_NextPickId++);
                }
            }

            m_Scene.GetDispatcher().enqueue<ECS::Events::EntitySpawned>({root});
            return root;
        }

        Core::Log::Error("Cannot spawn model: Asset not ready or invalid.");
        return entt::null;
    }

    void SceneManager::Clear()
    {
        m_Scene.GetRegistry().clear();
    }
}

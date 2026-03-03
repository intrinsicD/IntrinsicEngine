module;
#include <string>
#include <cstdint>
#include <entt/entity/registry.hpp>
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

    void OnMeshRendererDestroyed(entt::registry& registry, entt::entity entity)
    {
        // NOTE: This hook is invoked for every MeshRenderer component destroyed.
        // With Geometry views (wireframe/vertex) we may attach additional MeshRenderer
        // components to the same entity (or helper entities). Each one owns its own
        // GPUScene slot and must be freed here.
        if (!g_GpuSceneForDestroyHook)
            return;

        auto& mr = registry.get<ECS::MeshRenderer::Component>(entity);
        if (mr.GpuSlot == ECS::MeshRenderer::Component::kInvalidSlot)
            return;

        // Deactivate slot (radius = 0 => culler skips it) and free.
        Graphics::GpuInstanceData inst{};
        g_GpuSceneForDestroyHook->QueueUpdate(mr.GpuSlot, inst, /*sphere*/ {0.0f, 0.0f, 0.0f, 0.0f});
        g_GpuSceneForDestroyHook->FreeSlot(mr.GpuSlot);
        mr.GpuSlot = ECS::MeshRenderer::Component::kInvalidSlot;
    }

    void OnGeometryViewRendererDestroyed(entt::registry& registry, entt::entity entity)
    {
        if (!g_GpuSceneForDestroyHook)
            return;

        auto& vr = registry.get<ECS::GeometryViewRenderer::Component>(entity);

        auto freeSlot = [&](uint32_t& slot)
        {
            if (slot == ECS::MeshRenderer::Component::kInvalidSlot)
                return;

            Graphics::GpuInstanceData inst{};
            g_GpuSceneForDestroyHook->QueueUpdate(slot, inst, /*sphere*/ {0.0f, 0.0f, 0.0f, 0.0f});
            g_GpuSceneForDestroyHook->FreeSlot(slot);
            slot = ECS::MeshRenderer::Component::kInvalidSlot;
        };

        freeSlot(vr.SurfaceGpuSlot);
        freeSlot(vr.VerticesGpuSlot);
    }

    void OnPointCloudRendererDestroyed(entt::registry& registry, entt::entity entity)
    {
        if (!g_GpuSceneForDestroyHook)
            return;

        auto& pc = registry.get<ECS::PointCloudRenderer::Component>(entity);
        if (pc.GpuSlot == ECS::PointCloudRenderer::Component::kInvalidSlot)
            return;

        Graphics::GpuInstanceData inst{};
        g_GpuSceneForDestroyHook->QueueUpdate(pc.GpuSlot, inst, /*sphere*/ {0.0f, 0.0f, 0.0f, 0.0f});
        g_GpuSceneForDestroyHook->FreeSlot(pc.GpuSlot);
        pc.GpuSlot = ECS::PointCloudRenderer::Component::kInvalidSlot;
    }

    void OnMeshEdgeViewDestroyed(entt::registry& registry, entt::entity entity)
    {
        if (!g_GpuSceneForDestroyHook)
            return;

        auto& ev = registry.get<ECS::MeshEdgeView::Component>(entity);
        if (ev.GpuSlot == ECS::MeshEdgeView::Component::kInvalidSlot)
            return;

        Graphics::GpuInstanceData inst{};
        g_GpuSceneForDestroyHook->QueueUpdate(ev.GpuSlot, inst, /*sphere*/ {0.0f, 0.0f, 0.0f, 0.0f});
        g_GpuSceneForDestroyHook->FreeSlot(ev.GpuSlot);
        ev.GpuSlot = ECS::MeshEdgeView::Component::kInvalidSlot;
    }

    void OnMeshVertexViewDestroyed(entt::registry& registry, entt::entity entity)
    {
        if (!g_GpuSceneForDestroyHook)
            return;

        auto& pv = registry.get<ECS::MeshVertexView::Component>(entity);
        if (pv.GpuSlot == ECS::MeshVertexView::Component::kInvalidSlot)
            return;

        Graphics::GpuInstanceData inst{};
        g_GpuSceneForDestroyHook->QueueUpdate(pv.GpuSlot, inst, /*sphere*/ {0.0f, 0.0f, 0.0f, 0.0f});
        g_GpuSceneForDestroyHook->FreeSlot(pv.GpuSlot);
        pv.GpuSlot = ECS::MeshVertexView::Component::kInvalidSlot;
    }

    void OnGraphDataDestroyed(entt::registry& registry, entt::entity entity)
    {
        if (!g_GpuSceneForDestroyHook)
            return;

        auto& graphData = registry.get<ECS::Graph::Data>(entity);
        if (graphData.GpuSlot == ECS::Graph::Data::kInvalidSlot)
            return;

        Graphics::GpuInstanceData inst{};
        g_GpuSceneForDestroyHook->QueueUpdate(graphData.GpuSlot, inst, /*sphere*/ {0.0f, 0.0f, 0.0f, 0.0f});
        g_GpuSceneForDestroyHook->FreeSlot(graphData.GpuSlot);
        graphData.GpuSlot = ECS::Graph::Data::kInvalidSlot;
    }

    void OnPointCloudDataDestroyed(entt::registry& registry, entt::entity entity)
    {
        if (!g_GpuSceneForDestroyHook)
            return;

        auto& pcData = registry.get<ECS::PointCloud::Data>(entity);
        if (pcData.GpuSlot == ECS::PointCloud::Data::kInvalidSlot)
            return;

        Graphics::GpuInstanceData inst{};
        g_GpuSceneForDestroyHook->QueueUpdate(pcData.GpuSlot, inst, /*sphere*/ {0.0f, 0.0f, 0.0f, 0.0f});
        g_GpuSceneForDestroyHook->FreeSlot(pcData.GpuSlot);
        pcData.GpuSlot = ECS::PointCloud::Data::kInvalidSlot;
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
        m_Scene.GetRegistry().on_destroy<ECS::MeshRenderer::Component>()
            .connect<&OnMeshRendererDestroyed>();

        m_Scene.GetRegistry().on_destroy<ECS::GeometryViewRenderer::Component>()
            .connect<&OnGeometryViewRendererDestroyed>();

        m_Scene.GetRegistry().on_destroy<ECS::PointCloudRenderer::Component>()
            .connect<&OnPointCloudRendererDestroyed>();

        m_Scene.GetRegistry().on_destroy<ECS::MeshEdgeView::Component>()
            .connect<&OnMeshEdgeViewDestroyed>();

        m_Scene.GetRegistry().on_destroy<ECS::MeshVertexView::Component>()
            .connect<&OnMeshVertexViewDestroyed>();

        m_Scene.GetRegistry().on_destroy<ECS::Graph::Data>()
            .connect<&OnGraphDataDestroyed>();

        m_Scene.GetRegistry().on_destroy<ECS::PointCloud::Data>()
            .connect<&OnPointCloudDataDestroyed>();
    }

    void SceneManager::DisconnectGpuHooks()
    {
        m_Scene.GetRegistry().on_destroy<ECS::MeshRenderer::Component>()
            .disconnect<&OnMeshRendererDestroyed>();

        m_Scene.GetRegistry().on_destroy<ECS::GeometryViewRenderer::Component>()
            .disconnect<&OnGeometryViewRendererDestroyed>();

        m_Scene.GetRegistry().on_destroy<ECS::PointCloudRenderer::Component>()
            .disconnect<&OnPointCloudRendererDestroyed>();

        m_Scene.GetRegistry().on_destroy<ECS::MeshEdgeView::Component>()
            .disconnect<&OnMeshEdgeViewDestroyed>();

        m_Scene.GetRegistry().on_destroy<ECS::MeshVertexView::Component>()
            .disconnect<&OnMeshVertexViewDestroyed>();

        m_Scene.GetRegistry().on_destroy<ECS::Graph::Data>()
            .disconnect<&OnGraphDataDestroyed>();

        m_Scene.GetRegistry().on_destroy<ECS::PointCloud::Data>()
            .disconnect<&OnPointCloudDataDestroyed>();

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

                // Route point topologies to PointCloudRenderer for billboard
                // expansion via PointPass. Meshes/lines use the standard
                // MeshRenderer path.
                const auto handle = model->Meshes[i]->Handle;
                const auto* geo = m_GeometryStorage ? m_GeometryStorage->GetUnchecked(handle) : nullptr;

                if (geo && geo->GetTopology() == Graphics::PrimitiveTopology::Points)
                {
                    auto& pc = m_Scene.GetRegistry().emplace<ECS::PointCloudRenderer::Component>(targetEntity);
                    pc.Geometry = handle;
                    pc.GpuDirty = false; // Already uploaded by ModelLoader.
                }
                else
                {
                    auto& mr = m_Scene.GetRegistry().emplace<ECS::MeshRenderer::Component>(targetEntity);
                    mr.Geometry = handle;
                    mr.Material = materialHandle;

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

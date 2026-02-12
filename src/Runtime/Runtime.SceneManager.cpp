module;
#include <string>
#include <cstdint>
#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>

module Runtime.SceneManager;

import Core;
import Graphics;
import ECS;

namespace
{
    // File-static pointer for the EnTT on_destroy hook.
    // Safe because there is exactly one SceneManager instance per process.
    Graphics::GPUScene* g_GpuSceneForDestroyHook = nullptr;

    void OnMeshRendererDestroyed(entt::registry& registry, entt::entity entity)
    {
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
    }

    void SceneManager::DisconnectGpuHooks()
    {
        m_Scene.GetRegistry().on_destroy<ECS::MeshRenderer::Component>()
            .disconnect<&OnMeshRendererDestroyed>();
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

            // 3. Create Submeshes
            for (size_t i = 0; i < model->Meshes.size(); i++)
            {
                entt::entity targetEntity = root;

                // If complex model, create children. If single mesh, put it on root.
                if (model->Meshes.size() > 1)
                {
                    targetEntity = m_Scene.CreateEntity(model->Meshes[i]->Name);
                    ECS::Components::Hierarchy::Attach(m_Scene.GetRegistry(), targetEntity, root);
                }

                // Add Renderer
                auto& mr = m_Scene.GetRegistry().emplace<ECS::MeshRenderer::Component>(targetEntity);
                mr.Geometry = model->Meshes[i]->Handle;
                mr.Material = materialHandle;

                // Add Collider
                if (model->Meshes[i]->CollisionGeometry)
                {
                    auto& col = m_Scene.GetRegistry().emplace<ECS::MeshCollider::Component>(targetEntity);
                    col.CollisionRef = model->Meshes[i]->CollisionGeometry;
                    col.WorldOBB.Center = col.CollisionRef->LocalAABB.GetCenter();
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

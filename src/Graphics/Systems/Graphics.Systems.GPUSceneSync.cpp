module;

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>

module Graphics.Systems.GPUSceneSync;

import Graphics.GPUScene;

import ECS;
import Graphics.Components;
import Core.Hash;
import Asset.Manager;
import Core.FrameGraph;
import Graphics.MaterialRegistry;
import Graphics.Material;
import Core.SystemFeatureCatalog;

using namespace Core::Hash;

namespace Graphics::Systems::GPUSceneSync
{
    void OnUpdate(entt::registry& registry,
                  GPUScene& gpuScene,
                  const Core::Assets::AssetManager& assetManager,
                  const MaterialRegistry& materialRegistry,
                  uint32_t defaultTextureId)
    {
        (void)defaultTextureId; // Material slot index is used instead of raw texture ID.

        // Fast path: only entities that either changed transform OR need material refresh.
        // Queries Surface::Component (migrated from MeshRenderer::Component in Phase 2).
        auto view = registry.view<
            ECS::Components::Transform::WorldMatrix,
            ECS::Surface::Component>();

        for (auto [entity, world, sc] : view.each())
        {
            if (sc.GpuSlot == ECS::kInvalidGpuSlot)
                continue;

            const bool transformDirty = registry.all_of<ECS::Components::Transform::WorldUpdatedTag>(entity);

            // --- Surface visibility ---
            // When Visible transitions, we deactivate (radius=0) or reactivate
            // the GPU scene slot so the culling shader skips/includes the instance.
            bool visibilityDirty = false;
            if (sc.Visible != sc.CachedVisible)
            {
                visibilityDirty = true;
                sc.CachedVisible = sc.Visible;
            }

            // Resolve material handle once and cache it inside the Surface component.
            if (!sc.CachedMaterialHandle.IsValid())
            {
                if (auto* mat = assetManager.TryGetFast<Graphics::Material>(sc.Material))
                {
                    sc.CachedMaterialHandle = mat->GetHandle();
                }
            }

            uint32_t matRev = 0u;
            const MaterialData* matData = nullptr;
            if (sc.CachedMaterialHandle.IsValid())
            {
                matRev = materialRegistry.GetRevision(sc.CachedMaterialHandle);
                matData = materialRegistry.GetData(sc.CachedMaterialHandle);
            }

            const bool materialDirty = (sc.CachedMaterialHandle != sc.CachedMaterialHandleForInstance) ||
                                       (matRev != sc.CachedMaterialRevisionForInstance);

            if (!transformDirty && !materialDirty && !visibilityDirty)
                continue;

            // If only the transform changed, don't touch TextureID/EntityID/GeometryID.
            // If material changed, refresh TextureID.
            const bool wantsMaterialRefresh = materialDirty;

            GpuInstanceData inst{};
            inst.Model = world.Matrix;

            inst.GeometryID = GPUSceneConstants::kPreserveGeometryId;

            if (wantsMaterialRefresh)
            {
                inst.MaterialSlot = sc.CachedMaterialHandle.IsValid()
                    ? sc.CachedMaterialHandle.Index : 0u;
            }
            else
            {
                // Preserve existing MaterialSlot.
                inst.MaterialSlot = 0xFFFFFFFFu;
            }

            // Keep the picking ID stable.
            // If selection/picking is missing, EntityID stays 0.
            if (auto* pick = registry.try_get<ECS::Components::Selection::PickID>(entt::entity(entity)))
                inst.EntityID = pick->Value;
            // else preserve existing

            // IMPORTANT(bounds): don't clobber spawn-time local bounds.
            // Sentinel contract:
            //  - radius < 0 => keep existing bounds
            //  - radius == 0 => slot inactive (culler skips)
            //
            // When Visible is toggled off, we deactivate by sending radius=0.
            // When toggled back on, we restore the conservative default radius.
            glm::vec4 sphereBounds{0.0f, 0.0f, 0.0f, -1.0f}; // Default: preserve existing bounds.

            if (visibilityDirty)
            {
                if (!sc.Visible)
                {
                    // Deactivate: radius = 0 makes the culler skip this instance.
                    sphereBounds = {0.0f, 0.0f, 0.0f, 0.0f};
                }
                else
                {
                    // Reactivate: restore conservative bounding sphere.
                    sphereBounds = {0.0f, 0.0f, 0.0f, GPUSceneConstants::kDefaultBoundingSphereRadius};
                }
            }

            gpuScene.QueueUpdate(sc.GpuSlot, inst, sphereBounds);

            sc.CachedMaterialHandleForInstance = sc.CachedMaterialHandle;
            sc.CachedMaterialRevisionForInstance = matRev;

            if (transformDirty)
                registry.remove<ECS::Components::Transform::WorldUpdatedTag>(entt::entity(entity));
        }

        // --- Graph entities: transform-only sync (no materials). ---
        auto graphView = registry.view<
            ECS::Components::Transform::WorldMatrix,
            ECS::Graph::Data>();

        for (auto [entity, world, graphData] : graphView.each())
        {
            if (graphData.GpuSlot == ECS::kInvalidGpuSlot)
                continue;

            const bool transformDirty = registry.all_of<ECS::Components::Transform::WorldUpdatedTag>(entity);
            if (!transformDirty)
                continue;

            GpuInstanceData inst{};
            inst.Model = world.Matrix;
            inst.GeometryID = GPUSceneConstants::kPreserveGeometryId;
            inst.MaterialSlot = 0xFFFFFFFFu; // Preserve existing.

            if (auto* pick = registry.try_get<ECS::Components::Selection::PickID>(entt::entity(entity)))
                inst.EntityID = pick->Value;

            glm::vec4 sphereBounds{0.0f, 0.0f, 0.0f, -1.0f}; // Preserve existing bounds.
            gpuScene.QueueUpdate(graphData.GpuSlot, inst, sphereBounds);

            registry.remove<ECS::Components::Transform::WorldUpdatedTag>(entt::entity(entity));
        }

        // --- Cloud-backed point cloud entities: transform-only sync (no materials). ---
        auto cloudView = registry.view<
            ECS::Components::Transform::WorldMatrix,
            ECS::PointCloud::Data>();

        for (auto [entity, world, pcData] : cloudView.each())
        {
            if (pcData.GpuSlot == ECS::kInvalidGpuSlot)
                continue;

            const bool transformDirty = registry.all_of<ECS::Components::Transform::WorldUpdatedTag>(entity);
            if (!transformDirty)
                continue;

            GpuInstanceData inst{};
            inst.Model = world.Matrix;
            inst.GeometryID = GPUSceneConstants::kPreserveGeometryId;
            inst.MaterialSlot = 0xFFFFFFFFu; // Preserve existing.

            if (auto* pick = registry.try_get<ECS::Components::Selection::PickID>(entt::entity(entity)))
                inst.EntityID = pick->Value;

            glm::vec4 sphereBounds{0.0f, 0.0f, 0.0f, -1.0f}; // Preserve existing bounds.
            gpuScene.QueueUpdate(pcData.GpuSlot, inst, sphereBounds);

            registry.remove<ECS::Components::Transform::WorldUpdatedTag>(entt::entity(entity));
        }
    }

    void RegisterSystem(Core::FrameGraph& graph,
                        entt::registry& registry,
                        GPUScene& gpuScene,
                        const Core::Assets::AssetManager& assetManager,
                        const MaterialRegistry& materialRegistry,
                        uint32_t defaultTextureId)
    {
        graph.AddPass(Runtime::SystemFeatureCatalog::PassNames::GPUSceneSync,
            [](Core::FrameGraphBuilder& builder)
            {
                builder.Read<ECS::Components::Transform::WorldMatrix>();
                builder.Read<ECS::Surface::Component>();
                builder.Write<ECS::Components::Transform::WorldUpdatedTag>();
                builder.WaitFor("TransformUpdate"_id);
                builder.Signal("GPUSceneReady"_id);
            },
            [&registry, &gpuScene, &assetManager, &materialRegistry, defaultTextureId]()
            {
                OnUpdate(registry, gpuScene, assetManager, materialRegistry, defaultTextureId);
            });
    }
}

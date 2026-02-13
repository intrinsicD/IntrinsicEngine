module;

#include <entt/entity/registry.hpp>

module Graphics:Systems.GPUSceneSync.Impl;

import :Systems.GPUSceneSync;
import :GPUScene;

import ECS;
import :Components;
import Core.Hash;
import Core.Assets;
import Core.FrameGraph;
import :MaterialSystem;
import :Material;

using namespace Core::Hash;

namespace Graphics::Systems::GPUSceneSync
{
    void OnUpdate(entt::registry& registry,
                  GPUScene& gpuScene,
                  const Core::Assets::AssetManager& assetManager,
                  const MaterialSystem& materialSystem,
                  uint32_t defaultTextureId)
    {
        // Fast path: only entities that either changed transform OR need material refresh.
        auto view = registry.view<
            ECS::Components::Transform::WorldMatrix,
            ECS::MeshRenderer::Component>();

        for (auto [entity, world, mr] : view.each())
        {
            if (mr.GpuSlot == ECS::MeshRenderer::Component::kInvalidSlot)
                continue;

            const bool transformDirty = registry.all_of<ECS::Components::Transform::WorldUpdatedTag>(entity);

            // Resolve material handle once and cache it inside the MeshRenderer component as intended.
            if (!mr.CachedMaterialHandle.IsValid())
            {
                if (auto* mat = assetManager.TryGetFast<Graphics::Material>(mr.Material))
                {
                    mr.CachedMaterialHandle = mat->GetHandle();
                }
            }

            uint32_t matRev = 0u;
            const MaterialData* matData = nullptr;
            if (mr.CachedMaterialHandle.IsValid())
            {
                matRev = materialSystem.GetRevision(mr.CachedMaterialHandle);
                matData = materialSystem.GetData(mr.CachedMaterialHandle);
            }

            const bool materialDirty = (mr.CachedMaterialHandle != mr.CachedMaterialHandleForInstance) ||
                                       (matRev != mr.CachedMaterialRevisionForInstance);

            if (!transformDirty && !materialDirty)
                continue;

            // If only the transform changed, don't touch TextureID/EntityID/GeometryID.
            // If material changed, refresh TextureID.
            const bool wantsMaterialRefresh = materialDirty;

            GpuInstanceData inst{};
            inst.Model = world.Matrix;

            inst.GeometryID = GPUSceneConstants::kPreserveGeometryId;

            if (wantsMaterialRefresh)
            {
                inst.TextureID = (matData) ? matData->AlbedoID : defaultTextureId;
            }
            else
            {
                // Preserve existing TextureID.
                inst.TextureID = 0xFFFFFFFFu;
            }

            // Keep the picking ID stable.
            // If selection/picking is missing, EntityID stays 0.
            if (auto* pick = registry.try_get<ECS::Components::Selection::PickID>(entt::entity(entity)))
                inst.EntityID = pick->Value;
            // else preserve existing

            // IMPORTANT(bounds): don't clobber spawn-time local bounds.
            // Sentinel contract:
            //  - radius < 0 => keep existing bounds
            //  - w/x/y/z = NaN ignored (implementation-defined), so keep it simple
            gpuScene.QueueUpdate(mr.GpuSlot, inst, /*sphereBounds*/ {0.0f, 0.0f, 0.0f, -1.0f});

            mr.CachedMaterialHandleForInstance = mr.CachedMaterialHandle;
            mr.CachedMaterialRevisionForInstance = matRev;

            if (transformDirty)
                registry.remove<ECS::Components::Transform::WorldUpdatedTag>(entt::entity(entity));
        }
    }

    void RegisterSystem(Core::FrameGraph& graph,
                        entt::registry& registry,
                        GPUScene& gpuScene,
                        const Core::Assets::AssetManager& assetManager,
                        const MaterialSystem& materialSystem,
                        uint32_t defaultTextureId)
    {
        graph.AddPass("GPUSceneSync",
            [](Core::FrameGraphBuilder& builder)
            {
                builder.Read<ECS::Components::Transform::WorldMatrix>();
                builder.Read<ECS::MeshRenderer::Component>();
                builder.Write<ECS::Components::Transform::WorldUpdatedTag>();
                builder.WaitFor("TransformUpdate"_id);
                builder.Signal("GPUSceneReady"_id);
            },
            [&registry, &gpuScene, &assetManager, &materialSystem, defaultTextureId]()
            {
                OnUpdate(registry, gpuScene, assetManager, materialSystem, defaultTextureId);
            });
    }
}

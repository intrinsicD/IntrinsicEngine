module;

#include <entt/entity/registry.hpp>

module Graphics:Systems.GPUSceneSync.Impl;

import :Systems.GPUSceneSync;

import ECS;
import :Components;
import Core;
import :MaterialSystem;
import :Material;

namespace Graphics::Systems::GPUSceneSync
{
    void OnUpdate(entt::registry& registry,
                  GPUScene& gpuScene,
                  const Core::Assets::AssetManager& assetManager,
                  const MaterialSystem& materialSystem)
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
                auto matRes = assetManager.Get<Graphics::Material>(mr.Material);
                if (matRes)
                {
                    const auto& mat = *matRes;
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

            GpuInstanceData inst{};
            inst.Model = world.Matrix;

            // IMPORTANT(GeometryID): do NOT overwrite GeometryID here.
            // In the multi-geometry Stage 3 path, GeometryID is a per-frame dense geometry index.
            // Overwriting it with mr.Geometry.Index (handle index) causes compute culling to drop the instance.
            inst.GeometryID = 0xFFFFFFFFu;

            // TextureID: bindless index from material; 0 is default/error.
            inst.TextureID = (matData) ? matData->AlbedoID : 0u;

            // Keep the picking ID stable.
            // If selection/picking is missing, EntityID stays 0.
            if (auto* pick = registry.try_get<ECS::Components::Selection::PickID>(entt::entity(entity)))
                inst.EntityID = pick->Value;

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
}

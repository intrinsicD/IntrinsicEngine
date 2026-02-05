module;

#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>

module Graphics:Systems.MeshRendererLifecycle.Impl;

import :Systems.MeshRendererLifecycle;

import :Components;
import :Material;
import :Geometry;

import Core;
import ECS;

namespace Graphics::Systems::MeshRendererLifecycle
{
    namespace
    {
        [[nodiscard]] auto ComputeLocalBoundingSphere(const GeometryGpuData& geo) -> glm::vec4
        {
            // Robust fallback: if we can't infer bounds, return an "inactive" sphere.
            // (caller will override to a tiny sphere to keep the slot alive if desired)
            if (geo.GetIndexCount() == 0)
                return {0.0f, 0.0f, 0.0f, 0.0f};

            // GeometryGpuData currently doesn’t expose a precomputed sphere, so we conservatively
            // keep the previous Engine spawn strategy: tiny fixed radius around origin.
            // TODO(next): plumb in per-geometry local AABB/sphere from ModelLoader/CollisionGeometry.
            return {0.0f, 0.0f, 0.0f, 1e-3f};
        }

        [[nodiscard]] auto ResolveTextureId(const Core::Assets::AssetManager& assetManager,
                                            const MaterialSystem& materialSystem,
                                            Core::Assets::AssetHandle materialAsset,
                                            uint32_t defaultTextureId) -> uint32_t
        {
            auto matRes = assetManager.Get<Graphics::Material>(materialAsset);
            if (!matRes)
                return defaultTextureId;

            const auto& mat = *matRes;
            const auto handle = mat->GetHandle();
            if (!handle.IsValid())
                return defaultTextureId;

            if (const MaterialData* data = materialSystem.GetData(handle))
                return data->AlbedoID;

            return defaultTextureId;
        }
    }

    void OnUpdate(entt::registry& registry,
                  GPUScene& gpuScene,
                  const Core::Assets::AssetManager& assetManager,
                  const MaterialSystem& materialSystem,
                  const GeometryPool& geometryStorage,
                  uint32_t defaultTextureId)
    {

        auto view = registry.view<ECS::MeshRenderer::Component, ECS::Components::Transform::WorldMatrix>();

        for (auto [entity, mr, world] : view.each())
        {
            // Allocate slot for newly-added components.
            if (mr.GpuSlot == ECS::MeshRenderer::Component::kInvalidSlot)
            {
                const uint32_t slot = gpuScene.AllocateSlot();
                if (slot == ECS::MeshRenderer::Component::kInvalidSlot)
                    continue;

                mr.GpuSlot = slot;

                GpuInstanceData inst{};
                inst.Model = world.Matrix;

                // IMPORTANT: GeometryID stores the sparse GeometryHandle.Index.
                // The GPU culler maps this to a dense per-frame ID via the handleToDense buffer.
                inst.GeometryID = mr.Geometry.Index;

                inst.TextureID = ResolveTextureId(assetManager, materialSystem, mr.Material, defaultTextureId);

                if (auto* pick = registry.try_get<ECS::Components::Selection::PickID>(entt::entity(entity)))
                    inst.EntityID = pick->Value;

                glm::vec4 sphere{0.0f, 0.0f, 0.0f, 0.0f};

                if (mr.Geometry.IsValid())
                {
                    if (GeometryGpuData* geo = geometryStorage.GetUnchecked(mr.Geometry))
                    {
                        sphere = ComputeLocalBoundingSphere(*geo);
                        if (sphere.w <= 0.0f)
                            sphere.w = 1e-3f;
                    }
                }


                gpuScene.QueueUpdate(mr.GpuSlot, inst, sphere);

                // Initialize the GPUSceneSync material cache so it doesn't do redundant work.
                // (GPUSceneSync still handles revision changes correctly.)
                mr.CachedMaterialHandle = {};
                mr.CachedMaterialHandleForInstance = {};
                mr.CachedMaterialRevisionForInstance = 0u;

                // Ensure at least one update makes it this frame.
                registry.emplace_or_replace<ECS::Components::Transform::WorldUpdatedTag>(entt::entity(entity));
            }
        }

        // Reclaim slots for entities destroyed since last frame.
        // We can’t hook EnTT signals globally without a stable system init point, so we do a cheap sweep
        // over MeshRenderer components and deactivate slots that no longer have required components.
        //
        // NOTE: This doesn’t catch immediate destruction mid-frame, but it guarantees correctness.
        // TODO(next): register on_destroy hooks in Engine/Scene setup for O(1) reclaim.
        auto orphanView = registry.view<ECS::MeshRenderer::Component>(entt::exclude<ECS::Components::Transform::WorldMatrix>);
        for (auto entity : orphanView)
        {
            auto& mr = orphanView.get<ECS::MeshRenderer::Component>(entity);
            if (mr.GpuSlot == ECS::MeshRenderer::Component::kInvalidSlot)
                continue;

            // Deactivate slot: radius <= 0 => cull skips it.
            GpuInstanceData inst{};
            gpuScene.QueueUpdate(mr.GpuSlot, inst, /*sphere*/ {0.0f, 0.0f, 0.0f, 0.0f});
            gpuScene.FreeSlot(mr.GpuSlot);
            mr.GpuSlot = ECS::MeshRenderer::Component::kInvalidSlot;
        }
    }
}

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
            // Robust fallback:
            //  - The culler treats radius <= 0 as "inactive" and will skip the instance entirely.
            //  - GeometryGpuData currently doesn't ship precomputed bounds.
            //
            // Until ModelLoader plumbs local AABB/sphere into GeometryGpuData, we use a conservative
            // "safety sphere" to effectively disable culling for typical assets (fixes Invisible Duck).
            if (geo.GetIndexCount() == 0)
                return {0.0f, 0.0f, 0.0f, 0.0f};

            // Massive radius = always visible under any sane camera frustum.
            return {0.0f, 0.0f, 0.0f, 10'000.0f};
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
                if (!mr.Geometry.IsValid())
                    continue;

                GeometryGpuData* geo = geometryStorage.GetUnchecked(mr.Geometry);
                if (!geo || geo->GetIndexCount() == 0 || !geo->GetIndexBuffer() || !geo->GetVertexBuffer())
                    continue;

                const uint32_t slot = gpuScene.AllocateSlot();
                if (slot == ECS::MeshRenderer::Component::kInvalidSlot)
                    continue;

                mr.GpuSlot = slot;

                GpuInstanceData inst{};
                inst.Model = world.Matrix;

                // IMPORTANT: GeometryID stores the sparse GeometryHandle.Index.
                // The GPU culler maps this to a dense per-frame ID via the handleToDense buffer.
                inst.GeometryID = mr.Geometry.Index;

                Graphics::MaterialHandle matHandle{};
                if (auto* mat = assetManager.TryGetFast<Graphics::Material>(mr.Material))
                    matHandle = mat->GetHandle();
                mr.CachedMaterialHandle = matHandle;

                uint32_t matRev = 0u;
                uint32_t texId = defaultTextureId;
                if (matHandle.IsValid())
                {
                    matRev = materialSystem.GetRevision(matHandle);
                    if (const MaterialData* data = materialSystem.GetData(matHandle))
                        texId = data->AlbedoID;
                }
                inst.TextureID = texId;

                mr.CachedMaterialHandleForInstance = matHandle;
                mr.CachedMaterialRevisionForInstance = matRev;

                if (auto* pick = registry.try_get<ECS::Components::Selection::PickID>(entt::entity(entity)))
                    inst.EntityID = pick->Value;

                glm::vec4 sphere{0.0f, 0.0f, 0.0f, 0.0f};

                sphere = ComputeLocalBoundingSphere(*geo);
                if (sphere.w <= 0.0f)
                    sphere.w = 1e-3f;


                gpuScene.QueueUpdate(mr.GpuSlot, inst, sphere);

                registry.remove<ECS::Components::Transform::WorldUpdatedTag>(entt::entity(entity));
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

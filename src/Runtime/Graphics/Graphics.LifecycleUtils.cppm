module;

#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>

export module Graphics.LifecycleUtils;

import Graphics.Geometry;
import Geometry.Handle;
import Graphics.GPUScene;
import Graphics.Components;
import ECS;
import Core.Logging;

// =============================================================================
// LifecycleUtils — shared helpers for GPU lifecycle system implementations.
//
// Used by MeshViewLifecycle, GraphLifecycle, PointCloudLifecycle,
// MeshRendererLifecycle, SceneManager, and EditorUI.
// =============================================================================

export namespace Graphics::LifecycleUtils
{
    // =========================================================================
    // ComputeLocalBoundingSphere — resolve a GPU geometry's bounding sphere.
    // =========================================================================
    // Returns the precomputed local bounding sphere when valid (bounds.w > 0).
    // Falls back to a large conservative radius (kDefaultBoundingSphereRadius) so
    // the entity is never incorrectly culled before a real upload populates bounds.

    inline glm::vec4 ComputeLocalBoundingSphere(const GeometryGpuData& geo)
    {
        const glm::vec4 bounds = geo.GetLocalBoundingSphere();
        if (bounds.w > 0.0f)
            return bounds;

        return {0.0f, 0.0f, 0.0f, GPUSceneConstants::kDefaultBoundingSphereRadius};
    }

    // =========================================================================
    // AllocateGpuSlot — shared GPUScene slot allocation for lifecycle systems.
    // =========================================================================
    // Allocates a GPUScene slot, populates GpuInstanceData with transform,
    // geometry ID, and entity pick ID, computes bounding sphere, queues the
    // update, and clears WorldUpdatedTag to prevent double-update by GPUSceneSync.

    inline uint32_t AllocateGpuSlot(
        entt::registry& registry,
        entt::entity entity,
        GPUScene& gpuScene,
        const GeometryGpuData& geo,
        Geometry::GeometryHandle geometryHandle)
    {
        const uint32_t slot = gpuScene.AllocateSlot();
        if (slot == ECS::kInvalidGpuSlot)
            return ECS::kInvalidGpuSlot;

        GpuInstanceData inst{};

        auto* wm = registry.try_get<ECS::Components::Transform::WorldMatrix>(entity);
        if (wm)
            inst.Model = wm->Matrix;

        inst.GeometryID = geometryHandle.Index;

        if (auto* pick = registry.try_get<ECS::Components::Selection::PickID>(entity))
            inst.EntityID = pick->Value;

        glm::vec4 sphere = ComputeLocalBoundingSphere(geo);
        if (sphere.w <= 0.0f)
            sphere.w = GPUSceneConstants::kMinBoundingSphereRadius;

        gpuScene.QueueUpdate(slot, inst, sphere);

        registry.remove<ECS::Components::Transform::WorldUpdatedTag>(entity);

        return slot;
    }

    // =========================================================================
    // TryAllocateGpuSlot — conditional GPUScene slot allocation.
    // =========================================================================
    // Allocates a slot only when `currentSlot` is invalid and `geometry`
    // is valid with a vertex buffer.

    inline uint32_t TryAllocateGpuSlot(
        entt::registry& registry,
        entt::entity entity,
        GPUScene& gpuScene,
        const GeometryPool& geometryStorage,
        uint32_t currentSlot,
        Geometry::GeometryHandle geometryHandle)
    {
        if (currentSlot != ECS::kInvalidGpuSlot)
            return currentSlot;

        if (!geometryHandle.IsValid())
            return ECS::kInvalidGpuSlot;

        GeometryGpuData* geo = geometryStorage.GetIfValid(geometryHandle);
        if (!geo || !geo->GetVertexBuffer())
            return ECS::kInvalidGpuSlot;

        return AllocateGpuSlot(registry, entity, gpuScene, *geo, geometryHandle);
    }

    // =========================================================================
    // ReleaseGpuSlot — GPUScene slot deactivation + reclamation.
    // =========================================================================

    inline void ReleaseGpuSlot(GPUScene& gpuScene, uint32_t& slot)
    {
        if (slot == ECS::kInvalidGpuSlot)
            return;

        GpuInstanceData inst{};
        gpuScene.QueueUpdate(slot, inst, /*sphere*/ {0.0f, 0.0f, 0.0f, 0.0f});
        gpuScene.FreeSlot(slot);
        slot = ECS::kInvalidGpuSlot;
    }

    template<typename T>
    inline void ReleaseGpuSlot(GPUScene& gpuScene, T& comp)
    {
        ReleaseGpuSlot(gpuScene, comp.GpuSlot);
    }

    // =========================================================================
    // RemovePassComponentIfPresent — conditional per-pass component removal.
    // =========================================================================

    template<typename T>
    inline void RemovePassComponentIfPresent(entt::registry& registry, entt::entity entity)
    {
        if (registry.all_of<T>(entity))
            registry.remove<T>(entity);
    }

    // =========================================================================
    // PopulateOrRemovePassComponent — Phase 3 visibility-aware sync.
    // =========================================================================

    template<typename PassComponent, typename Populator>
    inline void PopulateOrRemovePassComponent(
        entt::registry& registry,
        entt::entity entity,
        bool visible,
        bool hasValidGpuGeometry,
        Populator&& populator)
    {
        if (visible && hasValidGpuGeometry)
        {
            auto& comp = registry.get_or_emplace<PassComponent>(entity);
            populator(comp);
        }
        else if (!visible)
        {
            RemovePassComponentIfPresent<PassComponent>(registry, entity);
        }
    }

    // =========================================================================
    // HandleUploadFailure — Phase 1 shared error handling for geometry upload.
    // =========================================================================

    inline void HandleUploadFailure(
        entt::dispatcher& dispatcher,
        entt::entity entity,
        const char* systemName)
    {
        Core::Log::Error("{}: Failed to create GPU geometry for entity {}",
                         systemName, static_cast<uint32_t>(entity));
        dispatcher.enqueue<ECS::Events::GeometryUploadFailed>({entity});
    }
}

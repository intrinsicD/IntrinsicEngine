#pragma once

// =============================================================================
// LifecycleUtils — shared helpers for GPU lifecycle system implementations.
//
// Intended for inclusion in lifecycle system .cpp files (MeshViewLifecycle,
// GraphGeometrySync, PointCloudGeometrySync, MeshRendererLifecycle).
// Not part of any exported module interface.
//
// Requires the including TU to have imported:
//   import :Geometry;   (for GeometryGpuData, GeometryPool)
//   import :GPUScene;   (for GPUScene, GPUSceneConstants, GpuInstanceData)
//   import :Components; (for ECS component types)
//   import ECS;         (for Transform, Selection components)
//   #include <glm/glm.hpp>
//   #include <entt/entity/registry.hpp>
// =============================================================================

// =============================================================================
// ComputeLocalBoundingSphere — resolve a GPU geometry's bounding sphere.
// =============================================================================
// Returns the precomputed local bounding sphere when valid (bounds.w > 0).
// Falls back to a large conservative radius (kDefaultBoundingSphereRadius) so
// the entity is never incorrectly culled before a real upload populates bounds.
//
// Callers should clamp sphere.w to kMinBoundingSphereRadius after this call to
// guard against degenerate geometry slipping through.

inline glm::vec4 ComputeLocalBoundingSphere(const GeometryGpuData& geo)
{
    const glm::vec4 bounds = geo.GetLocalBoundingSphere();
    if (bounds.w > 0.0f)
        return bounds;

    // Geometry exists but bounds are not yet computed (e.g., reused buffers
    // uploaded without positions). Use a large conservative radius so the
    // entity stays visible until a proper upload populates real bounds.
    return {0.0f, 0.0f, 0.0f, GPUSceneConstants::kDefaultBoundingSphereRadius};
}

// =============================================================================
// AllocateGpuSlot — shared GPUScene slot allocation for lifecycle systems.
// =============================================================================
// Allocates a GPUScene slot, populates GpuInstanceData with transform, geometry
// ID, and entity pick ID, computes bounding sphere, queues the update, and
// clears WorldUpdatedTag to prevent double-update by GPUSceneSync.
//
// Returns the allocated slot index, or kInvalidSlot on failure.
// Used by GraphGeometrySync, PointCloudGeometrySync, and MeshViewLifecycle.

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

    // Clear the WorldUpdatedTag so GPUSceneSync doesn't double-update
    // on the same frame.
    registry.remove<ECS::Components::Transform::WorldUpdatedTag>(entity);

    return slot;
}

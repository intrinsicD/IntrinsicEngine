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

inline glm::vec4 ComputeLocalBoundingSphere(const Graphics::GeometryGpuData& geo)
{
    const glm::vec4 bounds = geo.GetLocalBoundingSphere();
    if (bounds.w > 0.0f)
        return bounds;

    // Geometry exists but bounds are not yet computed (e.g., reused buffers
    // uploaded without positions). Use a large conservative radius so the
    // entity stays visible until a proper upload populates real bounds.
    return {0.0f, 0.0f, 0.0f, Graphics::GPUSceneConstants::kDefaultBoundingSphereRadius};
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
    Graphics::GPUScene& gpuScene,
    const Graphics::GeometryGpuData& geo,
    Geometry::GeometryHandle geometryHandle)
{
    const uint32_t slot = gpuScene.AllocateSlot();
    if (slot == ECS::kInvalidGpuSlot)
        return ECS::kInvalidGpuSlot;

    Graphics::GpuInstanceData inst{};

    auto* wm = registry.try_get<ECS::Components::Transform::WorldMatrix>(entity);
    if (wm)
        inst.Model = wm->Matrix;

    inst.GeometryID = geometryHandle.Index;

    if (auto* pick = registry.try_get<ECS::Components::Selection::PickID>(entity))
        inst.EntityID = pick->Value;

    glm::vec4 sphere = ComputeLocalBoundingSphere(geo);
    if (sphere.w <= 0.0f)
        sphere.w = Graphics::GPUSceneConstants::kMinBoundingSphereRadius;

    gpuScene.QueueUpdate(slot, inst, sphere);

    // Clear the WorldUpdatedTag so GPUSceneSync doesn't double-update
    // on the same frame.
    registry.remove<ECS::Components::Transform::WorldUpdatedTag>(entity);

    return slot;
}

// =============================================================================
// TryAllocateGpuSlot — conditional GPUScene slot allocation for lifecycle
// systems. Allocates a slot only when `currentSlot` is invalid and `geometry`
// is valid with a vertex buffer. Returns the allocated slot (or kInvalidGpuSlot).
// =============================================================================
// Deduplicates the identical Phase 2 pattern found in GraphGeometrySync,
// PointCloudGeometrySync, and MeshViewLifecycle.

inline uint32_t TryAllocateGpuSlot(
    entt::registry& registry,
    entt::entity entity,
    Graphics::GPUScene& gpuScene,
    const Graphics::GeometryPool& geometryStorage,
    uint32_t currentSlot,
    Geometry::GeometryHandle geometryHandle)
{
    if (currentSlot != ECS::kInvalidGpuSlot)
        return currentSlot;

    if (!geometryHandle.IsValid())
        return ECS::kInvalidGpuSlot;

    Graphics::GeometryGpuData* geo = geometryStorage.GetIfValid(geometryHandle);
    if (!geo || !geo->GetVertexBuffer())
        return ECS::kInvalidGpuSlot;

    return AllocateGpuSlot(registry, entity, gpuScene, *geo, geometryHandle);
}

// =============================================================================
// RemovePassComponentIfPresent — conditional per-pass component removal.
// =============================================================================
// Removes a per-pass ECS component from an entity only when present.
// Used by lifecycle systems to hide geometry when visibility is toggled off.

template<typename T>
inline void RemovePassComponentIfPresent(entt::registry& registry, entt::entity entity)
{
    if (registry.all_of<T>(entity))
        registry.remove<T>(entity);
}

// =============================================================================
// PopulateOrRemovePassComponent — Phase 3 visibility-aware pass component sync.
// =============================================================================
// When visible and GPU geometry is valid, calls `get_or_emplace<T>` and invokes
// the populator lambda to fill fields. When not visible, removes the component.
// This deduplicates the identical Phase 3 pattern found in GraphGeometrySync,
// PointCloudGeometrySync, and MeshViewLifecycle.
//
// Usage:
//   PopulateOrRemovePassComponent<ECS::Point::Component>(
//       registry, entity, visible, gpuGeoValid,
//       [&](ECS::Point::Component& pt) {
//           pt.Geometry = data.GpuGeometry;
//           pt.Color = data.DefaultColor;
//       });

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

// =============================================================================
// HandleUploadFailure — Phase 1 shared error handling for geometry upload.
// =============================================================================
// Logs the error, fires GeometryUploadFailed event, and returns. Called by all
// three lifecycle systems when GeometryGpuData::CreateAsync fails.
//
// After calling this, the lifecycle system should clear its cached attributes
// and mark GpuDirty = false (type-specific cleanup is caller responsibility).

inline void HandleUploadFailure(
    entt::dispatcher& dispatcher,
    entt::entity entity,
    const char* systemName)
{
    Core::Log::Error("{}: Failed to create GPU geometry for entity {}",
                     systemName, static_cast<uint32_t>(entity));
    dispatcher.enqueue<ECS::Events::GeometryUploadFailed>({entity});
}

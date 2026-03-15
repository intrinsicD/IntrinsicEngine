#pragma once

// =============================================================================
// LifecycleUtils — shared helpers for GPU lifecycle system implementations.
//
// Intended for inclusion in lifecycle system .cpp files (MeshViewLifecycle,
// GraphGeometrySync, PointCloudGeometrySync, MeshRendererLifecycle).
// Not part of any exported module interface.
//
// Requires the including TU to have imported:
//   import :Geometry;   (for GeometryGpuData)
//   import :GPUScene;   (for GPUSceneConstants)
//   #include <glm/glm.hpp>  (in the global module fragment)
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

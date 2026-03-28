// -------------------------------------------------------------------------
// MeshVertexView — Vertex point view derived from a mesh via
//                  ReuseVertexBuffersFrom.
// -------------------------------------------------------------------------
//
// Attached to entities with Surface::Component to request vertex point
// rendering as a first-class GPU geometry view. MeshViewLifecycleSystem
// creates the view (sharing the mesh's vertex buffer via BDA, topology
// Points) and manages the GPUScene slot lifecycle.

module;
#include <cstdint>

export module Graphics.Components.MeshVertexView;

import Graphics.Components.Core;
import Geometry.Handle;

export namespace ECS::MeshVertexView
{
    struct Component
    {
        // Vertex view geometry handle — sharing mesh vertex buffer via
        // ReuseVertexBuffersFrom. Topology = Points, no index buffer.
        Geometry::GeometryHandle Geometry{};

        // GPUScene slot for frustum culling of this vertex view.
        uint32_t GpuSlot = ECS::kInvalidGpuSlot;

        // Number of vertices in the view (derived from source mesh layout).
        uint32_t VertexCount = 0;

        // true when the vertex view needs (re-)creation.
        bool Dirty = true;

        // ---- Queries ----
        [[nodiscard]] bool HasGpuGeometry() const noexcept { return Geometry.IsValid(); }
    };
}

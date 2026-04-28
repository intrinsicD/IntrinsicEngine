// -------------------------------------------------------------------------
// MeshEdgeView — Edge view derived from a mesh via ReuseVertexBuffersFrom.
// -------------------------------------------------------------------------
//
// Attached to entities with Surface::Component to request wireframe edge
// rendering as a first-class GPU geometry view. MeshViewLifecycleSystem
// creates the edge index buffer (sharing the mesh's vertex buffer via BDA)
// and manages the GPUScene slot lifecycle.
//
// Edge pairs are extracted from collision data and uploaded into a
// contiguous uint32_t index buffer with topology Lines. The index buffer
// is BDA-accessible, allowing LinePass to read edge pairs directly from
// the GeometryGpuData without maintaining internal buffers.

module;
#include <cstdint>

export module Graphics.Components.MeshEdgeView;

import Graphics.Components.Core;
import Geometry.Handle;

export namespace ECS::MeshEdgeView
{
    struct Component
    {
        // Edge view geometry handle — index buffer of edge pairs sharing mesh
        // vertex buffer via ReuseVertexBuffersFrom. Topology = Lines.
        Geometry::GeometryHandle Geometry{};

        // GPUScene slot for frustum culling of this edge view.
        uint32_t GpuSlot = ECS::kInvalidGpuSlot;

        // Number of edges in the uploaded buffer.
        uint32_t EdgeCount = 0;

        // true when the edge buffer needs (re-)creation.
        // Set on first attach, or when collision data changes.
        bool Dirty = true;

        // ---- Queries ----
        [[nodiscard]] bool HasGpuGeometry() const noexcept { return Geometry.IsValid(); }
    };
}

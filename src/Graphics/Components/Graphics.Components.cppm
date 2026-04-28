// =============================================================================
// Graphics.Components — Re-export aggregator.
// =============================================================================
//
// This module re-exports all individual component modules so that existing
// importers (`import Graphics.Components;`) continue to work without change.
//
// Component Naming Contract (see docs/architecture/patterns.md S5):
//
//   *::Data  — Geometry data authority components (Graph::Data, PointCloud::Data,
//              Mesh::Data). Hold shared_ptr to authoritative geometry, cached
//              per-element attributes (colors, radii), GPU state (GpuGeometry,
//              GpuSlot, GpuDirty). Updated by lifecycle systems on dirty check.
//
//   *::Component — Per-pass render components (Surface::Component, Line::Component,
//                  Point::Component) and geometry view components (MeshEdgeView,
//                  MeshVertexView). Presence/absence is the rendering toggle — no
//                  boolean flags. Populated every frame by lifecycle systems.
//
//   DirtyTag::* — Zero-size tag components for per-domain dirty tracking.
//                 Presence signals that data has changed and needs re-sync.
//
// When adding a new geometry type, create a *::Data component.
// When adding a new render pass, create a *::Component.

export module Graphics.Components;

export import Graphics.Components.Core;
export import Graphics.Components.DataAuthority;
export import Graphics.Components.DirtyTag;
export import Graphics.Components.MeshCollider;
export import Graphics.Components.PrimitiveBVH;
export import Graphics.Components.PointKDTree;
export import Graphics.Components.Mesh;
export import Graphics.Components.PointCloud;
export import Graphics.Components.Graph;
export import Graphics.Components.MeshEdgeView;
export import Graphics.Components.MeshVertexView;
export import Graphics.Components.Surface;
export import Graphics.Components.Line;
export import Graphics.Components.Point;

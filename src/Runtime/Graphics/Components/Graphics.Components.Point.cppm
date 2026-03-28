// -------------------------------------------------------------------------
// Point::Component — Owned by PointPass (vertex/node/point cloud rendering).
// -------------------------------------------------------------------------
//
// Presence of this component enables point rendering for the entity.
// Removal disables it. The render mode selects the pipeline variant
// (FlatDisc, Surfel, EWA, Sphere).

module;
#include <glm/glm.hpp>

export module Graphics.Components.Point;

import Geometry.Handle;
import Geometry.PointCloudUtils;

export namespace ECS::Point
{
    struct Component
    {
        // ---- Geometry Handle ----
        // Shared vertex buffer (BDA) — same device-local buffer as the
        // source mesh/graph/cloud. For mesh-derived vertex views, created
        // via ReuseVertexBuffersFrom with topology Points.
        Geometry::GeometryHandle Geometry{};

        // ---- Appearance (defaults; overridden by per-point attributes) ----
        glm::vec4 Color = {1.0f, 0.6f, 0.0f, 1.0f};
        float     Size  = 0.008f;         // World-space radius
        float     SizeMultiplier = 1.0f;  // Per-entity size scaling
        Geometry::PointCloud::RenderMode Mode = Geometry::PointCloud::RenderMode::FlatDisc;

        // ---- Per-Point Attribute Flags ----
        // Set by geometry view lifecycle systems when PropertySet data
        // contains per-point attributes.
        bool HasPerPointColors  = false;
        bool HasPerPointRadii   = false;
        bool HasPerPointNormals = false;  // Required for Surfel mode
    };
}

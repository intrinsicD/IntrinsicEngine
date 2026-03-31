// -------------------------------------------------------------------------
// Line::Component — Owned by LinePass (thick anti-aliased edge rendering).
// -------------------------------------------------------------------------
//
// Presence of this component enables wireframe/edge rendering for the entity.
// Removal disables it. Edge data comes from MeshViewLifecycleSystem (for
// meshes) or GraphLifecycleSystem (for graphs). Per-edge attributes
// (colors, widths) are uploaded to a separate BDA channel by LinePass.

module;
#include <cstdint>
#include <vector>
#include <glm/glm.hpp>

export module Graphics.Components.Line;

import Geometry.Handle;

export namespace ECS::Line
{
    /// Identifies which data authority populated this Line::Component.
    /// Set by lifecycle systems; used by picking and property extraction.
    enum class Domain : uint8_t
    {
        MeshEdge,       ///< Derived from Mesh::Data via MeshEdgeView.
        GraphEdge,      ///< Derived from Graph::Data via GraphLifecycle.
    };

    struct Component
    {
        Component() noexcept
            : Geometry{}
            , EdgeView{}
            , EdgeCount(0)
            , Color(0.85f, 0.85f, 0.85f, 1.0f)
            , Width(1.5f)
            , Overlay(false)
            , HasPerEdgeColors(false)
            , HasPerEdgeWidths(false)
            , ShowPerEdgeColors(true)
            , CachedEdgeColors{}
        {
        }

        // ---- Geometry Handles ----
        // Shared vertex buffer (BDA) — same device-local buffer as the
        // source mesh/graph, referenced via ReuseVertexBuffersFrom.
        Geometry::GeometryHandle Geometry{};

        // Edge index buffer (separate from vertex buffer). Contains
        // flattened uint32_t pairs. Created by MeshViewLifecycleSystem
        // (from collision data) or GraphLifecycleSystem (from graph
        // topology via ReuseVertexBuffersFrom). Must be valid for
        // LinePass to render edges — no internal fallback.
        Geometry::GeometryHandle EdgeView{};

        // Number of edges to render.
        uint32_t EdgeCount = 0;

        // ---- Appearance (defaults; overridden by per-edge attributes) ----
        glm::vec4 Color = {0.85f, 0.85f, 0.85f, 1.0f};
        float     Width = 1.5f;
        bool      Overlay = false;  // true = no depth test (always visible)

        // ---- Per-Edge Attribute Flags ----
        bool HasPerEdgeColors = false;
        bool HasPerEdgeWidths = false;

        // ---- Attribute Visualization Toggle ----
        // When true and HasPerEdgeColors is true, LinePass uses per-edge
        // colors from the aux buffer. When false, uniform Color is used
        // even if per-edge data exists. Toggled via Inspector UI.
        bool ShowPerEdgeColors = true;

        // ---- Per-Edge Color Cache (optional) ----
        // Packed ABGR per edge, sourced from mesh edge PropertySets
        // or set programmatically (e.g., curvature visualization).
        // Must be same length as EdgeCount when non-empty.
        // When empty, LinePass uses uniform Color.
        // For graph entities, edge colors come from Graph::Data::CachedEdgeColors.
        std::vector<uint32_t> CachedEdgeColors;

        // ---- Domain Hint ----
        // Set by the lifecycle system that populated this component.
        Domain SourceDomain = Domain::MeshEdge;
    };
}

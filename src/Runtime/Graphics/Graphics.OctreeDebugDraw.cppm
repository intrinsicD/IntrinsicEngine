module;

#include <cstdint>

#include <glm/glm.hpp>

export module Graphics:OctreeDebugDraw;

import Geometry;
import :DebugDraw;

export namespace Graphics
{
    // -------------------------------------------------------------------------
    // Octree Debug Visualization (CPU → DebugDraw → LineRenderPass)
    // -------------------------------------------------------------------------

    struct OctreeDebugDrawSettings
    {
        bool Enabled = false;

        // If true, boxes are emitted into the overlay list (no depth test).
        // If false, they are depth-tested.
        bool Overlay = true;

        // If true, color varies by node depth. Otherwise BaseColor is used.
        bool ColorByDepth = true;

        // Inclusive maximum depth to draw. Root is depth 0.
        std::uint32_t MaxDepth = 8;

        // If true, draw only leaf nodes.
        bool LeafOnly = false;

        // If true, draw internal nodes (ignored if LeafOnly=true).
        bool DrawInternal = true;

        // If true, skip nodes with NumElements == 0.
        bool OccupiedOnly = true;

        // Alpha is applied to all emitted colors.
        float Alpha = 0.65f;

        // Used when ColorByDepth=false.
        glm::vec3 BaseColor = {1.0f, 0.8f, 0.1f};
    };

    // Emits AABB wire boxes for octree nodes into DebugDraw.
    //
    // Degenerate/empty octrees: emits nothing.
    void DrawOctree(DebugDraw& dd, const Geometry::Octree& octree, const OctreeDebugDrawSettings& settings);

    // Overload that transforms local-space octree AABBs into world space.
    // worldTransform: the entity's model matrix (position, rotation, scale).
    // Note: Non-uniform scale will produce axis-aligned boxes that approximate the transformed OBB.
    void DrawOctree(DebugDraw& dd, const Geometry::Octree& octree, const OctreeDebugDrawSettings& settings,
                    const glm::mat4& worldTransform);
}


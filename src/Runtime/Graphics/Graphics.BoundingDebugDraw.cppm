module;

#include <cstdint>

#include <glm/glm.hpp>

export module Graphics:BoundingDebugDraw;

import Geometry;
import :DebugDraw;

export namespace Graphics
{
    struct BoundingDebugDrawSettings
    {
        bool Enabled = false;
        bool Overlay = false;

        bool DrawAABB = true;
        bool DrawOBB = true;
        bool DrawBoundingSphere = false;

        float Alpha = 0.8f;
        glm::vec3 AABBColor = {0.15f, 0.85f, 1.0f};
        glm::vec3 OBBColor = {1.0f, 0.7f, 0.0f};
        glm::vec3 SphereColor = {0.65f, 0.9f, 0.35f};
    };

    // Draws world-space bounds from a local AABB + world OBB pair.
    // The AABB is transformed into world space by taking the AABB of the OBB corners,
    // keeping the representation conservative under rotation/non-uniform scale.
    void DrawBoundingVolumes(DebugDraw& dd,
                             const Geometry::AABB& localAabb,
                             const Geometry::OBB& worldObb,
                             const BoundingDebugDrawSettings& settings);
}

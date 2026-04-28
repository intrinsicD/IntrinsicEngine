module;

#include <cstdint>

#include <glm/glm.hpp>

export module Graphics.ConvexHullDebugDraw;

import Geometry.HalfedgeMesh;
import Graphics.DebugDraw;

export namespace Graphics
{
    struct ConvexHullDebugDrawSettings
    {
        bool Enabled = false;
        bool Overlay = true;

        float Alpha = 0.75f;
        glm::vec3 Color = {0.95f, 0.35f, 0.15f};
    };

    // Emits a wireframe view of a convex hull mesh into DebugDraw.
    // If the mesh is not triangular/valid, degenerate edges are skipped.
    void DrawConvexHull(DebugDraw& dd,
                        const Geometry::Halfedge::Mesh& hullMesh,
                        const ConvexHullDebugDrawSettings& settings);

    // Overload that applies a world transform to emitted lines.
    void DrawConvexHull(DebugDraw& dd,
                        const Geometry::Halfedge::Mesh& hullMesh,
                        const ConvexHullDebugDrawSettings& settings,
                        const glm::mat4& worldTransform);
}

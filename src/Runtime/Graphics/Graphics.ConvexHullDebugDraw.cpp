module;

#include <algorithm>
#include <cstdint>

#include <glm/glm.hpp>

module Graphics.ConvexHullDebugDraw;

import Graphics.GpuColor;
import Geometry.Properties;

namespace Graphics
{
    namespace
    {
        void EmitEdge(DebugDraw& dd,
                      const glm::vec3& a,
                      const glm::vec3& b,
                      const bool overlay,
                      const std::uint32_t color,
                      const glm::mat4& worldTransform)
        {
            const glm::vec3 wa = GpuColor::TransformPoint(a, worldTransform);
            const glm::vec3 wb = GpuColor::TransformPoint(b, worldTransform);
            if (overlay) dd.OverlayLine(wa, wb, color);
            else dd.Line(wa, wb, color);
        }
    }

    void DrawConvexHull(DebugDraw& dd,
                        const Geometry::Halfedge::Mesh& hullMesh,
                        const ConvexHullDebugDrawSettings& settings)
    {
        DrawConvexHull(dd, hullMesh, settings, glm::mat4(1.0f));
    }

    void DrawConvexHull(DebugDraw& dd,
                        const Geometry::Halfedge::Mesh& hullMesh,
                        const ConvexHullDebugDrawSettings& settings,
                        const glm::mat4& worldTransform)
    {
        if (!settings.Enabled) return;
        if (hullMesh.IsEmpty()) return;

        const std::uint32_t lineColor = GpuColor::PackVec3WithAlpha(settings.Color, settings.Alpha);

        for (std::uint32_t fi = 0; fi < hullMesh.FacesSize(); ++fi)
        {
            const Geometry::FaceHandle face{fi};
            if (!hullMesh.IsValid(face) || hullMesh.IsDeleted(face)) continue;

            const auto h0 = hullMesh.Halfedge(face);
            if (!h0.IsValid()) continue;

            auto h = h0;
            std::uint32_t safetyCounter = 0;
            do
            {
                const auto from = hullMesh.FromVertex(h);
                const auto to = hullMesh.ToVertex(h);
                if (from.IsValid() && to.IsValid() && !hullMesh.IsDeleted(from) && !hullMesh.IsDeleted(to))
                {
                    EmitEdge(dd,
                             hullMesh.Position(from),
                             hullMesh.Position(to),
                             settings.Overlay,
                             lineColor,
                             worldTransform);
                }

                h = hullMesh.NextHalfedge(h);
                ++safetyCounter;
            }
            while (h.IsValid() && h != h0 && safetyCounter <= 16u);
        }
    }
}

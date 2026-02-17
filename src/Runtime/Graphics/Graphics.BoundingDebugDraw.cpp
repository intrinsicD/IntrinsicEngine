module;

#include <algorithm>
#include <cstdint>
#include <glm/glm.hpp>

module Graphics:BoundingDebugDraw.Impl;

import :BoundingDebugDraw;

namespace Graphics
{
    namespace
    {
        [[nodiscard]] std::uint32_t PackWithAlpha(const glm::vec3& rgb, const float alpha)
        {
            return DebugDraw::PackColorF(rgb.r, rgb.g, rgb.b, std::clamp(alpha, 0.0f, 1.0f));
        }

        [[nodiscard]] Geometry::AABB AabbFromObb(const Geometry::OBB& obb)
        {
            const auto corners = obb.GetCorners();
            Geometry::AABB worldAabb;
            worldAabb.Min = corners[0];
            worldAabb.Max = corners[0];

            for (const auto& p : corners)
            {
                worldAabb.Min = glm::min(worldAabb.Min, p);
                worldAabb.Max = glm::max(worldAabb.Max, p);
            }

            return worldAabb;
        }

        void DrawObbWire(DebugDraw& dd, const Geometry::OBB& obb, const bool overlay, const std::uint32_t color)
        {
            const auto corners = obb.GetCorners();
            constexpr int kEdges[12][2] = {
                {0, 1}, {0, 2}, {0, 4},
                {1, 3}, {1, 5},
                {2, 3}, {2, 6},
                {3, 7},
                {4, 5}, {4, 6},
                {5, 7},
                {6, 7},
            };

            for (const auto& edge : kEdges)
            {
                const glm::vec3 a = corners[edge[0]];
                const glm::vec3 b = corners[edge[1]];
                if (overlay) dd.OverlayLine(a, b, color);
                else dd.Line(a, b, color);
            }
        }
    }

    void DrawBoundingVolumes(DebugDraw& dd,
                             const Geometry::AABB& localAabb,
                             const Geometry::OBB& worldObb,
                             const BoundingDebugDrawSettings& settings)
    {
        if (!settings.Enabled) return;
        if (!localAabb.IsValid()) return;
        if (!worldObb.IsValid()) return;

        const std::uint32_t aabbColor = PackWithAlpha(settings.AABBColor, settings.Alpha);
        const std::uint32_t obbColor = PackWithAlpha(settings.OBBColor, settings.Alpha);
        const std::uint32_t sphereColor = PackWithAlpha(settings.SphereColor, settings.Alpha);

        const auto drawBox = [&dd, &settings](const glm::vec3& lo, const glm::vec3& hi, const std::uint32_t color)
        {
            if (settings.Overlay) dd.OverlayBox(lo, hi, color);
            else dd.Box(lo, hi, color);
        };

        if (settings.DrawAABB)
        {
            const Geometry::AABB worldAabb = AabbFromObb(worldObb);
            drawBox(worldAabb.Min, worldAabb.Max, aabbColor);
        }

        if (settings.DrawOBB)
        {
            DrawObbWire(dd, worldObb, settings.Overlay, obbColor);
        }

        if (settings.DrawBoundingSphere)
        {
            const float radius = glm::length(worldObb.Extents);
            if (radius > 0.0f)
            {
                if (settings.Overlay) dd.OverlaySphere(worldObb.Center, radius, sphereColor);
                else dd.Sphere(worldObb.Center, radius, sphereColor);
            }
        }
    }
}

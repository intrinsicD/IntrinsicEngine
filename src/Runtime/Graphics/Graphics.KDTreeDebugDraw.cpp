module;

#include <array>
#include <vector>
#include <cstdint>
#include <limits>

#include <glm/glm.hpp>

module Graphics:KDTreeDebugDraw.Impl;

import :KDTreeDebugDraw;

namespace Graphics
{
    namespace
    {
        [[nodiscard]] std::uint32_t PackWithAlpha(const glm::vec3& rgb, const float alpha)
        {
            return DebugDraw::PackColorF(rgb.r, rgb.g, rgb.b, alpha);
        }

        [[nodiscard]] glm::vec3 TransformPoint(const glm::vec3& p, const glm::mat4& m)
        {
            return glm::vec3(m * glm::vec4(p, 1.0f));
        }

        void EmitBox(DebugDraw& dd,
                     const Geometry::AABB& box,
                     const bool overlay,
                     const std::uint32_t color,
                     const glm::mat4& worldTransform)
        {
            const std::array<glm::vec3, 8> corners = {
                glm::vec3{box.Min.x, box.Min.y, box.Min.z},
                glm::vec3{box.Max.x, box.Min.y, box.Min.z},
                glm::vec3{box.Min.x, box.Max.y, box.Min.z},
                glm::vec3{box.Max.x, box.Max.y, box.Min.z},
                glm::vec3{box.Min.x, box.Min.y, box.Max.z},
                glm::vec3{box.Max.x, box.Min.y, box.Max.z},
                glm::vec3{box.Min.x, box.Max.y, box.Max.z},
                glm::vec3{box.Max.x, box.Max.y, box.Max.z},
            };

            glm::vec3 minP{std::numeric_limits<float>::max()};
            glm::vec3 maxP{std::numeric_limits<float>::lowest()};
            for (const glm::vec3 p : corners)
            {
                const glm::vec3 wp = TransformPoint(p, worldTransform);
                minP = glm::min(minP, wp);
                maxP = glm::max(maxP, wp);
            }

            if (overlay) dd.OverlayBox(minP, maxP, color);
            else dd.Box(minP, maxP, color);
        }

        void EmitLine(DebugDraw& dd,
                      const glm::vec3& a,
                      const glm::vec3& b,
                      const bool overlay,
                      const std::uint32_t color,
                      const glm::mat4& worldTransform)
        {
            const glm::vec3 wa = TransformPoint(a, worldTransform);
            const glm::vec3 wb = TransformPoint(b, worldTransform);
            if (overlay) dd.OverlayLine(wa, wb, color);
            else dd.Line(wa, wb, color);
        }

        void EmitSplitPlane(DebugDraw& dd,
                            const Geometry::KDTree::Node& node,
                            const bool overlay,
                            const std::uint32_t color,
                            const glm::mat4& worldTransform)
        {
            const auto& b = node.Aabb;
            const float s = node.SplitValue;

            // 4 line loop representing the split rectangle clipped to node bounds.
            if (node.SplitAxis == 0)
            {
                const glm::vec3 p00{s, b.Min.y, b.Min.z};
                const glm::vec3 p01{s, b.Min.y, b.Max.z};
                const glm::vec3 p10{s, b.Max.y, b.Min.z};
                const glm::vec3 p11{s, b.Max.y, b.Max.z};
                EmitLine(dd, p00, p01, overlay, color, worldTransform);
                EmitLine(dd, p01, p11, overlay, color, worldTransform);
                EmitLine(dd, p11, p10, overlay, color, worldTransform);
                EmitLine(dd, p10, p00, overlay, color, worldTransform);
                return;
            }

            if (node.SplitAxis == 1)
            {
                const glm::vec3 p00{b.Min.x, s, b.Min.z};
                const glm::vec3 p01{b.Min.x, s, b.Max.z};
                const glm::vec3 p10{b.Max.x, s, b.Min.z};
                const glm::vec3 p11{b.Max.x, s, b.Max.z};
                EmitLine(dd, p00, p01, overlay, color, worldTransform);
                EmitLine(dd, p01, p11, overlay, color, worldTransform);
                EmitLine(dd, p11, p10, overlay, color, worldTransform);
                EmitLine(dd, p10, p00, overlay, color, worldTransform);
                return;
            }

            const glm::vec3 p00{b.Min.x, b.Min.y, s};
            const glm::vec3 p01{b.Min.x, b.Max.y, s};
            const glm::vec3 p10{b.Max.x, b.Min.y, s};
            const glm::vec3 p11{b.Max.x, b.Max.y, s};
            EmitLine(dd, p00, p01, overlay, color, worldTransform);
            EmitLine(dd, p01, p11, overlay, color, worldTransform);
            EmitLine(dd, p11, p10, overlay, color, worldTransform);
            EmitLine(dd, p10, p00, overlay, color, worldTransform);
        }
    }

    void DrawKDTree(DebugDraw& dd, const Geometry::KDTree& tree, const KDTreeDebugDrawSettings& settings)
    {
        DrawKDTree(dd, tree, settings, glm::mat4(1.0f));
    }

    void DrawKDTree(DebugDraw& dd,
                    const Geometry::KDTree& tree,
                    const KDTreeDebugDrawSettings& settings,
                    const glm::mat4& worldTransform)
    {
        if (!settings.Enabled) return;

        const auto& nodes = tree.Nodes();
        if (nodes.empty()) return;

        struct StackItem
        {
            Geometry::KDTree::NodeIndex Node{Geometry::KDTree::kInvalidIndex};
            std::uint32_t Depth{0};
        };

        std::vector<StackItem> stack;
        stack.reserve(64);
        stack.push_back({0u, 0u});

        const auto leafColor = PackWithAlpha(settings.LeafColor, settings.Alpha);
        const auto internalColor = PackWithAlpha(settings.InternalColor, settings.Alpha);
        const auto splitColor = PackWithAlpha(settings.SplitPlaneColor, settings.Alpha);

        while (!stack.empty())
        {
            const StackItem item = stack.back();
            stack.pop_back();
            if (item.Node >= nodes.size()) continue;
            if (item.Depth > settings.MaxDepth) continue;

            const auto& n = nodes[item.Node];
            const bool occupied = n.NumElements > 0;
            const bool shouldDrawNode =
                (!settings.OccupiedOnly || occupied) &&
                ((settings.LeafOnly && n.IsLeaf) || (!settings.LeafOnly && (n.IsLeaf || settings.DrawInternal)));

            if (shouldDrawNode)
            {
                EmitBox(dd, n.Aabb, settings.Overlay, n.IsLeaf ? leafColor : internalColor, worldTransform);
            }

            if (!n.IsLeaf && settings.DrawSplitPlanes)
            {
                EmitSplitPlane(dd, n, settings.Overlay, splitColor, worldTransform);
            }

            if (!n.IsLeaf)
            {
                if (n.Right != Geometry::KDTree::kInvalidIndex)
                    stack.push_back({n.Right, item.Depth + 1});
                if (n.Left != Geometry::KDTree::kInvalidIndex)
                    stack.push_back({n.Left, item.Depth + 1});
            }
        }
    }
}


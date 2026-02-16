module;

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include <glm/glm.hpp>

module Graphics:OctreeDebugDraw.Impl;

import :OctreeDebugDraw;

namespace Graphics
{
    namespace
    {
        // A tiny, branchless(ish) color ramp: depth -> viridis-ish.
        // We keep it deterministic, cheap, and independent of std::format/etc.
        [[nodiscard]] glm::vec3 DepthRamp(float t)
        {
            t = std::clamp(t, 0.0f, 1.0f);

            // 5-point LUT (viridis-like). Linear interpolation.
            constexpr std::array<glm::vec3, 5> k = {
                glm::vec3{0.267f, 0.005f, 0.329f},
                glm::vec3{0.230f, 0.322f, 0.546f},
                glm::vec3{0.128f, 0.566f, 0.550f},
                glm::vec3{0.369f, 0.788f, 0.382f},
                glm::vec3{0.993f, 0.906f, 0.144f},
            };

            const float x = t * 4.0f;
            const int i0 = std::clamp(static_cast<int>(x), 0, 3);
            const int i1 = i0 + 1;
            const float a = x - static_cast<float>(i0);
            return k[i0] * (1.0f - a) + k[i1] * a;
        }

        [[nodiscard]] std::uint32_t PackWithAlpha(const glm::vec3& rgb, float alpha)
        {
            return DebugDraw::PackColorF(rgb.r, rgb.g, rgb.b, alpha);
        }

        // Transform an AABB by a matrix and compute the new axis-aligned bounding box.
        void TransformAABB(const glm::vec3& lo, const glm::vec3& hi, const glm::mat4& m,
                          glm::vec3& outLo, glm::vec3& outHi)
        {
            glm::vec3 corners[8] = {
                {lo.x, lo.y, lo.z}, {hi.x, lo.y, lo.z},
                {lo.x, hi.y, lo.z}, {hi.x, hi.y, lo.z},
                {lo.x, lo.y, hi.z}, {hi.x, lo.y, hi.z},
                {lo.x, hi.y, hi.z}, {hi.x, hi.y, hi.z},
            };

            outLo = glm::vec3(std::numeric_limits<float>::max());
            outHi = glm::vec3(std::numeric_limits<float>::lowest());

            for (int i = 0; i < 8; ++i)
            {
                glm::vec4 p = m * glm::vec4(corners[i], 1.0f);
                glm::vec3 tp = glm::vec3(p);
                outLo = glm::min(outLo, tp);
                outHi = glm::max(outHi, tp);
            }
        }
    }

    void DrawOctree(DebugDraw& dd, const Geometry::Octree& octree, const OctreeDebugDrawSettings& settings)
    {
        DrawOctree(dd, octree, settings, glm::mat4(1.0f));
    }

    void DrawOctree(DebugDraw& dd, const Geometry::Octree& octree, const OctreeDebugDrawSettings& settings,
                    const glm::mat4& worldTransform)
    {
        if (!settings.Enabled) return;
        if (octree.m_Nodes.empty()) return;

        const std::uint32_t maxDepth = settings.MaxDepth;

        // Iterative DFS: (nodeIndex, depth)
        struct StackItem { Geometry::Octree::NodeIndex Node; std::uint32_t Depth; };
        std::array<StackItem, 512> stack{};
        std::size_t sp = 0;
        stack[sp++] = {0u, 0u};

        const Geometry::Octree::Node* nodes = octree.m_Nodes.data();

        while (sp > 0)
        {
            const StackItem it = stack[--sp];
            if (it.Node >= octree.m_Nodes.size()) continue;

            const auto& n = nodes[it.Node];

            if (it.Depth > maxDepth) continue;

            const bool isLeaf = n.IsLeaf;
            const bool drawThis =
                (!settings.OccupiedOnly || n.NumElements > 0) &&
                ((settings.LeafOnly && isLeaf) || (!settings.LeafOnly && (isLeaf || settings.DrawInternal)));

            if (drawThis)
            {
                const float t = (maxDepth > 0) ? (static_cast<float>(it.Depth) / static_cast<float>(maxDepth)) : 0.0f;
                const glm::vec3 rgb = settings.ColorByDepth ? DepthRamp(t) : settings.BaseColor;
                const std::uint32_t color = PackWithAlpha(rgb, settings.Alpha);

                const auto& aabb = n.Aabb;
                glm::vec3 lo, hi;
                TransformAABB(aabb.Min, aabb.Max, worldTransform, lo, hi);

                if (settings.Overlay) dd.OverlayBox(lo, hi, color);
                else dd.Box(lo, hi, color);
            }

            if (!n.IsLeaf && n.BaseChildIndex != Geometry::Octree::kInvalidIndex)
            {
                // Push children in reverse so child 0 is processed first (stable order).
                // Children are stored contiguously; ChildMask indicates which exist.
                std::uint32_t childOffset = 0;
                for (int child = 0; child < 8; ++child)
                {
                    if (!n.ChildExists(child)) continue;
                    const auto childIndex = n.BaseChildIndex + childOffset;
                    childOffset++;

                    if (sp < stack.size())
                        stack[sp++] = {childIndex, it.Depth + 1};
                }
            }
        }
    }
}

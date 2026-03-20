module;

#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include <glm/glm.hpp>

module Graphics.OctreeDebugDraw;

import Graphics.GpuColor;
import Geometry.AABB;


namespace Graphics
{
    namespace
    {
        // Delegates to Geometry::TransformAABB, wrapping the out-param interface.
        void TransformAABB(const glm::vec3& lo, const glm::vec3& hi, const glm::mat4& m,
                          glm::vec3& outLo, glm::vec3& outHi)
        {
            const Geometry::AABB src{lo, hi};
            const Geometry::AABB result = Geometry::TransformAABB(src, m);
            outLo = result.Min;
            outHi = result.Max;
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
        if (dd.GetRemainingLineCapacity() == 0u) return;

        const std::uint32_t maxDepth = settings.MaxDepth;

        // Iterative DFS: (nodeIndex, depth)
        struct StackItem { Geometry::Octree::NodeIndex Node; std::uint32_t Depth; };
        std::array<StackItem, 512> stack{};
        std::size_t sp = 0;
        stack[sp++] = {0u, 0u};

        const Geometry::Octree::Node* nodes = octree.m_Nodes.data();

        while (sp > 0)
        {
            if (dd.GetRemainingLineCapacity() < 12u)
                break;

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
                if (dd.GetRemainingLineCapacity() < 12u)
                    break;

                const float t = (maxDepth > 0) ? (static_cast<float>(it.Depth) / static_cast<float>(maxDepth)) : 0.0f;
                const glm::vec3 rgb = settings.ColorByDepth ? GpuColor::DepthRamp(t) : settings.BaseColor;
                const std::uint32_t color = GpuColor::PackVec3WithAlpha(rgb, settings.Alpha);

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

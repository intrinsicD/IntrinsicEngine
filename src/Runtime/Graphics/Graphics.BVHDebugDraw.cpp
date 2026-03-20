module;

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

#include <glm/glm.hpp>

module Graphics.BVHDebugDraw;

import Graphics.GpuColor;
import Geometry;

namespace Graphics
{
    namespace
    {
        struct BuildTriangle
        {
            Geometry::AABB Bounds{};
            glm::vec3 Centroid{0.0f};
        };

        struct BVHNode
        {
            Geometry::AABB Bounds{};
            std::uint32_t First = 0;
            std::uint32_t Count = 0;
            std::int32_t Left = -1;
            std::int32_t Right = -1;

            [[nodiscard]] bool IsLeaf() const { return Left < 0 || Right < 0; }
        };

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
                const glm::vec3 wp = GpuColor::TransformPoint(p, worldTransform);
                minP = glm::min(minP, wp);
                maxP = glm::max(maxP, wp);
            }

            if (overlay) dd.OverlayBox(minP, maxP, color);
            else dd.Box(minP, maxP, color);
        }

        [[nodiscard]] Geometry::AABB ComputeRangeBounds(const std::vector<BuildTriangle>& triangles,
                                                        const std::vector<std::uint32_t>& order,
                                                        const std::uint32_t first,
                                                        const std::uint32_t count)
        {
            Geometry::AABB bounds{};
            for (std::uint32_t i = 0; i < count; ++i)
            {
                bounds = Geometry::Union(bounds, triangles[order[first + i]].Bounds);
            }
            return bounds;
        }

        void BuildNodesRecursive(std::vector<BVHNode>& nodes,
                                 const std::vector<BuildTriangle>& triangles,
                                 std::vector<std::uint32_t>& order,
                                 const std::uint32_t nodeIndex,
                                 const std::uint32_t maxLeafTriangleCount,
                                 const std::uint32_t depth,
                                 const std::uint32_t maxDepth)
        {
            BVHNode& node = nodes[nodeIndex];
            if (node.Count <= maxLeafTriangleCount || depth >= maxDepth)
            {
                return;
            }

            const glm::vec3 extent = node.Bounds.GetSize();
            std::uint32_t axis = 0;
            if (extent.y > extent.x && extent.y >= extent.z) axis = 1;
            else if (extent.z > extent.x && extent.z >= extent.y) axis = 2;

            const auto beginIt = order.begin() + static_cast<std::ptrdiff_t>(node.First);
            const auto endIt = beginIt + static_cast<std::ptrdiff_t>(node.Count);
            std::sort(beginIt, endIt, [&](const std::uint32_t a, const std::uint32_t b)
            {
                return triangles[a].Centroid[axis] < triangles[b].Centroid[axis];
            });

            const std::uint32_t leftCount = node.Count / 2;
            const std::uint32_t rightCount = node.Count - leftCount;
            if (leftCount == 0 || rightCount == 0)
            {
                return;
            }

            const std::uint32_t leftFirst = node.First;
            const std::uint32_t rightFirst = node.First + leftCount;

            const std::int32_t leftIndex = static_cast<std::int32_t>(nodes.size());
            nodes.push_back(BVHNode{
                .Bounds = ComputeRangeBounds(triangles, order, leftFirst, leftCount),
                .First = leftFirst,
                .Count = leftCount,
                .Left = -1,
                .Right = -1
            });

            const std::int32_t rightIndex = static_cast<std::int32_t>(nodes.size());
            nodes.push_back(BVHNode{
                .Bounds = ComputeRangeBounds(triangles, order, rightFirst, rightCount),
                .First = rightFirst,
                .Count = rightCount,
                .Left = -1,
                .Right = -1
            });

            node.Left = leftIndex;
            node.Right = rightIndex;

            BuildNodesRecursive(nodes, triangles, order, static_cast<std::uint32_t>(leftIndex), maxLeafTriangleCount,
                                depth + 1, maxDepth);
            BuildNodesRecursive(nodes, triangles, order, static_cast<std::uint32_t>(rightIndex), maxLeafTriangleCount,
                                depth + 1, maxDepth);
        }
    }

    void DrawBVH(DebugDraw& dd,
                 std::span<const glm::vec3> positions,
                 std::span<const uint32_t> indices,
                 const BVHDebugDrawSettings& settings,
                 const glm::mat4& worldTransform)
    {
        if (!settings.Enabled) return;
        if (positions.size() < 3) return;

        const bool indexed = (indices.size() >= 3);
        const std::size_t triangleCount = indexed ? (indices.size() / 3u) : (positions.size() / 3u);
        if (triangleCount == 0) return;

        std::vector<BuildTriangle> triangles;
        triangles.reserve(triangleCount);

        for (std::size_t tri = 0; tri < triangleCount; ++tri)
        {
            const auto idx0 = indexed ? indices[tri * 3u + 0u] : static_cast<std::uint32_t>(tri * 3u + 0u);
            const auto idx1 = indexed ? indices[tri * 3u + 1u] : static_cast<std::uint32_t>(tri * 3u + 1u);
            const auto idx2 = indexed ? indices[tri * 3u + 2u] : static_cast<std::uint32_t>(tri * 3u + 2u);

            if (idx0 >= positions.size() || idx1 >= positions.size() || idx2 >= positions.size()) continue;

            const glm::vec3 a = positions[idx0];
            const glm::vec3 b = positions[idx1];
            const glm::vec3 c = positions[idx2];

            Geometry::AABB bounds{};
            bounds = Geometry::Union(bounds, a);
            bounds = Geometry::Union(bounds, b);
            bounds = Geometry::Union(bounds, c);

            triangles.push_back(BuildTriangle{
                .Bounds = bounds,
                .Centroid = (a + b + c) / 3.0f
            });
        }

        if (triangles.empty()) return;

        std::vector<std::uint32_t> order(triangles.size());
        for (std::uint32_t i = 0; i < order.size(); ++i) order[i] = i;

        std::vector<BVHNode> nodes;
        nodes.reserve(triangles.size() * 2u);
        nodes.push_back(BVHNode{
            .Bounds = ComputeRangeBounds(triangles, order, 0u, static_cast<std::uint32_t>(order.size())),
            .First = 0u,
            .Count = static_cast<std::uint32_t>(order.size()),
            .Left = -1,
            .Right = -1
        });

        BuildNodesRecursive(nodes, triangles, order, 0u, glm::max(1u, settings.LeafTriangleCount), 0u,
                            glm::max(settings.MaxDepth, 1u));

        const std::uint32_t leafColor = GpuColor::PackVec3WithAlpha(settings.LeafColor, settings.Alpha);
        const std::uint32_t internalColor = GpuColor::PackVec3WithAlpha(settings.InternalColor, settings.Alpha);

        struct NodeWithDepth { std::uint32_t Index; std::uint32_t Depth; };
        std::vector<NodeWithDepth> stack{};
        stack.reserve(nodes.size());
        stack.push_back({0u, 0u});

        while (!stack.empty())
        {
            const NodeWithDepth current = stack.back();
            stack.pop_back();

            if (current.Index >= nodes.size() || current.Depth > settings.MaxDepth) continue;

            const BVHNode& node = nodes[current.Index];
            const bool isLeaf = node.IsLeaf();
            const bool shouldDraw = (isLeaf && (settings.LeafOnly || !settings.DrawInternal)) ||
                                    (!isLeaf && !settings.LeafOnly && settings.DrawInternal) ||
                                    (!settings.LeafOnly && isLeaf);

            if (shouldDraw)
            {
                EmitBox(dd, node.Bounds, settings.Overlay, isLeaf ? leafColor : internalColor, worldTransform);
            }

            if (!isLeaf)
            {
                stack.push_back({static_cast<std::uint32_t>(node.Left), current.Depth + 1u});
                stack.push_back({static_cast<std::uint32_t>(node.Right), current.Depth + 1u});
            }
        }
    }
}

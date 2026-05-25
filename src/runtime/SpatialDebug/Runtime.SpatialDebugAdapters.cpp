module;

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

module Extrinsic.Runtime.SpatialDebugAdapters;

import Geometry.AABB;
import Geometry.BVH;
import Extrinsic.Graphics.SpatialDebugVisualizers;

namespace Extrinsic::Runtime
{
    namespace
    {
        [[nodiscard]] Extrinsic::Graphics::SpatialDebugAabb ToSpatialDebugAabb(
            const Geometry::AABB& aabb) noexcept
        {
            return Extrinsic::Graphics::SpatialDebugAabb{
                .Min = aabb.Min,
                .Max = aabb.Max,
            };
        }

        [[nodiscard]] Extrinsic::Graphics::SpatialDebugSplitAxis ToSpatialDebugSplitAxis(
            std::uint8_t axis) noexcept
        {
            switch (axis)
            {
            case 0: return Extrinsic::Graphics::SpatialDebugSplitAxis::X;
            case 1: return Extrinsic::Graphics::SpatialDebugSplitAxis::Y;
            default: return Extrinsic::Graphics::SpatialDebugSplitAxis::Z;
            }
        }
    }

    void SpatialDebugSnapshotBatch::Clear() noexcept
    {
        Bounds.clear();
        HierarchyNodes.clear();
        SplitPlanes.clear();
        ConvexHullVertices.clear();
        ConvexHullEdges.clear();
        PointMarkers.clear();
    }

    BvhAdapter::BvhAdapter(const Geometry::BVH& bvh) noexcept
        : m_Bvh(&bvh)
    {
    }

    void BvhAdapter::Append(SpatialDebugSnapshotBatch&        out,
                            const SpatialDebugAdapterOptions& options,
                            SpatialDebugAdapterStats&         stats) const
    {
        if (m_Bvh == nullptr)
            return;

        const auto& nodes = m_Bvh->Nodes();
        if (nodes.empty())
            return;

        // Iterative DFS from the root carrying per-node depth so the
        // adapter can apply MaxDepth truncation. A truncated subtree
        // root increments DepthCapTruncationCount exactly once.
        struct StackEntry
        {
            Geometry::BVH::NodeIndex Index;
            std::uint32_t            Depth;
        };

        std::vector<StackEntry> stack;
        stack.reserve(nodes.size());
        stack.push_back({0u, 0u});

        while (!stack.empty())
        {
            const StackEntry entry = stack.back();
            stack.pop_back();

            const auto& node = nodes[entry.Index];

            const bool depthCapped = entry.Depth >= options.MaxDepth;

            if (options.LeafOnly && !node.IsLeaf)
            {
                // Skip inner nodes when leaf-only filter is on; still
                // recurse into children so the leaves further down
                // are visited.
                if (!depthCapped)
                {
                    if (node.Right != Geometry::BVH::kInvalidIndex)
                        stack.push_back({node.Right, entry.Depth + 1u});
                    if (node.Left != Geometry::BVH::kInvalidIndex)
                        stack.push_back({node.Left, entry.Depth + 1u});
                }
                else
                {
                    ++stats.DepthCapTruncationCount;
                }
                continue;
            }

            const bool emptyLeafSkipped = options.OccupancyOnly && node.IsLeaf && node.NumElements == 0u;
            if (emptyLeafSkipped)
            {
                ++stats.EmptyNodeSkippedCount;
                continue;
            }

            const auto bounds = ToSpatialDebugAabb(node.Aabb);
            out.Bounds.push_back(bounds);
            out.HierarchyNodes.push_back(Extrinsic::Graphics::SpatialDebugHierarchyNode{
                .Bounds = bounds,
                .Depth  = entry.Depth,
                .IsLeaf = node.IsLeaf,
            });

            if (node.IsLeaf)
            {
                ++stats.LeafNodeCount;
            }
            else
            {
                ++stats.InnerNodeCount;

                out.SplitPlanes.push_back(Extrinsic::Graphics::SpatialDebugSplitPlane{
                    .Bounds   = bounds,
                    .Axis     = ToSpatialDebugSplitAxis(node.SplitAxis),
                    .Position = node.SplitValue,
                });
                ++stats.SplitPlaneCount;
            }

            if (depthCapped)
            {
                if (!node.IsLeaf)
                    ++stats.DepthCapTruncationCount;
                continue;
            }

            if (node.Right != Geometry::BVH::kInvalidIndex)
                stack.push_back({node.Right, entry.Depth + 1u});
            if (node.Left != Geometry::BVH::kInvalidIndex)
                stack.push_back({node.Left, entry.Depth + 1u});
        }
    }
}

module;

#include <array>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

module Extrinsic.Runtime.SpatialDebugAdapters;

import Geometry.AABB;
import Geometry.BVH;
import Geometry.KDTree;
import Geometry.Octree;
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

    KdTreeAdapter::KdTreeAdapter(const Geometry::KDTree& kdTree) noexcept
        : m_KdTree(&kdTree)
    {
    }

    void KdTreeAdapter::Append(SpatialDebugSnapshotBatch&        out,
                               const SpatialDebugAdapterOptions& options,
                               SpatialDebugAdapterStats&         stats) const
    {
        if (m_KdTree == nullptr)
            return;

        const auto& nodes = m_KdTree->Nodes();
        if (nodes.empty())
            return;

        struct StackEntry
        {
            Geometry::KDTree::NodeIndex Index;
            std::uint32_t               Depth;
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
                if (!depthCapped)
                {
                    if (node.Right != Geometry::KDTree::kInvalidIndex)
                        stack.push_back({node.Right, entry.Depth + 1u});
                    if (node.Left != Geometry::KDTree::kInvalidIndex)
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

            if (node.Right != Geometry::KDTree::kInvalidIndex)
                stack.push_back({node.Right, entry.Depth + 1u});
            if (node.Left != Geometry::KDTree::kInvalidIndex)
                stack.push_back({node.Left, entry.Depth + 1u});
        }
    }

    OctreeAdapter::OctreeAdapter(const Geometry::Octree& octree) noexcept
        : m_Octree(&octree)
    {
    }

    void OctreeAdapter::Append(SpatialDebugSnapshotBatch&        out,
                               const SpatialDebugAdapterOptions& options,
                               SpatialDebugAdapterStats&         stats) const
    {
        if (m_Octree == nullptr)
            return;

        const auto& nodes = m_Octree->m_Nodes;
        if (nodes.empty())
            return;

        struct StackEntry
        {
            Geometry::Octree::NodeIndex Index;
            std::uint32_t               Depth;
        };

        // Octrees split simultaneously on three axes through a single
        // implicit split point. The Octree::Node struct does not
        // store that split point, so the adapter visualizes the
        // partition using the parent node's AABB center. This is
        // exact for the SplitPoint::Center policy and an approximation
        // for Mean / Median, which the OctreeAdapter explicitly does
        // not attempt to reconstruct.
        //
        // Child node indices in Geometry::Octree::m_Nodes are stored as
        // BaseChildIndex + presentOffset, where presentOffset is the
        // count of present octants encountered in [0, octant). We push
        // child indices onto the work stack in reverse so that octant 0
        // is popped first, giving deterministic ascending-octant DFS
        // order.
        auto pushChildren = [&](const Geometry::Octree::Node& node, std::uint32_t depth, auto& workStack) {
            if (node.IsLeaf || node.BaseChildIndex == Geometry::Octree::kInvalidIndex)
                return;
            std::array<Geometry::Octree::NodeIndex, 8u> presentChildren{};
            std::uint32_t                                presentCount = 0u;
            for (std::uint32_t octant = 0u; octant < 8u; ++octant)
            {
                if (!node.ChildExists(octant))
                    continue;
                presentChildren[presentCount] = node.BaseChildIndex + presentCount;
                ++presentCount;
            }
            for (std::uint32_t i = presentCount; i-- > 0u; )
                workStack.push_back({presentChildren[i], depth + 1u});
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
                if (!depthCapped)
                {
                    pushChildren(node, entry.Depth, stack);
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

                const glm::vec3 center = 0.5f * (node.Aabb.Min + node.Aabb.Max);
                for (std::uint8_t axis = 0u; axis < 3u; ++axis)
                {
                    const float position =
                        (axis == 0u) ? center.x : (axis == 1u ? center.y : center.z);
                    out.SplitPlanes.push_back(Extrinsic::Graphics::SpatialDebugSplitPlane{
                        .Bounds   = bounds,
                        .Axis     = ToSpatialDebugSplitAxis(axis),
                        .Position = position,
                    });
                    ++stats.SplitPlaneCount;
                }
            }

            if (depthCapped)
            {
                if (!node.IsLeaf)
                    ++stats.DepthCapTruncationCount;
                continue;
            }

            pushChildren(node, entry.Depth, stack);
        }
    }
}

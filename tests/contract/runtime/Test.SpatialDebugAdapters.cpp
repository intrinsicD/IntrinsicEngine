#include <array>
#include <cstdint>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>
#include <glm/glm.hpp>

import Geometry.AABB;
import Geometry.BVH;
import Extrinsic.Graphics.SpatialDebugVisualizers;
import Extrinsic.Runtime.SpatialDebugAdapters;

using Extrinsic::Graphics::SpatialDebugAabb;
using Extrinsic::Graphics::SpatialDebugHierarchyNode;
using Extrinsic::Graphics::SpatialDebugSplitAxis;
using Extrinsic::Graphics::SpatialDebugSplitPlane;
using Extrinsic::Graphics::SpatialDebugWireEdge;
using Extrinsic::Runtime::BvhAdapter;
using Extrinsic::Runtime::SpatialDebugAdapterOptions;
using Extrinsic::Runtime::SpatialDebugAdapterStats;
using Extrinsic::Runtime::SpatialDebugSnapshotBatch;

// BvhAdapter is non-owning; the public surface must reject temporaries so
// callers cannot leave m_Bvh dangling. lvalue construction must stay
// constructible.
static_assert(!std::is_constructible_v<BvhAdapter, Geometry::BVH&&>,
              "BvhAdapter must not bind to an rvalue Geometry::BVH (non-owning pointer would dangle)");
static_assert(!std::is_constructible_v<BvhAdapter, const Geometry::BVH&&>,
              "BvhAdapter must not bind to a const rvalue Geometry::BVH");
static_assert(std::is_constructible_v<BvhAdapter, Geometry::BVH&>,
              "BvhAdapter must remain constructible from a mutable lvalue Geometry::BVH");
static_assert(std::is_constructible_v<BvhAdapter, const Geometry::BVH&>,
              "BvhAdapter must remain constructible from a const lvalue Geometry::BVH");

namespace
{
    // Four well-separated AABBs guarantee a non-trivial BVH split tree.
    [[nodiscard]] Geometry::BVH MakeFourBoxFixture()
    {
        std::vector<Geometry::AABB> boxes{
            Geometry::AABB{{-4.0f, -1.0f, -1.0f}, {-3.0f, 1.0f, 1.0f}},
            Geometry::AABB{{-1.0f, -1.0f, -1.0f}, { 0.0f, 1.0f, 1.0f}},
            Geometry::AABB{{ 1.0f, -1.0f, -1.0f}, { 2.0f, 1.0f, 1.0f}},
            Geometry::AABB{{ 3.0f, -1.0f, -1.0f}, { 4.0f, 1.0f, 1.0f}},
        };

        Geometry::BVH bvh;
        Geometry::BVHBuildParams params{};
        params.LeafSize = 1;
        EXPECT_TRUE(bvh.Build(boxes, params).has_value());
        return bvh;
    }

    [[nodiscard]] std::uint32_t CountLeavesAndInners(const Geometry::BVH& bvh,
                                                    std::uint32_t&       leafOut,
                                                    std::uint32_t&       innerOut)
    {
        leafOut  = 0u;
        innerOut = 0u;
        for (const auto& node : bvh.Nodes())
        {
            if (node.IsLeaf) ++leafOut;
            else             ++innerOut;
        }
        return leafOut + innerOut;
    }
}

TEST(SpatialDebugAdapters, BvhAdapterAppendsDeterministicNodesAndPlanes)
{
    const Geometry::BVH bvh = MakeFourBoxFixture();
    ASSERT_FALSE(bvh.Nodes().empty()) << "fixture must produce a non-empty BVH";

    std::uint32_t expectedLeaves = 0u;
    std::uint32_t expectedInners = 0u;
    const std::uint32_t expectedTotal = CountLeavesAndInners(bvh, expectedLeaves, expectedInners);

    SpatialDebugSnapshotBatch batch{};
    SpatialDebugAdapterStats  stats{};
    BvhAdapter                adapter{bvh};

    adapter.Append(batch, SpatialDebugAdapterOptions{}, stats);

    EXPECT_EQ(batch.HierarchyNodes.size(), static_cast<std::size_t>(expectedTotal));
    EXPECT_EQ(batch.Bounds.size(),         static_cast<std::size_t>(expectedTotal));
    EXPECT_EQ(batch.SplitPlanes.size(),    static_cast<std::size_t>(expectedInners));

    EXPECT_EQ(stats.LeafNodeCount,           expectedLeaves);
    EXPECT_EQ(stats.InnerNodeCount,          expectedInners);
    EXPECT_EQ(stats.SplitPlaneCount,         expectedInners);
    EXPECT_EQ(stats.DepthCapTruncationCount, 0u);

    // Root is always the first emitted hierarchy node (DFS from index 0).
    const auto& rootNode = bvh.Nodes()[0];
    ASSERT_FALSE(batch.HierarchyNodes.empty());
    EXPECT_EQ(batch.HierarchyNodes.front().Depth, 0u);
    EXPECT_EQ(batch.HierarchyNodes.front().IsLeaf, rootNode.IsLeaf);
    EXPECT_FLOAT_EQ(batch.HierarchyNodes.front().Bounds.Min.x, rootNode.Aabb.Min.x);
    EXPECT_FLOAT_EQ(batch.HierarchyNodes.front().Bounds.Max.x, rootNode.Aabb.Max.x);

    // ConvexHull spans stay untouched by the BVH adapter.
    EXPECT_TRUE(batch.ConvexHullVertices.empty());
    EXPECT_TRUE(batch.ConvexHullEdges.empty());
    EXPECT_TRUE(batch.PointMarkers.empty());
}

TEST(SpatialDebugAdapters, BvhAdapterLeafOnlyFilterDropsInnerNodes)
{
    const Geometry::BVH bvh = MakeFourBoxFixture();
    std::uint32_t expectedLeaves = 0u;
    std::uint32_t expectedInners = 0u;
    (void)CountLeavesAndInners(bvh, expectedLeaves, expectedInners);
    ASSERT_GT(expectedInners, 0u) << "fixture should split into at least one inner node";

    SpatialDebugSnapshotBatch batch{};
    SpatialDebugAdapterStats  stats{};
    BvhAdapter                adapter{bvh};

    SpatialDebugAdapterOptions options{};
    options.LeafOnly = true;
    adapter.Append(batch, options, stats);

    EXPECT_EQ(batch.HierarchyNodes.size(), static_cast<std::size_t>(expectedLeaves));
    EXPECT_EQ(batch.Bounds.size(),         static_cast<std::size_t>(expectedLeaves));
    EXPECT_TRUE(batch.SplitPlanes.empty());

    EXPECT_EQ(stats.LeafNodeCount,   expectedLeaves);
    EXPECT_EQ(stats.InnerNodeCount,  0u);
    EXPECT_EQ(stats.SplitPlaneCount, 0u);
    for (const auto& node : batch.HierarchyNodes)
        EXPECT_TRUE(node.IsLeaf);
}

TEST(SpatialDebugAdapters, BvhAdapterDepthCapTruncatesAndCounts)
{
    const Geometry::BVH bvh = MakeFourBoxFixture();
    ASSERT_FALSE(bvh.Nodes()[0].IsLeaf)
        << "fixture root must be an inner node for the truncation test";

    SpatialDebugSnapshotBatch batch{};
    SpatialDebugAdapterStats  stats{};
    BvhAdapter                adapter{bvh};

    SpatialDebugAdapterOptions options{};
    options.MaxDepth = 0u;
    adapter.Append(batch, options, stats);

    EXPECT_EQ(batch.HierarchyNodes.size(), 1u);
    EXPECT_EQ(batch.SplitPlanes.size(),    1u);
    EXPECT_EQ(stats.DepthCapTruncationCount, 1u);
    EXPECT_EQ(batch.HierarchyNodes.front().Depth, 0u);
    EXPECT_FALSE(batch.HierarchyNodes.front().IsLeaf);
}

TEST(SpatialDebugAdapters, SnapshotBatchClearResetsAllSpans)
{
    SpatialDebugSnapshotBatch batch{};
    batch.Bounds.push_back(SpatialDebugAabb{});
    batch.HierarchyNodes.push_back(SpatialDebugHierarchyNode{});
    batch.SplitPlanes.push_back(SpatialDebugSplitPlane{});
    batch.ConvexHullVertices.push_back(glm::vec3{1.0f});
    batch.ConvexHullEdges.push_back(SpatialDebugWireEdge{});
    batch.PointMarkers.push_back(glm::vec3{1.0f});

    batch.Clear();

    EXPECT_TRUE(batch.Bounds.empty());
    EXPECT_TRUE(batch.HierarchyNodes.empty());
    EXPECT_TRUE(batch.SplitPlanes.empty());
    EXPECT_TRUE(batch.ConvexHullVertices.empty());
    EXPECT_TRUE(batch.ConvexHullEdges.empty());
    EXPECT_TRUE(batch.PointMarkers.empty());
}

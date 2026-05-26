#include <array>
#include <cstdint>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>
#include <glm/glm.hpp>

import Geometry.AABB;
import Geometry.BVH;
import Geometry.ConvexHull;
import Geometry.KDTree;
import Geometry.Octree;
import Geometry.Plane;
import Extrinsic.Graphics.SpatialDebugVisualizers;
import Extrinsic.Runtime.SpatialDebugAdapters;

using Extrinsic::Graphics::SpatialDebugAabb;
using Extrinsic::Graphics::SpatialDebugHierarchyNode;
using Extrinsic::Graphics::SpatialDebugSplitAxis;
using Extrinsic::Graphics::SpatialDebugSplitPlane;
using Extrinsic::Graphics::SpatialDebugWireEdge;
using Extrinsic::Runtime::BvhAdapter;
using Extrinsic::Runtime::ConvexHullAdapter;
using Extrinsic::Runtime::ISpatialDebugAdapter;
using Extrinsic::Runtime::KdTreeAdapter;
using Extrinsic::Runtime::OctreeAdapter;
using Extrinsic::Runtime::SpatialDebugAdapterOptions;
using Extrinsic::Runtime::SpatialDebugAdapterRegistry;
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

// KdTreeAdapter and OctreeAdapter inherit the same non-owning contract.
static_assert(!std::is_constructible_v<KdTreeAdapter, Geometry::KDTree&&>,
              "KdTreeAdapter must not bind to an rvalue Geometry::KDTree");
static_assert(!std::is_constructible_v<KdTreeAdapter, const Geometry::KDTree&&>,
              "KdTreeAdapter must not bind to a const rvalue Geometry::KDTree");
static_assert(std::is_constructible_v<KdTreeAdapter, const Geometry::KDTree&>,
              "KdTreeAdapter must remain constructible from a const lvalue Geometry::KDTree");
static_assert(!std::is_constructible_v<OctreeAdapter, Geometry::Octree&&>,
              "OctreeAdapter must not bind to an rvalue Geometry::Octree");
static_assert(!std::is_constructible_v<OctreeAdapter, const Geometry::Octree&&>,
              "OctreeAdapter must not bind to a const rvalue Geometry::Octree");
static_assert(std::is_constructible_v<OctreeAdapter, const Geometry::Octree&>,
              "OctreeAdapter must remain constructible from a const lvalue Geometry::Octree");

// ConvexHullAdapter inherits the same non-owning contract.
static_assert(!std::is_constructible_v<ConvexHullAdapter, Geometry::ConvexHull&&>,
              "ConvexHullAdapter must not bind to an rvalue Geometry::ConvexHull");
static_assert(!std::is_constructible_v<ConvexHullAdapter, const Geometry::ConvexHull&&>,
              "ConvexHullAdapter must not bind to a const rvalue Geometry::ConvexHull");
static_assert(std::is_constructible_v<ConvexHullAdapter, const Geometry::ConvexHull&>,
              "ConvexHullAdapter must remain constructible from a const lvalue Geometry::ConvexHull");

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

    // Four well-separated AABBs along X also build a non-trivial KDTree once
    // LeafSize=1 forces splitting between every box.
    [[nodiscard]] Geometry::KDTree MakeFourBoxKdTreeFixture()
    {
        std::vector<Geometry::AABB> boxes{
            Geometry::AABB{{-4.0f, -1.0f, -1.0f}, {-3.0f, 1.0f, 1.0f}},
            Geometry::AABB{{-1.0f, -1.0f, -1.0f}, { 0.0f, 1.0f, 1.0f}},
            Geometry::AABB{{ 1.0f, -1.0f, -1.0f}, { 2.0f, 1.0f, 1.0f}},
            Geometry::AABB{{ 3.0f, -1.0f, -1.0f}, { 4.0f, 1.0f, 1.0f}},
        };

        Geometry::KDTree kdTree;
        Geometry::KDTreeBuildParams params{};
        params.LeafSize = 1u;
        EXPECT_TRUE(kdTree.Build(boxes, params).has_value());
        return kdTree;
    }

    [[nodiscard]] std::uint32_t CountLeavesAndInners(const Geometry::KDTree& kdTree,
                                                    std::uint32_t&          leafOut,
                                                    std::uint32_t&          innerOut)
    {
        leafOut  = 0u;
        innerOut = 0u;
        for (const auto& node : kdTree.Nodes())
        {
            if (node.IsLeaf) ++leafOut;
            else             ++innerOut;
        }
        return leafOut + innerOut;
    }

    // Eight well-separated AABBs (one per octant) split the root into eight
    // children with the Center policy at maxPerNode=1.
    [[nodiscard]] Geometry::Octree MakeEightBoxOctreeFixture()
    {
        std::vector<Geometry::AABB> boxes;
        boxes.reserve(8u);
        const float half = 0.25f;
        for (int z = 0; z < 2; ++z)
            for (int y = 0; y < 2; ++y)
                for (int x = 0; x < 2; ++x)
                {
                    const glm::vec3 center{
                        x == 0 ? -1.0f : 1.0f,
                        y == 0 ? -1.0f : 1.0f,
                        z == 0 ? -1.0f : 1.0f,
                    };
                    boxes.push_back(Geometry::AABB{center - glm::vec3{half}, center + glm::vec3{half}});
                }

        Geometry::Octree octree;
        Geometry::Octree::SplitPolicy policy{};
        policy.SplitPoint = Geometry::Octree::SplitPoint::Center;
        policy.TightChildren = false;
        EXPECT_TRUE(octree.Build(std::move(boxes), policy, /*maxPerNode*/ 1u, /*maxDepth*/ 8u));
        return octree;
    }

    [[nodiscard]] std::uint32_t CountLeavesAndInners(const Geometry::Octree& octree,
                                                    std::uint32_t&          leafOut,
                                                    std::uint32_t&          innerOut)
    {
        leafOut  = 0u;
        innerOut = 0u;
        for (const auto& node : octree.m_Nodes)
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

TEST(SpatialDebugAdapters, KdTreeAdapterAppendsDeterministicNodesAndPlanes)
{
    const Geometry::KDTree kdTree = MakeFourBoxKdTreeFixture();
    ASSERT_FALSE(kdTree.Nodes().empty()) << "fixture must produce a non-empty KDTree";

    std::uint32_t expectedLeaves = 0u;
    std::uint32_t expectedInners = 0u;
    const std::uint32_t expectedTotal = CountLeavesAndInners(kdTree, expectedLeaves, expectedInners);

    SpatialDebugSnapshotBatch batch{};
    SpatialDebugAdapterStats  stats{};
    KdTreeAdapter             adapter{kdTree};

    adapter.Append(batch, SpatialDebugAdapterOptions{}, stats);

    EXPECT_EQ(batch.HierarchyNodes.size(), static_cast<std::size_t>(expectedTotal));
    EXPECT_EQ(batch.Bounds.size(),         static_cast<std::size_t>(expectedTotal));
    EXPECT_EQ(batch.SplitPlanes.size(),    static_cast<std::size_t>(expectedInners));

    EXPECT_EQ(stats.LeafNodeCount,           expectedLeaves);
    EXPECT_EQ(stats.InnerNodeCount,          expectedInners);
    EXPECT_EQ(stats.SplitPlaneCount,         expectedInners);
    EXPECT_EQ(stats.DepthCapTruncationCount, 0u);

    // Root is always the first emitted hierarchy node (DFS from index 0).
    const auto& rootNode = kdTree.Nodes()[0];
    ASSERT_FALSE(batch.HierarchyNodes.empty());
    EXPECT_EQ(batch.HierarchyNodes.front().Depth, 0u);
    EXPECT_EQ(batch.HierarchyNodes.front().IsLeaf, rootNode.IsLeaf);
    EXPECT_FLOAT_EQ(batch.HierarchyNodes.front().Bounds.Min.x, rootNode.Aabb.Min.x);
    EXPECT_FLOAT_EQ(batch.HierarchyNodes.front().Bounds.Max.x, rootNode.Aabb.Max.x);

    // Pin the root split plane's (Axis, Position) to the KDTree's own value.
    if (!rootNode.IsLeaf)
    {
        ASSERT_FALSE(batch.SplitPlanes.empty());
        const auto& rootPlane = batch.SplitPlanes.front();
        EXPECT_EQ(static_cast<std::uint8_t>(rootPlane.Axis), rootNode.SplitAxis);
        EXPECT_FLOAT_EQ(rootPlane.Position, rootNode.SplitValue);
    }

    // ConvexHull spans stay untouched by the KDTree adapter.
    EXPECT_TRUE(batch.ConvexHullVertices.empty());
    EXPECT_TRUE(batch.ConvexHullEdges.empty());
    EXPECT_TRUE(batch.PointMarkers.empty());
}

TEST(SpatialDebugAdapters, KdTreeAdapterLeafOnlyFilterDropsInnerNodes)
{
    const Geometry::KDTree kdTree = MakeFourBoxKdTreeFixture();
    std::uint32_t expectedLeaves = 0u;
    std::uint32_t expectedInners = 0u;
    (void)CountLeavesAndInners(kdTree, expectedLeaves, expectedInners);
    ASSERT_GT(expectedInners, 0u) << "fixture should split into at least one inner node";

    SpatialDebugSnapshotBatch batch{};
    SpatialDebugAdapterStats  stats{};
    KdTreeAdapter             adapter{kdTree};

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

TEST(SpatialDebugAdapters, KdTreeAdapterDepthCapTruncatesAndCounts)
{
    const Geometry::KDTree kdTree = MakeFourBoxKdTreeFixture();
    ASSERT_FALSE(kdTree.Nodes()[0].IsLeaf)
        << "fixture root must be an inner node for the truncation test";

    SpatialDebugSnapshotBatch batch{};
    SpatialDebugAdapterStats  stats{};
    KdTreeAdapter             adapter{kdTree};

    SpatialDebugAdapterOptions options{};
    options.MaxDepth = 0u;
    adapter.Append(batch, options, stats);

    EXPECT_EQ(batch.HierarchyNodes.size(), 1u);
    EXPECT_EQ(batch.SplitPlanes.size(),    1u);
    EXPECT_EQ(stats.DepthCapTruncationCount, 1u);
    EXPECT_EQ(batch.HierarchyNodes.front().Depth, 0u);
    EXPECT_FALSE(batch.HierarchyNodes.front().IsLeaf);
}

TEST(SpatialDebugAdapters, OctreeAdapterAppendsDeterministicNodesAndPlanes)
{
    const Geometry::Octree octree = MakeEightBoxOctreeFixture();
    ASSERT_FALSE(octree.m_Nodes.empty()) << "fixture must produce a non-empty Octree";

    std::uint32_t expectedLeaves = 0u;
    std::uint32_t expectedInners = 0u;
    const std::uint32_t expectedTotal = CountLeavesAndInners(octree, expectedLeaves, expectedInners);
    ASSERT_GT(expectedInners, 0u) << "fixture should split the root into octants";

    SpatialDebugSnapshotBatch batch{};
    SpatialDebugAdapterStats  stats{};
    OctreeAdapter             adapter{octree};

    adapter.Append(batch, SpatialDebugAdapterOptions{}, stats);

    EXPECT_EQ(batch.HierarchyNodes.size(), static_cast<std::size_t>(expectedTotal));
    EXPECT_EQ(batch.Bounds.size(),         static_cast<std::size_t>(expectedTotal));
    EXPECT_EQ(batch.SplitPlanes.size(),    static_cast<std::size_t>(expectedInners) * 3u);

    EXPECT_EQ(stats.LeafNodeCount,           expectedLeaves);
    EXPECT_EQ(stats.InnerNodeCount,          expectedInners);
    EXPECT_EQ(stats.SplitPlaneCount,         expectedInners * 3u);
    EXPECT_EQ(stats.DepthCapTruncationCount, 0u);

    // Root is always the first emitted hierarchy node (DFS from index 0).
    const auto& rootNode = octree.m_Nodes[0];
    ASSERT_FALSE(batch.HierarchyNodes.empty());
    EXPECT_EQ(batch.HierarchyNodes.front().Depth, 0u);
    EXPECT_EQ(batch.HierarchyNodes.front().IsLeaf, rootNode.IsLeaf);

    // Root contributes three split planes (one per axis) at the
    // parent AABB center.
    if (!rootNode.IsLeaf)
    {
        const glm::vec3 rootCenter = 0.5f * (rootNode.Aabb.Min + rootNode.Aabb.Max);
        ASSERT_GE(batch.SplitPlanes.size(), 3u);
        const auto& xPlane = batch.SplitPlanes[0];
        const auto& yPlane = batch.SplitPlanes[1];
        const auto& zPlane = batch.SplitPlanes[2];
        EXPECT_EQ(xPlane.Axis, SpatialDebugSplitAxis::X);
        EXPECT_EQ(yPlane.Axis, SpatialDebugSplitAxis::Y);
        EXPECT_EQ(zPlane.Axis, SpatialDebugSplitAxis::Z);
        EXPECT_FLOAT_EQ(xPlane.Position, rootCenter.x);
        EXPECT_FLOAT_EQ(yPlane.Position, rootCenter.y);
        EXPECT_FLOAT_EQ(zPlane.Position, rootCenter.z);
    }
}

TEST(SpatialDebugAdapters, OctreeAdapterLeafOnlyFilterDropsInnerNodes)
{
    const Geometry::Octree octree = MakeEightBoxOctreeFixture();
    std::uint32_t expectedLeaves = 0u;
    std::uint32_t expectedInners = 0u;
    (void)CountLeavesAndInners(octree, expectedLeaves, expectedInners);

    SpatialDebugSnapshotBatch batch{};
    SpatialDebugAdapterStats  stats{};
    OctreeAdapter             adapter{octree};

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

TEST(SpatialDebugAdapters, OctreeAdapterDepthCapTruncatesAndCounts)
{
    const Geometry::Octree octree = MakeEightBoxOctreeFixture();
    ASSERT_FALSE(octree.m_Nodes[0].IsLeaf)
        << "fixture root must be an inner node for the truncation test";

    SpatialDebugSnapshotBatch batch{};
    SpatialDebugAdapterStats  stats{};
    OctreeAdapter             adapter{octree};

    SpatialDebugAdapterOptions options{};
    options.MaxDepth = 0u;
    adapter.Append(batch, options, stats);

    EXPECT_EQ(batch.HierarchyNodes.size(), 1u);
    EXPECT_EQ(batch.SplitPlanes.size(),    3u);
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

namespace
{
    // Unit cube centered at the origin: eight vertices, six axis-aligned face
    // planes, twelve edges. Vertex ordering matches the lexicographic (x, y, z)
    // octant enumeration so the expected edge pairs are deterministic.
    [[nodiscard]] Geometry::ConvexHull MakeUnitCubeHullFixture()
    {
        Geometry::ConvexHull hull{};
        hull.Vertices.reserve(8u);
        for (int z = 0; z < 2; ++z)
            for (int y = 0; y < 2; ++y)
                for (int x = 0; x < 2; ++x)
                    hull.Vertices.push_back(glm::vec3{
                        x == 0 ? -1.0f : 1.0f,
                        y == 0 ? -1.0f : 1.0f,
                        z == 0 ? -1.0f : 1.0f,
                    });

        // Face planes with outward normals; SignedDistance = dot(N, v) + D.
        // For each face, D = -dot(N, point-on-face).
        hull.Planes.push_back({{-1.0f, 0.0f, 0.0f}, -1.0f}); // x == -1
        hull.Planes.push_back({{ 1.0f, 0.0f, 0.0f}, -1.0f}); // x ==  1
        hull.Planes.push_back({{0.0f, -1.0f, 0.0f}, -1.0f}); // y == -1
        hull.Planes.push_back({{0.0f,  1.0f, 0.0f}, -1.0f}); // y ==  1
        hull.Planes.push_back({{0.0f, 0.0f, -1.0f}, -1.0f}); // z == -1
        hull.Planes.push_back({{0.0f, 0.0f,  1.0f}, -1.0f}); // z ==  1
        return hull;
    }
}

TEST(SpatialDebugAdapters, ConvexHullAdapterEmitsVerticesAndDerivedEdges)
{
    const Geometry::ConvexHull hull = MakeUnitCubeHullFixture();

    SpatialDebugSnapshotBatch batch{};
    SpatialDebugAdapterStats  stats{};
    ConvexHullAdapter         adapter{hull};

    adapter.Append(batch, SpatialDebugAdapterOptions{}, stats);

    // All eight hull vertices are copied verbatim into the batch span.
    ASSERT_EQ(batch.ConvexHullVertices.size(), hull.Vertices.size());
    for (std::size_t vi = 0u; vi < hull.Vertices.size(); ++vi)
    {
        EXPECT_FLOAT_EQ(batch.ConvexHullVertices[vi].x, hull.Vertices[vi].x);
        EXPECT_FLOAT_EQ(batch.ConvexHullVertices[vi].y, hull.Vertices[vi].y);
        EXPECT_FLOAT_EQ(batch.ConvexHullVertices[vi].z, hull.Vertices[vi].z);
    }

    // A unit cube has exactly twelve edges; pairs (i, j) with i < j and
    // exactly one differing bit in the octant index encode adjacency.
    EXPECT_EQ(batch.ConvexHullEdges.size(), 12u);
    for (const auto& edge : batch.ConvexHullEdges)
    {
        ASSERT_LT(edge.A, edge.B);
        const std::uint32_t diff = edge.A ^ edge.B;
        EXPECT_TRUE(diff == 1u || diff == 2u || diff == 4u)
            << "edge {" << edge.A << "," << edge.B
            << "} should differ in exactly one cube-octant bit";
    }

    // Tree-shaped accumulators stay untouched for a flat hull.
    EXPECT_EQ(stats.LeafNodeCount,           0u);
    EXPECT_EQ(stats.InnerNodeCount,          0u);
    EXPECT_EQ(stats.SplitPlaneCount,         0u);
    EXPECT_EQ(stats.EmptyNodeSkippedCount,   0u);
    EXPECT_EQ(stats.DepthCapTruncationCount, 0u);

    // Tree-shaped spans stay untouched by the hull adapter.
    EXPECT_TRUE(batch.Bounds.empty());
    EXPECT_TRUE(batch.HierarchyNodes.empty());
    EXPECT_TRUE(batch.SplitPlanes.empty());
    EXPECT_TRUE(batch.PointMarkers.empty());
}

TEST(SpatialDebugAdapters, ConvexHullAdapterRemapsIndicesAcrossMultiAppend)
{
    const Geometry::ConvexHull hull = MakeUnitCubeHullFixture();

    SpatialDebugSnapshotBatch batch{};
    SpatialDebugAdapterStats  stats{};
    ConvexHullAdapter         adapter{hull};

    adapter.Append(batch, SpatialDebugAdapterOptions{}, stats);
    const auto firstVertexCount = batch.ConvexHullVertices.size();
    const auto firstEdgeCount   = batch.ConvexHullEdges.size();

    // Second Append from the same hull must add another vertex block and
    // emit edges whose indices reference the second block, not the first.
    adapter.Append(batch, SpatialDebugAdapterOptions{}, stats);

    ASSERT_EQ(batch.ConvexHullVertices.size(), firstVertexCount * 2u);
    ASSERT_EQ(batch.ConvexHullEdges.size(),    firstEdgeCount   * 2u);

    const auto offset = static_cast<std::uint32_t>(firstVertexCount);
    for (std::size_t ei = firstEdgeCount; ei < batch.ConvexHullEdges.size(); ++ei)
    {
        const auto& edge = batch.ConvexHullEdges[ei];
        EXPECT_GE(edge.A, offset);
        EXPECT_GE(edge.B, offset);
        EXPECT_LT(edge.A, offset + firstVertexCount);
        EXPECT_LT(edge.B, offset + firstVertexCount);
    }
}

TEST(SpatialDebugAdapters, ConvexHullAdapterHandlesEmptyHull)
{
    Geometry::ConvexHull hull{}; // Default: no vertices, no planes.

    SpatialDebugSnapshotBatch batch{};
    SpatialDebugAdapterStats  stats{};
    ConvexHullAdapter         adapter{hull};

    adapter.Append(batch, SpatialDebugAdapterOptions{}, stats);

    EXPECT_TRUE(batch.ConvexHullVertices.empty());
    EXPECT_TRUE(batch.ConvexHullEdges.empty());
}

TEST(SpatialDebugAdapters, RegistryRegistersFindsAndUnregistersAdapters)
{
    const Geometry::BVH        bvh    = MakeFourBoxFixture();
    const Geometry::KDTree     kdTree = MakeFourBoxKdTreeFixture();
    const Geometry::ConvexHull hull   = MakeUnitCubeHullFixture();

    BvhAdapter        bvhAdapter{bvh};
    KdTreeAdapter     kdAdapter{kdTree};
    ConvexHullAdapter hullAdapter{hull};

    SpatialDebugAdapterRegistry registry{};
    EXPECT_TRUE(registry.Empty());
    EXPECT_EQ(registry.Size(), 0u);
    EXPECT_EQ(registry.Find(7u), nullptr);

    registry.Register(1u, bvhAdapter);
    registry.Register(2u, kdAdapter);
    registry.Register(3u, hullAdapter);

    EXPECT_FALSE(registry.Empty());
    EXPECT_EQ(registry.Size(), 3u);
    EXPECT_TRUE(registry.Contains(1u));
    EXPECT_EQ(registry.Find(1u), static_cast<const ISpatialDebugAdapter*>(&bvhAdapter));
    EXPECT_EQ(registry.Find(2u), static_cast<const ISpatialDebugAdapter*>(&kdAdapter));
    EXPECT_EQ(registry.Find(3u), static_cast<const ISpatialDebugAdapter*>(&hullAdapter));
    EXPECT_EQ(registry.Find(99u), nullptr);

    // Re-registering an existing key replaces the entry without growing
    // the table.
    BvhAdapter replacementAdapter{bvh};
    registry.Register(1u, replacementAdapter);
    EXPECT_EQ(registry.Size(), 3u);
    EXPECT_EQ(registry.Find(1u),
              static_cast<const ISpatialDebugAdapter*>(&replacementAdapter));

    EXPECT_TRUE(registry.Unregister(2u));
    EXPECT_FALSE(registry.Contains(2u));
    EXPECT_EQ(registry.Find(2u), nullptr);
    EXPECT_EQ(registry.Size(), 2u);
    EXPECT_FALSE(registry.Unregister(2u)); // Already removed.

    registry.Clear();
    EXPECT_TRUE(registry.Empty());
    EXPECT_EQ(registry.Find(1u), nullptr);
}

TEST(SpatialDebugAdapters, RegistryResolvedAdapterRoundTripsThroughISpatialDebugAdapter)
{
    const Geometry::ConvexHull hull = MakeUnitCubeHullFixture();
    ConvexHullAdapter          hullAdapter{hull};

    SpatialDebugAdapterRegistry registry{};
    registry.Register(42u, hullAdapter);

    const ISpatialDebugAdapter* resolved = registry.Find(42u);
    ASSERT_NE(resolved, nullptr);

    SpatialDebugSnapshotBatch batch{};
    SpatialDebugAdapterStats  stats{};
    resolved->Append(batch, SpatialDebugAdapterOptions{}, stats);

    EXPECT_EQ(batch.ConvexHullVertices.size(), hull.Vertices.size());
    EXPECT_EQ(batch.ConvexHullEdges.size(),    12u);
}


// =============================================================================
// Consolidated debug-draw visualization tests for spatial data structures
// and bounding volumes.
//
// Merges: Test_BoundingDebugDraw, Test_BVHDebugDraw, Test_KDTreeDebugDraw,
//         Test_ConvexHullDebugDraw, Test_OctreeDebugDraw, Test_GraphDebugDraw.
//
// All tests follow the same contract pattern:
//   - Disabled => emits nothing
//   - Overlay vs depth-tested routing
//   - Expected segment counts
//   - Budget enforcement
// =============================================================================

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <entt/entity/fwd.hpp>

import Graphics;
import ECS;
import Geometry;

using Graphics::DebugDraw;

// =============================================================================
// Shared test data builders
// =============================================================================

namespace
{

Geometry::AABB MakeLocalAabb()
{
    return Geometry::AABB{glm::vec3{-1.0f, -2.0f, -0.5f}, glm::vec3{1.0f, 2.0f, 0.5f}};
}

Geometry::OBB MakeWorldObb()
{
    Geometry::OBB obb;
    obb.Center = glm::vec3(10.0f, -2.0f, 5.0f);
    obb.Extents = glm::vec3(3.0f, 2.0f, 1.0f);
    obb.Rotation = glm::angleAxis(glm::radians(45.0f), glm::normalize(glm::vec3(0.0f, 1.0f, 1.0f)));
    return obb;
}

Geometry::KDTree MakeSimpleKDTree()
{
    Geometry::KDTree tree;
    std::vector<glm::vec3> points{
        glm::vec3{-10.0f, 0.0f, 0.0f},
        glm::vec3{10.0f, 0.0f, 0.0f}
    };
    Geometry::KDTreeBuildParams params{};
    params.LeafSize = 1;
    params.MaxDepth = 8;
    auto build = tree.BuildFromPoints(points, params);
    EXPECT_TRUE(build.has_value());
    EXPECT_GE(build->NodeCount, 3u);
    return tree;
}

Geometry::Octree MakeSimpleOctree()
{
    std::vector<Geometry::AABB> elems;
    elems.push_back(Geometry::AABB{glm::vec3{-10.0f, -1.0f, -1.0f}, glm::vec3{-9.0f, 1.0f, 1.0f}});
    elems.push_back(Geometry::AABB{glm::vec3{  9.0f, -1.0f, -1.0f}, glm::vec3{10.0f, 1.0f, 1.0f}});
    Geometry::Octree o;
    Geometry::Octree::SplitPolicy policy;
    policy.SplitPoint = Geometry::Octree::SplitPoint::Center;
    policy.TightChildren = true;
    const bool ok = o.Build(std::move(elems), policy, /*maxPerNode*/ 1, /*maxDepth*/ 8);
    EXPECT_TRUE(ok);
    EXPECT_FALSE(o.m_Nodes.empty());
    return o;
}

Geometry::Halfedge::Mesh MakeTetraHullMesh()
{
    std::vector<glm::vec3> points{
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
    };
    Geometry::ConvexHullBuilder::ConvexHullParams params{};
    params.BuildMesh = true;
    params.ComputePlanes = false;
    auto hull = Geometry::ConvexHullBuilder::Build(points, params);
    EXPECT_TRUE(hull.has_value());
    EXPECT_FALSE(hull->Mesh.IsEmpty());
    return hull ? std::move(hull->Mesh) : Geometry::Halfedge::Mesh{};
}

} // namespace

// =============================================================================
// Bounding Volume Debug Draw
// =============================================================================

TEST(BoundingDebugDraw, DisabledEmitsNothing)
{
    DebugDraw dd;
    Graphics::BoundingDebugDrawSettings s;
    s.Enabled = false;
    DrawBoundingVolumes(dd, MakeLocalAabb(), MakeWorldObb(), s);
    EXPECT_EQ(dd.GetLineCount(), 0u);
    EXPECT_EQ(dd.GetOverlayLineCount(), 0u);
}

TEST(BoundingDebugDraw, DrawObbAndAabb)
{
    DebugDraw dd;
    Graphics::BoundingDebugDrawSettings s;
    s.Enabled = true;
    s.Overlay = false;
    s.DrawAABB = true;
    s.DrawOBB = true;
    s.DrawBoundingSphere = false;
    DrawBoundingVolumes(dd, MakeLocalAabb(), MakeWorldObb(), s);
    EXPECT_EQ(dd.GetLineCount(), 24u); // AABB: 12 + OBB: 12
    EXPECT_EQ(dd.GetOverlayLineCount(), 0u);
}

TEST(BoundingDebugDraw, DrawSphereAddsExpectedSegments)
{
    DebugDraw dd;
    Graphics::BoundingDebugDrawSettings s;
    s.Enabled = true;
    s.Overlay = true;
    s.DrawAABB = false;
    s.DrawOBB = false;
    s.DrawBoundingSphere = true;
    DrawBoundingVolumes(dd, MakeLocalAabb(), MakeWorldObb(), s);
    EXPECT_EQ(dd.GetOverlayLineCount(), 72u); // 3 circles * 24 segments
    EXPECT_EQ(dd.GetLineCount(), 0u);
}

TEST(BoundingDebugDraw, InvalidInputRejected)
{
    DebugDraw dd;
    Geometry::AABB invalidLocal;
    invalidLocal.Min = glm::vec3(2.0f);
    invalidLocal.Max = glm::vec3(-2.0f);
    Graphics::BoundingDebugDrawSettings s;
    s.Enabled = true;
    DrawBoundingVolumes(dd, invalidLocal, MakeWorldObb(), s);
    EXPECT_EQ(dd.GetLineCount(), 0u);
    EXPECT_EQ(dd.GetOverlayLineCount(), 0u);
}

// =============================================================================
// BVH Debug Draw
// =============================================================================

TEST(BVHDebugDraw, DisabledEmitsNothing)
{
    DebugDraw dd;
    std::vector<glm::vec3> positions{
        {-1.0f, -1.0f, 0.0f}, {1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 0.0f},
        {-1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {-1.0f, 1.0f, 0.0f}
    };
    Graphics::BVHDebugDrawSettings settings{};
    settings.Enabled = false;
    DrawBVH(dd, positions, {}, settings);
    EXPECT_EQ(dd.GetLineCount(), 0u);
    EXPECT_EQ(dd.GetOverlayLineCount(), 0u);
}

TEST(BVHDebugDraw, OverlayRootOnly)
{
    DebugDraw dd;
    std::vector<glm::vec3> positions{
        {-2.0f, 0.0f, 0.0f}, {-1.0f, 1.0f, 0.0f}, {-1.0f, -1.0f, 0.0f},
        {1.0f, 0.0f, 0.0f}, {2.0f, 1.0f, 0.0f}, {2.0f, -1.0f, 0.0f}
    };
    Graphics::BVHDebugDrawSettings settings{};
    settings.Enabled = true;
    settings.Overlay = true;
    settings.MaxDepth = 0;
    DrawBVH(dd, positions, {}, settings);
    EXPECT_EQ(dd.GetOverlayLineCount(), 12u);
    EXPECT_EQ(dd.GetLineCount(), 0u);
}

TEST(BVHDebugDraw, LeafOnlyDrawsLeaves)
{
    DebugDraw dd;
    std::vector<glm::vec3> positions{
        {-2.0f, 0.0f, 0.0f}, {-1.0f, 1.0f, 0.0f}, {-1.0f, -1.0f, 0.0f},
        {1.0f, 0.0f, 0.0f}, {2.0f, 1.0f, 0.0f}, {2.0f, -1.0f, 0.0f}
    };
    Graphics::BVHDebugDrawSettings settings{};
    settings.Enabled = true;
    settings.Overlay = false;
    settings.MaxDepth = 4;
    settings.LeafOnly = true;
    settings.DrawInternal = false;
    settings.LeafTriangleCount = 1;
    DrawBVH(dd, positions, {}, settings);
    EXPECT_EQ(dd.GetLineCount(), 24u);
    EXPECT_EQ(dd.GetOverlayLineCount(), 0u);
}

// =============================================================================
// KD-Tree Debug Draw
// =============================================================================

TEST(KDTreeDebugDraw, DisabledEmitsNothing)
{
    DebugDraw dd;
    Geometry::KDTree tree = MakeSimpleKDTree();
    Graphics::KDTreeDebugDrawSettings s;
    s.Enabled = false;
    DrawKDTree(dd, tree, s);
    EXPECT_EQ(dd.GetLineCount(), 0u);
    EXPECT_EQ(dd.GetOverlayLineCount(), 0u);
}

TEST(KDTreeDebugDraw, MaxDepthZeroDrawsRootAndSplitPlane)
{
    DebugDraw dd;
    Geometry::KDTree tree = MakeSimpleKDTree();
    Graphics::KDTreeDebugDrawSettings s;
    s.Enabled = true;
    s.Overlay = true;
    s.MaxDepth = 0;
    s.LeafOnly = false;
    s.DrawInternal = true;
    s.DrawSplitPlanes = true;
    s.OccupiedOnly = false;
    DrawKDTree(dd, tree, s);
    EXPECT_EQ(dd.GetOverlayLineCount(), 16u); // Root AABB (12) + split plane (4)
    EXPECT_EQ(dd.GetLineCount(), 0u);
}

TEST(KDTreeDebugDraw, LeafOnlyDrawsLeafBoxes)
{
    DebugDraw dd;
    Geometry::KDTree tree = MakeSimpleKDTree();
    Graphics::KDTreeDebugDrawSettings s;
    s.Enabled = true;
    s.Overlay = true;
    s.MaxDepth = 8;
    s.LeafOnly = true;
    s.DrawInternal = false;
    s.DrawSplitPlanes = false;
    s.OccupiedOnly = true;
    DrawKDTree(dd, tree, s);
    EXPECT_EQ(dd.GetOverlayLineCount(), 24u); // 2 leaves * 12
    EXPECT_EQ(dd.GetLineCount(), 0u);
}

TEST(KDTreeDebugDraw, DepthTestedRouteUsesDepthLines)
{
    DebugDraw dd;
    Geometry::KDTree tree = MakeSimpleKDTree();
    Graphics::KDTreeDebugDrawSettings s;
    s.Enabled = true;
    s.Overlay = false;
    s.MaxDepth = 0;
    s.OccupiedOnly = false;
    DrawKDTree(dd, tree, s);
    EXPECT_EQ(dd.GetLineCount(), 16u);
    EXPECT_EQ(dd.GetOverlayLineCount(), 0u);
}

TEST(KDTreeDebugDraw, RespectsDebugDrawBudget)
{
    DebugDraw dd;
    dd.SetMaxLineSegments(12);
    Geometry::KDTree tree = MakeSimpleKDTree();
    Graphics::KDTreeDebugDrawSettings s;
    s.Enabled = true;
    s.Overlay = true;
    s.MaxDepth = 0;
    s.LeafOnly = false;
    s.DrawInternal = true;
    s.DrawSplitPlanes = true;
    s.OccupiedOnly = false;
    DrawKDTree(dd, tree, s);
    EXPECT_EQ(dd.GetOverlayLineCount(), 12u);
    EXPECT_EQ(dd.GetRemainingLineCapacity(), 0u);
}

// =============================================================================
// Convex Hull Debug Draw
// =============================================================================

TEST(ConvexHullDebugDraw, DisabledEmitsNothing)
{
    DebugDraw dd;
    auto mesh = MakeTetraHullMesh();
    Graphics::ConvexHullDebugDrawSettings s;
    s.Enabled = false;
    DrawConvexHull(dd, mesh, s);
    EXPECT_EQ(dd.GetLineCount(), 0u);
    EXPECT_EQ(dd.GetOverlayLineCount(), 0u);
}

TEST(ConvexHullDebugDraw, OverlayDrawsHullEdges)
{
    DebugDraw dd;
    auto mesh = MakeTetraHullMesh();
    Graphics::ConvexHullDebugDrawSettings s;
    s.Enabled = true;
    s.Overlay = true;
    DrawConvexHull(dd, mesh, s);
    EXPECT_EQ(dd.GetOverlayLineCount(), 12u); // Tetrahedron: 4 faces * 3 edges
    EXPECT_EQ(dd.GetLineCount(), 0u);
}

TEST(ConvexHullDebugDraw, DepthTestedRouteUsesDepthLines)
{
    DebugDraw dd;
    auto mesh = MakeTetraHullMesh();
    Graphics::ConvexHullDebugDrawSettings s;
    s.Enabled = true;
    s.Overlay = false;
    DrawConvexHull(dd, mesh, s);
    EXPECT_EQ(dd.GetLineCount(), 12u);
    EXPECT_EQ(dd.GetOverlayLineCount(), 0u);
}

// =============================================================================
// Octree Debug Draw
// =============================================================================

TEST(OctreeDebugDraw, DisabledEmitsNothing)
{
    DebugDraw dd;
    Geometry::Octree o = MakeSimpleOctree();
    Graphics::OctreeDebugDrawSettings s;
    s.Enabled = false;
    DrawOctree(dd, o, s);
    EXPECT_EQ(dd.GetLineCount(), 0u);
    EXPECT_EQ(dd.GetOverlayLineCount(), 0u);
}

TEST(OctreeDebugDraw, MaxDepthZeroDrawsOnlyRoot)
{
    DebugDraw dd;
    Geometry::Octree o = MakeSimpleOctree();
    Graphics::OctreeDebugDrawSettings s;
    s.Enabled = true;
    s.Overlay = true;
    s.OccupiedOnly = false;
    s.MaxDepth = 0;
    DrawOctree(dd, o, s);
    EXPECT_EQ(dd.GetOverlayLineCount(), 12u); // 1 AABB box
    EXPECT_EQ(dd.GetLineCount(), 0u);
}

TEST(OctreeDebugDraw, LeafOnlyProducesMultipleBoxes)
{
    DebugDraw dd;
    Geometry::Octree o = MakeSimpleOctree();
    Graphics::OctreeDebugDrawSettings s;
    s.Enabled = true;
    s.Overlay = true;
    s.OccupiedOnly = true;
    s.LeafOnly = true;
    s.DrawInternal = false;
    s.MaxDepth = 8;
    DrawOctree(dd, o, s);
    EXPECT_GE(dd.GetOverlayLineCount(), 24u); // At least 2 leaves
    EXPECT_EQ(dd.GetOverlayLineCount() % 12u, 0u);
}

TEST(OctreeDebugDraw, DepthTestedRouteUsesDepthLines)
{
    DebugDraw dd;
    Geometry::Octree o = MakeSimpleOctree();
    Graphics::OctreeDebugDrawSettings s;
    s.Enabled = true;
    s.Overlay = false;
    s.OccupiedOnly = false;
    s.MaxDepth = 0;
    DrawOctree(dd, o, s);
    EXPECT_EQ(dd.GetLineCount(), 12u);
    EXPECT_EQ(dd.GetOverlayLineCount(), 0u);
}

TEST(OctreeDebugDraw, TransformOverloadAppliesMatrix)
{
    DebugDraw dd;
    Geometry::Octree o = MakeSimpleOctree();
    Graphics::OctreeDebugDrawSettings s;
    s.Enabled = true;
    s.Overlay = true;
    s.OccupiedOnly = false;
    s.MaxDepth = 0;
    glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(100.0f, 0.0f, 0.0f));
    DrawOctree(dd, o, s, transform);
    EXPECT_EQ(dd.GetOverlayLineCount(), 12u);
    auto lines = dd.GetOverlayLines();
    for (const auto& seg : lines)
    {
        EXPECT_GT(seg.Start.x, 50.0f) << "Line start should be translated by +100 in x";
        EXPECT_GT(seg.End.x, 50.0f) << "Line end should be translated by +100 in x";
    }
}

TEST(OctreeDebugDraw, RespectsDebugDrawBudget)
{
    DebugDraw dd;
    dd.SetMaxLineSegments(12);
    Geometry::Octree o = MakeSimpleOctree();
    Graphics::OctreeDebugDrawSettings s;
    s.Enabled = true;
    s.Overlay = true;
    s.OccupiedOnly = true;
    s.LeafOnly = true;
    s.DrawInternal = false;
    s.MaxDepth = 8;
    DrawOctree(dd, o, s);
    EXPECT_EQ(dd.GetOverlayLineCount(), 12u);
    EXPECT_EQ(dd.GetRemainingLineCapacity(), 0u);
}

// =============================================================================
// Graph Debug Draw
// =============================================================================

TEST(Graphics_GraphDebugDraw, GraphEntityWithDebugDraw)
{
    ECS::Scene scene;
    auto& reg = scene.GetRegistry();
    const entt::entity e = reg.create();
    auto graph = std::make_shared<Geometry::Graph::Graph>();
    auto v0 = graph->AddVertex(glm::vec3(0.0f));
    auto v1 = graph->AddVertex(glm::vec3(1.0f, 0.0f, 0.0f));
    (void)graph->AddEdge(v0, v1);
    ECS::Graph::Data graphData{};
    graphData.Visible = true;
    graphData.GraphRef = graph;
    reg.emplace<ECS::Graph::Data>(e, graphData);

    DebugDraw dd;
    EXPECT_EQ(dd.GetLineCount(), 0u);
    EXPECT_FALSE(dd.HasContent());
}

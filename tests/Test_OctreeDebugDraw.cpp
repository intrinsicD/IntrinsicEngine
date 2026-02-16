#include <gtest/gtest.h>

#include <cstdint>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

import Geometry;
import Graphics;

using Geometry::AABB;
using Geometry::Octree;
using Graphics::DebugDraw;

static Octree MakeSimpleOctree()
{
    // 2 distant AABBs to force at least one split with Center policy.
    std::vector<AABB> elems;
    elems.push_back(AABB{glm::vec3{-10.0f, -1.0f, -1.0f}, glm::vec3{-9.0f, 1.0f, 1.0f}});
    elems.push_back(AABB{glm::vec3{  9.0f, -1.0f, -1.0f}, glm::vec3{10.0f, 1.0f, 1.0f}});

    Octree o;
    Octree::SplitPolicy policy;
    policy.SplitPoint = Octree::SplitPoint::Center;
    policy.TightChildren = true;

    const bool ok = o.Build(std::move(elems), policy, /*maxPerNode*/ 1, /*maxDepth*/ 8);
    EXPECT_TRUE(ok);
    EXPECT_FALSE(o.m_Nodes.empty());
    return o;
}

TEST(OctreeDebugDraw, DisabledEmitsNothing)
{
    DebugDraw dd;
    Octree o = MakeSimpleOctree();

    Graphics::OctreeDebugDrawSettings s;
    s.Enabled = false;

    DrawOctree(dd, o, s);

    EXPECT_EQ(dd.GetLineCount(), 0u);
    EXPECT_EQ(dd.GetOverlayLineCount(), 0u);
}

TEST(OctreeDebugDraw, MaxDepthZeroDrawsOnlyRoot)
{
    DebugDraw dd;
    Octree o = MakeSimpleOctree();

    Graphics::OctreeDebugDrawSettings s;
    s.Enabled = true;
    s.Overlay = true;
    s.OccupiedOnly = false; // root may contain only straddlers depending on build
    s.MaxDepth = 0;

    DrawOctree(dd, o, s);

    // One AABB box = 12 line segments.
    EXPECT_EQ(dd.GetOverlayLineCount(), 12u);
    EXPECT_EQ(dd.GetLineCount(), 0u);
}

TEST(OctreeDebugDraw, LeafOnlyProducesMultipleBoxes)
{
    DebugDraw dd;
    Octree o = MakeSimpleOctree();

    Graphics::OctreeDebugDrawSettings s;
    s.Enabled = true;
    s.Overlay = true;
    s.OccupiedOnly = true;
    s.LeafOnly = true;
    s.DrawInternal = false;
    s.MaxDepth = 8;

    DrawOctree(dd, o, s);

    // At least two leaves in this construction.
    EXPECT_GE(dd.GetOverlayLineCount(), 24u);
    EXPECT_EQ(dd.GetOverlayLineCount() % 12u, 0u);
}

TEST(OctreeDebugDraw, DepthTestedRouteUsesDepthLines)
{
    DebugDraw dd;
    Octree o = MakeSimpleOctree();

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
    Octree o = MakeSimpleOctree();

    Graphics::OctreeDebugDrawSettings s;
    s.Enabled = true;
    s.Overlay = true;
    s.OccupiedOnly = false;
    s.MaxDepth = 0;

    // Translate by (100, 0, 0)
    glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(100.0f, 0.0f, 0.0f));
    DrawOctree(dd, o, s, transform);

    EXPECT_EQ(dd.GetOverlayLineCount(), 12u);

    // Verify the lines are translated: check that all line endpoints have x > 50
    auto lines = dd.GetOverlayLines();
    for (const auto& seg : lines)
    {
        EXPECT_GT(seg.Start.x, 50.0f) << "Line start should be translated by +100 in x";
        EXPECT_GT(seg.End.x, 50.0f) << "Line end should be translated by +100 in x";
    }
}


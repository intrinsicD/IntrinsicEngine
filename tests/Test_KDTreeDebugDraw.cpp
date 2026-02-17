#include <gtest/gtest.h>

#include <vector>
#include <glm/glm.hpp>

import Geometry;
import Graphics;

using Graphics::DebugDraw;

static Geometry::KDTree MakeSimpleKDTree()
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

    // Root AABB box (12) + root split plane rectangle (4)
    EXPECT_EQ(dd.GetOverlayLineCount(), 16u);
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

    // Two leaves in this setup: 2 * 12 lines.
    EXPECT_EQ(dd.GetOverlayLineCount(), 24u);
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

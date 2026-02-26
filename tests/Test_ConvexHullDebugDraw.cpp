#include <gtest/gtest.h>

#include <vector>
#include <glm/glm.hpp>

import Geometry;
import Graphics;

using Graphics::DebugDraw;

static Geometry::Halfedge::Mesh MakeTetraHullMesh()
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

    // Tetrahedron has 4 triangular faces; this debug view emits face-edge wires (4 * 3).
    EXPECT_EQ(dd.GetOverlayLineCount(), 12u);
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

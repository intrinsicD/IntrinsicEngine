#include <gtest/gtest.h>

#include <array>
#include <limits>

#include <glm/glm.hpp>

import Extrinsic.Graphics.SpatialDebugVisualizers;

using namespace Extrinsic;

TEST(GraphicsSpatialDebugVisualizers, BoundsWireframesEmitStableBoxEdges)
{
    const std::array<Graphics::SpatialDebugAabb, 1> boxes{{
        Graphics::SpatialDebugAabb{.Min = {0.f, 0.f, 0.f}, .Max = {1.f, 2.f, 3.f}},
    }};

    const auto result = Graphics::BuildSpatialDebugBoundsWireframes(boxes);

    EXPECT_EQ(result.Diagnostics.InputRecordCount, 1u);
    EXPECT_EQ(result.Diagnostics.EmittedLineCount, 12u);
    ASSERT_EQ(result.Lines.size(), 12u);
    EXPECT_EQ(result.Lines[0].Start, glm::vec3(0.f, 0.f, 0.f));
    EXPECT_EQ(result.Lines[0].End, glm::vec3(1.f, 0.f, 0.f));
    EXPECT_EQ(result.Lines[11].Start, glm::vec3(0.f, 2.f, 0.f));
    EXPECT_EQ(result.Lines[11].End, glm::vec3(0.f, 2.f, 3.f));
    EXPECT_FALSE(result.Diagnostics.TruncatedLineBudget);
}

TEST(GraphicsSpatialDebugVisualizers, HierarchyRejectsInvalidBoundsAndDepth)
{
    const std::array<Graphics::SpatialDebugHierarchyNode, 3> nodes{{
        Graphics::SpatialDebugHierarchyNode{
            .Bounds = {.Min = {0.f, 0.f, 0.f}, .Max = {1.f, 1.f, 1.f}},
            .Depth = 0u,
            .IsLeaf = false,
        },
        Graphics::SpatialDebugHierarchyNode{
            .Bounds = {.Min = {2.f, 0.f, 0.f}, .Max = {1.f, 1.f, 1.f}},
            .Depth = 1u,
            .IsLeaf = true,
        },
        Graphics::SpatialDebugHierarchyNode{
            .Bounds = {.Min = {0.f, 0.f, 0.f}, .Max = {1.f, 1.f, 1.f}},
            .Depth = 3u,
            .IsLeaf = true,
        },
    }};

    Graphics::SpatialDebugVisualizerOptions options{};
    options.MaxDepth = 2u;
    options.BranchColor = {0.7f, 0.1f, 0.2f, 1.f};

    const auto result = Graphics::BuildSpatialDebugHierarchyWireframes(nodes, options);

    EXPECT_EQ(result.Diagnostics.InputRecordCount, 3u);
    EXPECT_EQ(result.Diagnostics.EmittedLineCount, 12u);
    EXPECT_EQ(result.Diagnostics.RejectedInvalidBoundsCount, 1u);
    EXPECT_EQ(result.Diagnostics.RejectedDepthLimitCount, 1u);
    ASSERT_FALSE(result.Lines.empty());
    EXPECT_EQ(result.Lines[0].Color, options.BranchColor);
}

TEST(GraphicsSpatialDebugVisualizers, SplitPlanesAndConvexHullEdgesAreDeterministic)
{
    const std::array<Graphics::SpatialDebugSplitPlane, 1> planes{{
        Graphics::SpatialDebugSplitPlane{
            .Bounds = {.Min = {0.f, 0.f, 0.f}, .Max = {2.f, 4.f, 6.f}},
            .Axis = Graphics::SpatialDebugSplitAxis::Y,
            .Position = 1.f,
        },
    }};

    const auto planeResult = Graphics::BuildSpatialDebugSplitPlaneWireframes(planes);
    ASSERT_EQ(planeResult.Lines.size(), 4u);
    EXPECT_EQ(planeResult.Lines[0].Start, glm::vec3(0.f, 1.f, 0.f));
    EXPECT_EQ(planeResult.Lines[0].End, glm::vec3(2.f, 1.f, 0.f));

    const std::array<glm::vec3, 4> vertices{{
        glm::vec3{0.f, 0.f, 0.f},
        glm::vec3{1.f, 0.f, 0.f},
        glm::vec3{0.f, 1.f, 0.f},
        glm::vec3{0.f, 0.f, 1.f},
    }};
    const std::array<Graphics::SpatialDebugWireEdge, 4> edges{{
        Graphics::SpatialDebugWireEdge{.A = 0u, .B = 1u},
        Graphics::SpatialDebugWireEdge{.A = 1u, .B = 2u},
        Graphics::SpatialDebugWireEdge{.A = 2u, .B = 9u},
        Graphics::SpatialDebugWireEdge{.A = 3u, .B = 3u},
    }};

    const auto hullResult = Graphics::BuildSpatialDebugConvexHullWireframe(vertices, edges);
    EXPECT_EQ(hullResult.Diagnostics.InputRecordCount, 4u);
    EXPECT_EQ(hullResult.Diagnostics.EmittedLineCount, 2u);
    EXPECT_EQ(hullResult.Diagnostics.RejectedTopologyCount, 2u);
    ASSERT_EQ(hullResult.Lines.size(), 2u);
    EXPECT_EQ(hullResult.Lines[1].Start, glm::vec3(1.f, 0.f, 0.f));
    EXPECT_EQ(hullResult.Lines[1].End, glm::vec3(0.f, 1.f, 0.f));
}

TEST(GraphicsSpatialDebugVisualizers, BudgetsAndInvalidCoordinatesAreReported)
{
    const std::array<Graphics::SpatialDebugAabb, 2> boxes{{
        Graphics::SpatialDebugAabb{.Min = {0.f, 0.f, 0.f}, .Max = {1.f, 1.f, 1.f}},
        Graphics::SpatialDebugAabb{.Min = {2.f, 2.f, 2.f}, .Max = {3.f, 3.f, 3.f}},
    }};

    Graphics::SpatialDebugVisualizerOptions options{};
    options.MaxLinePackets = 13u;
    const auto result = Graphics::BuildSpatialDebugBoundsWireframes(boxes, options);
    EXPECT_EQ(result.Lines.size(), 13u);
    EXPECT_EQ(result.Diagnostics.EmittedLineCount, 13u);
    EXPECT_TRUE(result.Diagnostics.TruncatedLineBudget);

    const std::array<glm::vec3, 3> points{{
        glm::vec3{0.f, 0.f, 0.f},
        glm::vec3{std::numeric_limits<float>::infinity(), 0.f, 0.f},
        glm::vec3{1.f, 0.f, 0.f},
    }};
    options.MaxPointPackets = 1u;
    const auto pointResult = Graphics::BuildSpatialDebugPointMarkers(points, options);
    EXPECT_EQ(pointResult.Points.size(), 1u);
    EXPECT_EQ(pointResult.Diagnostics.EmittedPointCount, 1u);
    EXPECT_EQ(pointResult.Diagnostics.RejectedInvalidCoordinateCount, 1u);
    EXPECT_TRUE(pointResult.Diagnostics.TruncatedPointBudget);
}



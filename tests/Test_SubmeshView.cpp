#include <gtest/gtest.h>
#include <cstddef>

#include <glm/glm.hpp>

import Geometry;

#include "Test_MeshBuilders.h"

// =============================================================================
// Halfedge::Mesh submesh view tests
// =============================================================================

TEST(SubmeshView_Mesh, DefaultMeshIsNotView)
{
    auto mesh = MakeTwoTriangleSquare();
    EXPECT_FALSE(mesh.IsSubmeshView());
    EXPECT_EQ(mesh.VertexRange().Offset, 0u);
    EXPECT_EQ(mesh.VertexRange().Size, 0u);
}

TEST(SubmeshView_Mesh, CreateViewSharesStorage)
{
    auto mesh = MakeTwoTriangleSquare();
    ASSERT_EQ(mesh.VerticesSize(), 4u);
    ASSERT_EQ(mesh.EdgesSize(), 5u);
    ASSERT_EQ(mesh.FacesSize(), 2u);

    auto view = Geometry::Halfedge::Mesh::CreateView(
        mesh,
        {.Offset = 0, .Size = 2},  // first 2 vertices
        {.Offset = 0, .Size = 3},  // first 3 edges
        {.Offset = 0, .Size = 1}   // first face
    );

    EXPECT_TRUE(view.IsSubmeshView());
    EXPECT_EQ(view.VerticesSize(), 2u);
    EXPECT_EQ(view.EdgesSize(), 3u);
    EXPECT_EQ(view.HalfedgesSize(), 6u);
    EXPECT_EQ(view.FacesSize(), 1u);
}

TEST(SubmeshView_Mesh, ViewPositionsReturnSubspan)
{
    auto mesh = MakeTwoTriangleSquare();
    // Vertices: v0=(0,0,0) v1=(1,0,0) v2=(1,1,0) v3=(0,1,0)

    auto view = Geometry::Halfedge::Mesh::CreateView(
        mesh,
        {.Offset = 1, .Size = 2},  // v1 and v2
        {.Offset = 0, .Size = 0},  // full edges (Size=0 → clamp to full)
        {.Offset = 0, .Size = 0}   // full faces
    );

    auto positions = view.Positions();
    ASSERT_EQ(positions.size(), 2u);
    EXPECT_FLOAT_EQ(positions[0].x, 1.0f);  // v1
    EXPECT_FLOAT_EQ(positions[0].y, 0.0f);
    EXPECT_FLOAT_EQ(positions[1].x, 1.0f);  // v2
    EXPECT_FLOAT_EQ(positions[1].y, 1.0f);
}

TEST(SubmeshView_Mesh, ViewRangeAccessors)
{
    auto mesh = MakeTwoTriangleSquare();

    auto view = Geometry::Halfedge::Mesh::CreateView(
        mesh,
        {.Offset = 1, .Size = 3},
        {.Offset = 2, .Size = 2},
        {.Offset = 0, .Size = 1}
    );

    EXPECT_EQ(view.VertexRange().Offset, 1u);
    EXPECT_EQ(view.VertexRange().Size, 3u);
    EXPECT_EQ(view.EdgeRange().Offset, 2u);
    EXPECT_EQ(view.EdgeRange().Size, 2u);
    EXPECT_EQ(view.FaceRange().Offset, 0u);
    EXPECT_EQ(view.FaceRange().Size, 1u);
}

TEST(SubmeshView_Mesh, ViewClampsToSourceExtent)
{
    auto mesh = MakeSingleTriangle();
    ASSERT_EQ(mesh.VerticesSize(), 3u);

    // Request more than available — should clamp
    auto view = Geometry::Halfedge::Mesh::CreateView(
        mesh,
        {.Offset = 1, .Size = 100},  // overflows
        {.Offset = 0, .Size = 100},
        {.Offset = 0, .Size = 100}
    );

    EXPECT_TRUE(view.IsSubmeshView());
    EXPECT_EQ(view.VerticesSize(), 2u);   // clamped: 3 - 1 = 2
    EXPECT_EQ(view.Positions().size(), 2u);
}

TEST(SubmeshView_Mesh, ViewWithOffsetBeyondSizeGivesEmpty)
{
    auto mesh = MakeSingleTriangle();
    ASSERT_EQ(mesh.VerticesSize(), 3u);

    auto view = Geometry::Halfedge::Mesh::CreateView(
        mesh,
        {.Offset = 100, .Size = 5},
        {.Offset = 100, .Size = 5},
        {.Offset = 100, .Size = 5}
    );

    EXPECT_TRUE(view.IsSubmeshView());
    EXPECT_EQ(view.VerticesSize(), 0u);
    EXPECT_EQ(view.EdgesSize(), 0u);
    EXPECT_EQ(view.FacesSize(), 0u);
}

TEST(SubmeshView_Mesh, ViewSharesPositionDataWithSource)
{
    auto mesh = MakeSingleTriangle();

    auto view = Geometry::Halfedge::Mesh::CreateView(
        mesh,
        {.Offset = 0, .Size = 3},
        {.Offset = 0, .Size = 3},
        {.Offset = 0, .Size = 1}
    );

    // Modify through source, observe through view
    mesh.Position(Geometry::VertexHandle{0}) = glm::vec3(42.0f, 0.0f, 0.0f);
    EXPECT_FLOAT_EQ(view.Positions()[0].x, 42.0f);
}

// =============================================================================
// Graph submesh view tests
// =============================================================================

TEST(SubmeshView_Graph, DefaultGraphIsNotView)
{
    Geometry::Graph::Graph graph;
    EXPECT_FALSE(graph.IsSubmeshView());
}

TEST(SubmeshView_Graph, CreateViewRestrictsSize)
{
    Geometry::Graph::Graph graph;
    auto v0 = graph.AddVertex({0.0f, 0.0f, 0.0f});
    auto v1 = graph.AddVertex({1.0f, 0.0f, 0.0f});
    auto v2 = graph.AddVertex({0.0f, 1.0f, 0.0f});
    (void)graph.AddEdge(v0, v1);
    (void)graph.AddEdge(v1, v2);

    ASSERT_EQ(graph.VerticesSize(), 3u);
    ASSERT_EQ(graph.EdgesSize(), 2u);

    auto view = Geometry::Graph::Graph::CreateView(
        graph,
        {.Offset = 0, .Size = 2},
        {.Offset = 0, .Size = 1}
    );

    EXPECT_TRUE(view.IsSubmeshView());
    EXPECT_EQ(view.VerticesSize(), 2u);
    EXPECT_EQ(view.EdgesSize(), 1u);
    EXPECT_EQ(view.HalfedgesSize(), 2u);
}

TEST(SubmeshView_Graph, ViewRangeAccessors)
{
    Geometry::Graph::Graph graph;
    auto v0 = graph.AddVertex({0.0f, 0.0f, 0.0f});
    auto v1 = graph.AddVertex({1.0f, 0.0f, 0.0f});
    (void)graph.AddEdge(v0, v1);

    auto view = Geometry::Graph::Graph::CreateView(
        graph,
        {.Offset = 0, .Size = 1},
        {.Offset = 0, .Size = 1}
    );

    EXPECT_EQ(view.VertexRange().Offset, 0u);
    EXPECT_EQ(view.VertexRange().Size, 1u);
    EXPECT_EQ(view.EdgeRange().Offset, 0u);
    EXPECT_EQ(view.EdgeRange().Size, 1u);
}

// =============================================================================
// PointCloud::Cloud submesh view tests
// =============================================================================

TEST(SubmeshView_Cloud, DefaultCloudIsNotView)
{
    Geometry::PointCloud::Cloud cloud;
    EXPECT_FALSE(cloud.IsSubmeshView());
}

TEST(SubmeshView_Cloud, CreateViewRestrictsSize)
{
    Geometry::PointCloud::Cloud cloud;
    cloud.AddPoint({0.0f, 0.0f, 0.0f});
    cloud.AddPoint({1.0f, 0.0f, 0.0f});
    cloud.AddPoint({2.0f, 0.0f, 0.0f});
    cloud.AddPoint({3.0f, 0.0f, 0.0f});

    ASSERT_EQ(cloud.VerticesSize(), 4u);

    auto view = Geometry::PointCloud::Cloud::CreateView(
        cloud,
        {.Offset = 1, .Size = 2}
    );

    EXPECT_TRUE(view.IsSubmeshView());
    EXPECT_EQ(view.VerticesSize(), 2u);
    EXPECT_FALSE(view.IsEmpty());
}

TEST(SubmeshView_Cloud, ViewPositionsReturnSubspan)
{
    Geometry::PointCloud::Cloud cloud;
    cloud.AddPoint({0.0f, 0.0f, 0.0f});
    cloud.AddPoint({1.0f, 0.0f, 0.0f});
    cloud.AddPoint({2.0f, 0.0f, 0.0f});
    cloud.AddPoint({3.0f, 0.0f, 0.0f});

    auto view = Geometry::PointCloud::Cloud::CreateView(
        cloud,
        {.Offset = 1, .Size = 2}
    );

    auto positions = view.Positions();
    ASSERT_EQ(positions.size(), 2u);
    EXPECT_FLOAT_EQ(positions[0].x, 1.0f);
    EXPECT_FLOAT_EQ(positions[1].x, 2.0f);
}

TEST(SubmeshView_Cloud, ViewNormalsReturnSubspan)
{
    Geometry::PointCloud::Cloud cloud;
    cloud.EnableNormals();
    cloud.AddPoint({0.0f, 0.0f, 0.0f});
    cloud.AddPoint({1.0f, 0.0f, 0.0f});
    cloud.AddPoint({2.0f, 0.0f, 0.0f});
    cloud.Normal(Geometry::VertexHandle{0}) = {1.0f, 0.0f, 0.0f};
    cloud.Normal(Geometry::VertexHandle{1}) = {0.0f, 1.0f, 0.0f};
    cloud.Normal(Geometry::VertexHandle{2}) = {0.0f, 0.0f, 1.0f};

    auto view = Geometry::PointCloud::Cloud::CreateView(
        cloud,
        {.Offset = 1, .Size = 2}
    );

    auto normals = view.Normals();
    ASSERT_EQ(normals.size(), 2u);
    EXPECT_FLOAT_EQ(normals[0].y, 1.0f);  // normal of point 1
    EXPECT_FLOAT_EQ(normals[1].z, 1.0f);  // normal of point 2
}

TEST(SubmeshView_Cloud, ViewSharesDataWithSource)
{
    Geometry::PointCloud::Cloud cloud;
    cloud.AddPoint({0.0f, 0.0f, 0.0f});
    cloud.AddPoint({1.0f, 0.0f, 0.0f});
    cloud.AddPoint({2.0f, 0.0f, 0.0f});

    auto view = Geometry::PointCloud::Cloud::CreateView(
        cloud,
        {.Offset = 0, .Size = 3}
    );

    // Modify source, observe through view
    cloud.Position(Geometry::VertexHandle{1}) = glm::vec3(99.0f, 0.0f, 0.0f);
    EXPECT_FLOAT_EQ(view.Positions()[1].x, 99.0f);
}

TEST(SubmeshView_Cloud, ViewClampsOverflow)
{
    Geometry::PointCloud::Cloud cloud;
    cloud.AddPoint({0.0f, 0.0f, 0.0f});
    cloud.AddPoint({1.0f, 0.0f, 0.0f});

    auto view = Geometry::PointCloud::Cloud::CreateView(
        cloud,
        {.Offset = 1, .Size = 100}
    );

    EXPECT_EQ(view.VerticesSize(), 1u);
    EXPECT_EQ(view.Positions().size(), 1u);
}

TEST(SubmeshView_Cloud, ViewRangeAccessor)
{
    Geometry::PointCloud::Cloud cloud;
    cloud.AddPoint({0.0f, 0.0f, 0.0f});
    cloud.AddPoint({1.0f, 0.0f, 0.0f});

    auto view = Geometry::PointCloud::Cloud::CreateView(
        cloud,
        {.Offset = 0, .Size = 1}
    );

    EXPECT_EQ(view.VertexRange().Offset, 0u);
    EXPECT_EQ(view.VertexRange().Size, 1u);
}

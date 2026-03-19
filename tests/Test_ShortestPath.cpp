// tests/Test_ShortestPath.cpp — Dijkstra shortest-path tests for meshes and graphs.
// Covers: mesh/graph parity, path reconstruction, and empty-set semantics.

#include <gtest/gtest.h>

#include <vector>

#include <glm/glm.hpp>

import Geometry;

#include "TestMeshBuilders.h"

TEST(ShortestPath, MeshTriangleChoosesDirectEdge)
{
    auto mesh = MakeSingleTriangle();
    std::vector<Geometry::VertexHandle> sources{Geometry::VertexHandle{0}};
    std::vector<Geometry::VertexHandle> targets{Geometry::VertexHandle{2}};

    auto result = Geometry::ShortestPath::Dijkstra(mesh, sources, targets);
    ASSERT_TRUE(result.has_value());

    EXPECT_NEAR(result->Distances[Geometry::VertexHandle{0}], 0.0, 1e-9);
    EXPECT_NEAR(result->Distances[Geometry::VertexHandle{2}], 1.0, 1e-6);
    EXPECT_EQ(result->Predecessors[Geometry::VertexHandle{2}], Geometry::VertexHandle{0});

    auto path = Geometry::ShortestPath::ExtractPathGraph(mesh, *result, sources, targets);
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(path->VertexCount(), 2u);
    EXPECT_EQ(path->EdgeCount(), 1u);
}

TEST(ShortestPath, ReturnsNulloptWhenBothSetsEmpty)
{
    auto mesh = MakeSingleTriangle();
    std::vector<Geometry::VertexHandle> empty;

    auto result = Geometry::ShortestPath::Dijkstra(mesh, empty, empty);
    EXPECT_FALSE(result.has_value());
}

TEST(ShortestPath, ReverseTreeWhenStartsEmpty)
{
    auto mesh = MakeSingleTriangle();
    std::vector<Geometry::VertexHandle> empty;
    std::vector<Geometry::VertexHandle> targets{Geometry::VertexHandle{2}};

    auto result = Geometry::ShortestPath::Dijkstra(mesh, empty, targets);
    ASSERT_TRUE(result.has_value());

    EXPECT_NEAR(result->Distances[Geometry::VertexHandle{2}], 0.0, 1e-9);
    EXPECT_NEAR(result->Distances[Geometry::VertexHandle{0}], 1.0, 1e-6);
    EXPECT_NEAR(result->Distances[Geometry::VertexHandle{1}], 1.0, 1e-6);
    EXPECT_EQ(result->Predecessors[Geometry::VertexHandle{0}], Geometry::VertexHandle{2});
    EXPECT_EQ(result->Predecessors[Geometry::VertexHandle{1}], Geometry::VertexHandle{2});

    auto path = Geometry::ShortestPath::ExtractPathGraph(mesh, *result, empty, targets);
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(path->VertexCount(), 3u);
    EXPECT_EQ(path->EdgeCount(), 2u);
}

TEST(ShortestPath, ForwardTreeWhenTargetsEmpty)
{
    auto mesh = MakeSingleTriangle();
    std::vector<Geometry::VertexHandle> sources{Geometry::VertexHandle{0}};
    std::vector<Geometry::VertexHandle> empty;

    auto result = Geometry::ShortestPath::Dijkstra(mesh, sources, empty);
    ASSERT_TRUE(result.has_value());

    EXPECT_NEAR(result->Distances[Geometry::VertexHandle{0}], 0.0, 1e-9);
    EXPECT_NEAR(result->Distances[Geometry::VertexHandle{1}], 1.0, 1e-6);
    EXPECT_NEAR(result->Distances[Geometry::VertexHandle{2}], 1.0, 1e-6);
    EXPECT_EQ(result->Predecessors[Geometry::VertexHandle{1}], Geometry::VertexHandle{0});
    EXPECT_EQ(result->Predecessors[Geometry::VertexHandle{2}], Geometry::VertexHandle{0});

    auto path = Geometry::ShortestPath::ExtractPathGraph(mesh, *result, sources, empty);
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(path->VertexCount(), 3u);
    EXPECT_EQ(path->EdgeCount(), 2u);
}

TEST(ShortestPath, GraphChainMatchesMeshStyleContract)
{
    Geometry::Graph::Graph graph;
    auto v0 = graph.AddVertex(glm::vec3{0.0f, 0.0f, 0.0f});
    auto v1 = graph.AddVertex(glm::vec3{1.0f, 0.0f, 0.0f});
    auto v2 = graph.AddVertex(glm::vec3{2.0f, 0.0f, 0.0f});

    ASSERT_TRUE(graph.AddEdge(v0, v1).has_value());
    ASSERT_TRUE(graph.AddEdge(v1, v2).has_value());

    std::vector<Geometry::VertexHandle> sources{v0};
    std::vector<Geometry::VertexHandle> targets{v2};

    auto result = Geometry::ShortestPath::Dijkstra(graph, sources, targets);
    ASSERT_TRUE(result.has_value());

    EXPECT_NEAR(result->Distances[v2], 2.0, 1e-6);
    EXPECT_EQ(result->Predecessors[v2], v1);
    EXPECT_EQ(result->Predecessors[v1], v0);

    auto path = Geometry::ShortestPath::ExtractPathGraph(graph, *result, sources, targets);
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(path->VertexCount(), 3u);
    EXPECT_EQ(path->EdgeCount(), 2u);
}

TEST(ShortestPath, MultiGoalExtractionProducesNetwork)
{
    Geometry::Graph::Graph graph;
    auto v0 = graph.AddVertex(glm::vec3{0.0f, 0.0f, 0.0f});
    auto v1 = graph.AddVertex(glm::vec3{1.0f, 0.0f, 0.0f});
    auto v2 = graph.AddVertex(glm::vec3{2.0f, 0.0f, 0.0f});

    ASSERT_TRUE(graph.AddEdge(v0, v1).has_value());
    ASSERT_TRUE(graph.AddEdge(v1, v2).has_value());

    std::vector<Geometry::VertexHandle> sources{v1};
    std::vector<Geometry::VertexHandle> targets{v0, v2};

    auto result = Geometry::ShortestPath::Dijkstra(graph, sources, targets);
    ASSERT_TRUE(result.has_value());

    auto path = Geometry::ShortestPath::ExtractPathGraph(graph, *result, sources, targets);
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(path->VertexCount(), 3u);
    EXPECT_EQ(path->EdgeCount(), 2u);
}

TEST(ShortestPath, ReturnsNulloptForEmptyMesh)
{
    Geometry::Halfedge::Mesh mesh;
    std::vector<Geometry::VertexHandle> sources{Geometry::VertexHandle{0}};
    std::vector<Geometry::VertexHandle> targets{Geometry::VertexHandle{1}};

    auto result = Geometry::ShortestPath::Dijkstra(mesh, sources, targets);
    EXPECT_FALSE(result.has_value());
}


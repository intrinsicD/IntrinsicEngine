#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

import Geometry.Graph;
import Geometry.Graph.Vertex.Normals;
import Geometry.Properties;

namespace
{
    namespace GraphNormals = Geometry::Graph::VertexNormals;

    void ExpectVecNear(const glm::vec3 actual, const glm::vec3 expected, const float epsilon = 1.0e-5f)
    {
        EXPECT_NEAR(actual.x, expected.x, epsilon);
        EXPECT_NEAR(actual.y, expected.y, epsilon);
        EXPECT_NEAR(actual.z, expected.z, epsilon);
    }

    [[nodiscard]] Geometry::Graph::Graph MakePlanarCycle()
    {
        Geometry::Graph::Graph graph;
        const auto v0 = graph.AddVertex(glm::vec3{0.0f, 0.0f, 0.0f});
        const auto v1 = graph.AddVertex(glm::vec3{1.0f, 0.0f, 0.0f});
        const auto v2 = graph.AddVertex(glm::vec3{1.0f, 1.0f, 0.0f});
        const auto v3 = graph.AddVertex(glm::vec3{0.0f, 1.0f, 0.0f});
        EXPECT_TRUE(graph.AddEdge(v0, v1).has_value());
        EXPECT_TRUE(graph.AddEdge(v1, v2).has_value());
        EXPECT_TRUE(graph.AddEdge(v2, v3).has_value());
        EXPECT_TRUE(graph.AddEdge(v3, v0).has_value());
        return graph;
    }
}

TEST(GraphVertexNormals, DebugNamesAreStable)
{
    EXPECT_EQ(GraphNormals::DebugName(GraphNormals::RecomputeStatus::Success), "Success");
    EXPECT_EQ(GraphNormals::DebugName(GraphNormals::RecomputeStatus::EmptyGraph), "EmptyGraph");
    EXPECT_EQ(GraphNormals::DebugName(GraphNormals::RecomputeStatus::InvalidPositionProperty),
              "InvalidPositionProperty");
}

TEST(GraphVertexNormals, PlanarCycleWritesCanonicalNormals)
{
    auto graph = MakePlanarCycle();

    GraphNormals::Params params;
    params.OutputProperty = "v:normal";
    const auto result = GraphNormals::Recompute(graph, params);

    ASSERT_EQ(result.Status, GraphNormals::RecomputeStatus::Success);
    ASSERT_TRUE(result.Normals.IsValid());
    EXPECT_EQ(result.Diagnostics.VertexSlotCount, 4u);
    EXPECT_EQ(result.Diagnostics.EdgeSlotCount, 4u);
    EXPECT_EQ(result.Diagnostics.WrittenCount, 4u);
    EXPECT_EQ(result.Diagnostics.ValidNormalVertexCount, 4u);
    EXPECT_EQ(result.Diagnostics.FallbackVertexCount, 0u);

    for (std::size_t i = 0; i < graph.VerticesSize(); ++i)
    {
        const auto vertex = Geometry::VertexHandle{static_cast<Geometry::PropertyIndex>(i)};
        ExpectVecNear(result.Normals[vertex], glm::vec3{0.0f, 0.0f, 1.0f});
        EXPECT_NEAR(glm::length(result.Normals[vertex]), 1.0f, 1.0e-5f);
    }
}

TEST(GraphVertexNormals, LineGraphReportsDegreeOneAndCollinearFallbacks)
{
    Geometry::Graph::Graph graph;
    const auto v0 = graph.AddVertex(glm::vec3{0.0f, 0.0f, 0.0f});
    const auto v1 = graph.AddVertex(glm::vec3{1.0f, 0.0f, 0.0f});
    const auto v2 = graph.AddVertex(glm::vec3{2.0f, 0.0f, 0.0f});
    ASSERT_TRUE(graph.AddEdge(v0, v1).has_value());
    ASSERT_TRUE(graph.AddEdge(v1, v2).has_value());

    GraphNormals::Params params;
    params.FallbackNormal = glm::vec3{0.0f, 1.0f, 0.0f};
    const auto result = GraphNormals::Recompute(graph, params);

    ASSERT_EQ(result.Status, GraphNormals::RecomputeStatus::Success);
    EXPECT_EQ(result.Diagnostics.DegreeOneVertexCount, 2u);
    EXPECT_EQ(result.Diagnostics.CollinearNeighborhoodCount, 1u);
    EXPECT_EQ(result.Diagnostics.FallbackVertexCount, 3u);
    ASSERT_TRUE(result.Normals.IsValid());
    for (std::size_t i = 0; i < graph.VerticesSize(); ++i)
    {
        ExpectVecNear(result.Normals[Geometry::VertexHandle{static_cast<Geometry::PropertyIndex>(i)}],
                      glm::vec3{0.0f, 1.0f, 0.0f});
    }
}

TEST(GraphVertexNormals, IsolatedDuplicateAndNonFiniteInputsStayFinite)
{
    Geometry::Graph::Graph graph;
    const auto isolated = graph.AddVertex(glm::vec3{5.0f, 0.0f, 0.0f});
    const auto v0 = graph.AddVertex(glm::vec3{0.0f, 0.0f, 0.0f});
    const auto v1 = graph.AddVertex(glm::vec3{0.0f, 0.0f, 0.0f});
    const auto v2 = graph.AddVertex(glm::vec3{0.0f, 1.0f, 0.0f});
    const auto bad = graph.AddVertex(glm::vec3{std::numeric_limits<float>::infinity(), 0.0f, 0.0f});
    ASSERT_TRUE(isolated.IsValid());
    ASSERT_TRUE(graph.AddEdge(v0, v1).has_value());
    ASSERT_TRUE(graph.AddEdge(v0, v2).has_value());
    ASSERT_TRUE(graph.AddEdge(v1, v2).has_value());
    ASSERT_TRUE(graph.AddEdge(v2, bad).has_value());

    const auto result = GraphNormals::Recompute(graph);

    ASSERT_EQ(result.Status, GraphNormals::RecomputeStatus::Success);
    EXPECT_GE(result.Diagnostics.IsolatedVertexCount, 1u);
    EXPECT_GE(result.Diagnostics.DuplicatePositionCount, 1u);
    EXPECT_GE(result.Diagnostics.NonFinitePositionCount, 1u);
    ASSERT_TRUE(result.Normals.IsValid());
    for (const glm::vec3 normal : result.Normals.Vector())
    {
        EXPECT_TRUE(std::isfinite(normal.x));
        EXPECT_TRUE(std::isfinite(normal.y));
        EXPECT_TRUE(std::isfinite(normal.z));
    }
}

TEST(GraphVertexNormals, RawPropertySetOverloadReportsInvalidEdges)
{
    Geometry::Vertices vertices;
    vertices.Resize(2u);
    auto positions = vertices.GetOrAdd<glm::vec3>("v:point", glm::vec3{0.0f});
    positions[0] = glm::vec3{0.0f, 0.0f, 0.0f};
    positions[1] = glm::vec3{1.0f, 0.0f, 0.0f};

    Geometry::Halfedges halfedges;
    halfedges.Resize(2u);
    auto hConn = halfedges.GetOrAdd<Geometry::Graph::HalfedgeConnectivity>("h:connectivity", {});
    hConn[0u].Vertex = Geometry::VertexHandle{1u};
    hConn[1u].Vertex = Geometry::VertexHandle{42u};

    const auto result = GraphNormals::Recompute(vertices,
                                                Geometry::ConstPropertySet(vertices).Get<glm::vec3>("v:point"),
                                                Geometry::ConstPropertySet(halfedges)
                                                    .Get<Geometry::Graph::HalfedgeConnectivity>("h:connectivity"),
                                                1u);

    ASSERT_EQ(result.Status, GraphNormals::RecomputeStatus::Success);
    EXPECT_EQ(result.Diagnostics.InvalidEdgeCount, 1u);
    EXPECT_EQ(result.Diagnostics.IsolatedVertexCount, 2u);
    EXPECT_EQ(result.Diagnostics.FallbackVertexCount, 2u);
    ASSERT_TRUE(result.Normals.IsValid());
}

TEST(GraphVertexNormals, OutputPropertyTypeConflictFailsClosed)
{
    auto graph = MakePlanarCycle();
    auto wrongType = graph.VertexProperties().GetOrAdd<float>("v:normal", 0.0f);
    ASSERT_TRUE(wrongType.IsValid());

    const auto result = GraphNormals::Recompute(graph);

    EXPECT_EQ(result.Status, GraphNormals::RecomputeStatus::PropertyTypeConflict);
    EXPECT_FALSE(result.Normals.IsValid());
    EXPECT_TRUE(graph.VertexProperties().Get<float>("v:normal").IsValid());
}

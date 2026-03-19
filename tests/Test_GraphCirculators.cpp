#include <gtest/gtest.h>

#include <concepts>
#include <vector>

#include <glm/glm.hpp>

import Geometry;

namespace
{
    [[nodiscard]] Geometry::Graph::Graph MakeChainGraph()
    {
        Geometry::Graph::Graph graph;
        const auto v0 = graph.AddVertex(glm::vec3(0.0F, 0.0F, 0.0F));
        const auto v1 = graph.AddVertex(glm::vec3(1.0F, 0.0F, 0.0F));
        const auto v2 = graph.AddVertex(glm::vec3(2.0F, 0.0F, 0.0F));
        (void)graph.AddEdge(v0, v1);
        (void)graph.AddEdge(v1, v2);
        return graph;
    }
}

template <class Domain>
concept GraphLike = requires(const Domain& d, Geometry::VertexHandle v, Geometry::HalfedgeHandle h)
{
    { d.HalfedgesAroundVertex(v) };
    { d.ToVertex(h) } -> std::convertible_to<Geometry::VertexHandle>;
};

static_assert(GraphLike<Geometry::Graph::Graph>);
static_assert(GraphLike<Geometry::Halfedge::Mesh>);

TEST(GraphCirculators, HalfedgesAroundVertexTraversesIncidentRing)
{
    const auto graph = MakeChainGraph();

    std::vector<Geometry::HalfedgeHandle> visited;
    for (const Geometry::HalfedgeHandle h : graph.HalfedgesAroundVertex(Geometry::VertexHandle{1}))
    {
        visited.push_back(h);
    }

    ASSERT_EQ(visited.size(), 2u);
    EXPECT_EQ(visited[0], Geometry::HalfedgeHandle{1});
    EXPECT_EQ(visited[1], Geometry::HalfedgeHandle{2});
}

TEST(GraphCirculators, BoundaryHalfedgesVisitBoundaryLoopOnce)
{
    const auto graph = MakeChainGraph();
    const Geometry::HalfedgeHandle boundaryStart{1};
    ASSERT_TRUE(graph.IsBoundary(boundaryStart));

    std::vector<Geometry::HalfedgeHandle> visited;
    for (const Geometry::HalfedgeHandle h : graph.BoundaryHalfedges(boundaryStart))
    {
        visited.push_back(h);
    }

    ASSERT_EQ(visited.size(), 4u);
    EXPECT_EQ(visited[0], Geometry::HalfedgeHandle{1});
    EXPECT_EQ(visited[1], Geometry::HalfedgeHandle{0});
    EXPECT_EQ(visited[2], Geometry::HalfedgeHandle{2});
    EXPECT_EQ(visited[3], Geometry::HalfedgeHandle{3});
}

TEST(GraphCirculators, IsolatedVertexProducesEmptyHalfedgeRing)
{
    Geometry::Graph::Graph graph;
    const auto isolated = graph.AddVertex(glm::vec3(4.0F, 5.0F, 6.0F));

    std::size_t count = 0;
    for (const Geometry::HalfedgeHandle h : graph.HalfedgesAroundVertex(isolated))
    {
        (void)h;
        ++count;
    }

    EXPECT_EQ(count, 0u);
    EXPECT_TRUE(graph.IsIsolated(isolated));
}


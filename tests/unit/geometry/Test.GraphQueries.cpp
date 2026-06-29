#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

import Geometry;

namespace
{
    [[nodiscard]] Geometry::Graph::Graph MakeQueryGraph()
    {
        Geometry::Graph::Graph graph;
        const auto v0 = graph.AddVertex({0.0F, 0.0F, 0.0F});
        const auto v1 = graph.AddVertex({2.0F, 0.0F, 0.0F});
        const auto v2 = graph.AddVertex({2.0F, 1.0F, 0.0F});
        const auto v3 = graph.AddVertex({0.0F, 1.0F, 0.0F});
        const auto v4 = graph.AddVertex({4.0F, 0.0F, 0.0F});

        (void)graph.AddEdge(v0, v1);
        (void)graph.AddEdge(v1, v2);
        (void)graph.AddEdge(v2, v3);
        (void)graph.AddEdge(v3, v0);
        (void)graph.AddEdge(v1, v4);
        return graph;
    }

    [[nodiscard]] Geometry::Graph::ClosestEdgeQueryResult BruteClosestEdge(
        const Geometry::Graph::Graph& graph,
        const glm::vec3 point,
        const std::vector<Geometry::EdgeHandle>& edges)
    {
        Geometry::Graph::ClosestEdgeQueryResult best{};
        best.Status = Geometry::Graph::EdgeQueryStatus::NoCandidates;
        for (const Geometry::EdgeHandle edge : edges)
        {
            const auto [start, end] = graph.EdgeVertices(edge);
            const auto segment = Geometry::ClosestPointSegment(point, graph.VertexPosition(start), graph.VertexPosition(end));
            Geometry::Graph::ClosestEdgeQueryResult candidate{
                Geometry::Graph::EdgeQueryStatus::Success,
                edge,
                segment.ClosestPoint,
                segment.DistanceSq,
                segment.SegmentT};

            if (best.Status != Geometry::Graph::EdgeQueryStatus::Success
                || candidate.SquaredDistance < best.SquaredDistance
                || (candidate.SquaredDistance == best.SquaredDistance && candidate.Edge.Index < best.Edge.Index))
            {
                best = candidate;
            }
        }
        return best;
    }

    [[nodiscard]] std::vector<Geometry::EdgeHandle> LiveEdges(const Geometry::Graph::Graph& graph)
    {
        std::vector<Geometry::EdgeHandle> edges;
        for (const Geometry::EdgeHandle edge : graph.LiveEdges()) edges.push_back(edge);
        return edges;
    }
}

TEST(GraphQueries, EnsureEdgeLengthsCachesCanonicalProperty)
{
    auto graph = MakeQueryGraph();

    const auto result = Geometry::Graph::EnsureEdgeLengths(graph);
    ASSERT_EQ(result.Status, Geometry::Graph::EdgeLengthStatus::Success);
    EXPECT_EQ(result.FilledCount, graph.EdgeCount());

    const auto lengths = graph.EdgeProperties().Get<float>("e:length");
    ASSERT_TRUE(lengths.IsValid());

    for (const Geometry::EdgeHandle edge : graph.LiveEdges())
    {
        const auto [start, end] = graph.EdgeVertices(edge);
        const float expected = glm::length(graph.VertexPosition(end) - graph.VertexPosition(start));
        EXPECT_NEAR(lengths[edge.Index], expected, 1.0e-6F);
    }

    const auto second = Geometry::Graph::EnsureEdgeLengths(graph);
    EXPECT_EQ(second.Status, Geometry::Graph::EdgeLengthStatus::Success);
    EXPECT_EQ(second.FilledCount, result.FilledCount);
}

TEST(GraphQueries, ClosestKAndRadiusQueriesMatchBruteForce)
{
    const auto graph = MakeQueryGraph();
    const glm::vec3 query{1.75F, 0.8F, 0.0F};
    const auto allEdges = LiveEdges(graph);

    const auto bruteClosest = BruteClosestEdge(graph, query, allEdges);
    const auto closest = Geometry::Graph::ClosestEdge(graph, query);
    ASSERT_EQ(closest.Status, Geometry::Graph::EdgeQueryStatus::Success);
    EXPECT_EQ(closest.Edge.Index, bruteClosest.Edge.Index);
    EXPECT_NEAR(closest.SquaredDistance, bruteClosest.SquaredDistance, 1.0e-6F);
    EXPECT_NEAR(glm::length(closest.ClosestPoint - bruteClosest.ClosestPoint), 0.0F, 1.0e-6F);

    std::vector<Geometry::Graph::ClosestEdgeQueryResult> bruteAll;
    bruteAll.reserve(allEdges.size());
    for (const auto edge : allEdges)
    {
        bruteAll.push_back(BruteClosestEdge(graph, query, std::vector<Geometry::EdgeHandle>{edge}));
    }
    std::sort(bruteAll.begin(), bruteAll.end(), [](const auto& a, const auto& b)
    {
        if (a.SquaredDistance != b.SquaredDistance) return a.SquaredDistance < b.SquaredDistance;
        return a.Edge.Index < b.Edge.Index;
    });

    const auto kClosest = Geometry::Graph::KClosestEdges(graph, query, 3);
    ASSERT_EQ(kClosest.Status, Geometry::Graph::EdgeQueryStatus::Success);
    ASSERT_EQ(kClosest.Edges.size(), 3u);
    for (std::size_t i = 0; i < kClosest.Edges.size(); ++i)
    {
        EXPECT_EQ(kClosest.Edges[i].Edge.Index, bruteAll[i].Edge.Index);
        EXPECT_NEAR(kClosest.Edges[i].SquaredDistance, bruteAll[i].SquaredDistance, 1.0e-6F);
    }

    const auto radiusSet = Geometry::Graph::EdgesWithinRadius(graph, query, 0.85F);
    ASSERT_EQ(radiusSet.Status, Geometry::Graph::EdgeQueryStatus::Success);
    for (const auto& edge : radiusSet.Edges)
    {
        EXPECT_LE(edge.SquaredDistance, 0.85F * 0.85F + 1.0e-6F);
    }
}

TEST(GraphQueries, ClosestEdgeWithinOneRingOnlySearchesIncidentEdges)
{
    const auto graph = MakeQueryGraph();
    const Geometry::VertexHandle seed{1u};
    const glm::vec3 query{3.7F, 0.1F, 0.0F};

    std::vector<Geometry::EdgeHandle> incident;
    for (const Geometry::HalfedgeHandle halfedge : graph.HalfedgesAroundVertex(seed))
    {
        incident.push_back(graph.Edge(halfedge));
    }

    const auto brute = BruteClosestEdge(graph, query, incident);
    const auto result = Geometry::Graph::ClosestEdgeWithinOneRing(graph, seed, query);
    ASSERT_EQ(result.Status, Geometry::Graph::EdgeQueryStatus::Success);
    EXPECT_EQ(result.Edge.Index, brute.Edge.Index);
    EXPECT_NEAR(result.SquaredDistance, brute.SquaredDistance, 1.0e-6F);
}

TEST(GraphQueries, GaussianNoiseIsDeterministicAndIdentityAtZeroScale)
{
    Geometry::Graph::Graph original;
    for (std::uint32_t i = 0; i < 128; ++i)
    {
        original.AddVertex({static_cast<float>(i), static_cast<float>(i % 7u), static_cast<float>(i % 5u)});
    }

    auto identity = original;
    const auto identityResult = Geometry::Graph::ApplyGaussianNoise(identity, {.StdDevFraction = 0.0F, .Seed = 7});
    ASSERT_EQ(identityResult.Status, Geometry::Graph::GaussianNoiseStatus::Success);
    EXPECT_EQ(identityResult.DisplacedCount, 0u);
    for (const auto vertex : original.LiveVertices())
    {
        EXPECT_EQ(identity.VertexPosition(vertex), original.VertexPosition(vertex));
    }

    auto a = original;
    auto b = original;
    auto c = original;
    const auto noiseA = Geometry::Graph::ApplyGaussianNoise(a, {.StdDevFraction = 0.01F, .Seed = 1234});
    const auto noiseB = Geometry::Graph::ApplyGaussianNoise(b, {.StdDevFraction = 0.01F, .Seed = 1234});
    const auto noiseC = Geometry::Graph::ApplyGaussianNoise(c, {.StdDevFraction = 0.01F, .Seed = 5678});
    ASSERT_EQ(noiseA.Status, Geometry::Graph::GaussianNoiseStatus::Success);
    ASSERT_EQ(noiseB.Status, Geometry::Graph::GaussianNoiseStatus::Success);
    ASSERT_EQ(noiseC.Status, Geometry::Graph::GaussianNoiseStatus::Success);
    EXPECT_FLOAT_EQ(noiseA.Scale, noiseB.Scale);

    bool differentSeedMovedDifferently = false;
    for (const auto vertex : original.LiveVertices())
    {
        EXPECT_EQ(a.VertexPosition(vertex), b.VertexPosition(vertex));
        if (a.VertexPosition(vertex) != c.VertexPosition(vertex)) differentSeedMovedDifferently = true;
    }
    EXPECT_TRUE(differentSeedMovedDifferently);
}

TEST(GraphQueries, DegenerateInputsFailClosed)
{
    Geometry::Graph::Graph empty;
    EXPECT_EQ(Geometry::Graph::EnsureEdgeLengths(empty).Status, Geometry::Graph::EdgeLengthStatus::EmptyGraph);
    EXPECT_EQ(Geometry::Graph::ClosestEdge(empty, {0.0F, 0.0F, 0.0F}).Status, Geometry::Graph::EdgeQueryStatus::EmptyGraph);
    EXPECT_EQ(Geometry::Graph::KClosestEdges(empty, {0.0F, 0.0F, 0.0F}, 0).Status,
        Geometry::Graph::EdgeQueryStatus::InvalidParameters);

    Geometry::Graph::Graph degenerate;
    const auto v0 = degenerate.AddVertex({1.0F, 0.0F, 0.0F});
    const auto v1 = degenerate.AddVertex({1.0F, 0.0F, 0.0F});
    ASSERT_TRUE(degenerate.AddEdge(v0, v1).has_value());
    EXPECT_EQ(Geometry::Graph::EnsureEdgeLengths(degenerate).Status, Geometry::Graph::EdgeLengthStatus::ZeroLengthEdge);
    EXPECT_EQ(Geometry::Graph::ClosestEdge(degenerate, {1.0F, 1.0F, 0.0F}).Status,
        Geometry::Graph::EdgeQueryStatus::ZeroLengthEdge);
}

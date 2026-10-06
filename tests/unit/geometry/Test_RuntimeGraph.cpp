#include <gtest/gtest.h>

#include <concepts>
#include <vector>

#include <glm/glm.hpp>

import Geometry.EPA;
import Geometry.Properties;
import Geometry.Circulators;
import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.Builder;
import Geometry.Graph;
import Geometry.MeshSoup;

TEST(RuntimeGraph, AddEdge_FindEdge)
{
    Geometry::Graph::Graph g;

    auto v0 = g.AddVertex({0.0f, 0.0f, 0.0f});
    auto v1 = g.AddVertex({1.0f, 0.0f, 0.0f});
    auto v2 = g.AddVertex({0.0f, 1.0f, 0.0f});

    ASSERT_TRUE(v0.IsValid());
    ASSERT_TRUE(v1.IsValid());
    ASSERT_TRUE(v2.IsValid());

    auto e01 = g.AddEdge(v0, v1);
    ASSERT_TRUE(e01.has_value());

    // Duplicate should be rejected (both orientations).
    EXPECT_FALSE(g.AddEdge(v0, v1).has_value());
    EXPECT_FALSE(g.AddEdge(v1, v0).has_value());

    auto he = g.FindHalfedge(v0, v1);
    ASSERT_TRUE(he.has_value());
    EXPECT_EQ(g.ToVertex(*he), v1);

    auto e = g.FindEdge(v0, v1);
    ASSERT_TRUE(e.has_value());
    EXPECT_EQ(*e, *e01);

    EXPECT_FALSE(g.FindEdge(v1, v2).has_value());
}

TEST(RuntimeGraph, DeleteVertex_ThenGarbageCollect)
{
    Geometry::Graph::Graph g;

    auto v0 = g.AddVertex({0.0f, 0.0f, 0.0f});
    auto v1 = g.AddVertex({1.0f, 0.0f, 0.0f});
    auto v2 = g.AddVertex({0.0f, 1.0f, 0.0f});

    ASSERT_TRUE(g.AddEdge(v0, v1).has_value());
    ASSERT_TRUE(g.AddEdge(v1, v2).has_value());

    EXPECT_EQ(g.VertexCount(), 3u);
    EXPECT_EQ(g.EdgeCount(), 2u);

    g.DeleteVertex(v1);
    EXPECT_TRUE(g.HasGarbage());

    g.GarbageCollection();
    EXPECT_FALSE(g.HasGarbage());

    // v1 is removed, and both incident edges are deleted.
    EXPECT_EQ(g.VertexCount(), 2u);
    EXPECT_EQ(g.EdgeCount(), 0u);
}

TEST(RuntimeGraph, GarbageCollectionRemapsSurvivingConnectivity)
{
    Geometry::Graph::Graph g;

    const auto v0 = g.AddVertex({0.0f, 0.0f, 0.0f});
    const auto v1 = g.AddVertex({1.0f, 0.0f, 0.0f});
    const auto v2 = g.AddVertex({2.0f, 0.0f, 0.0f});
    const auto v3 = g.AddVertex({3.0f, 0.0f, 0.0f});
    ASSERT_TRUE(g.AddEdge(v0, v1).has_value());
    ASSERT_TRUE(g.AddEdge(v2, v3).has_value());

    g.DeleteVertex(v1);
    g.GarbageCollection();

    ASSERT_EQ(g.VertexCount(), 3u);
    ASSERT_EQ(g.EdgeCount(), 1u);
    ASSERT_EQ(g.HalfedgesSize(), 2u);
    EXPECT_FALSE(g.Halfedge(Geometry::VertexHandle{0u}).IsValid());
    for (std::size_t i = 0u; i < g.VertexCount(); ++i)
    {
        const Geometry::VertexHandle vertex{
            static_cast<Geometry::PropertyIndex>(i)};
        const Geometry::HalfedgeHandle representative = g.Halfedge(vertex);
        if (representative.IsValid())
        {
            EXPECT_LT(representative.Index, g.HalfedgesSize());
            EXPECT_EQ(g.FromVertex(representative), vertex);
        }
    }
    for (std::size_t i = 0u; i < g.HalfedgesSize(); ++i)
    {
        const Geometry::HalfedgeHandle halfedge{
            static_cast<Geometry::PropertyIndex>(i)};
        EXPECT_LT(g.ToVertex(halfedge).Index, g.VertexCount());
        EXPECT_LT(g.NextHalfedge(halfedge).Index, g.HalfedgesSize());
        EXPECT_LT(g.PrevHalfedge(halfedge).Index, g.HalfedgesSize());
    }
}

TEST(RuntimeGraph, GarbageCollectionRebuildsSurvivingVertexStar)
{
    Geometry::Graph::Graph g;

    const auto center = g.AddVertex({0.0f, 0.0f, 0.0f});
    const auto removed = g.AddVertex({1.0f, 0.0f, 0.0f});
    const auto survivorA = g.AddVertex({0.0f, 1.0f, 0.0f});
    const auto survivorB = g.AddVertex({0.0f, 0.0f, 1.0f});
    ASSERT_TRUE(g.AddEdge(center, removed).has_value());
    ASSERT_TRUE(g.AddEdge(center, survivorA).has_value());
    ASSERT_TRUE(g.AddEdge(center, survivorB).has_value());

    g.DeleteVertex(removed);
    g.GarbageCollection();

    ASSERT_EQ(g.VertexCount(), 3u);
    ASSERT_EQ(g.EdgeCount(), 2u);
    ASSERT_EQ(g.HalfedgesSize(), 4u);
    for (std::size_t i = 0u; i < g.VertexCount(); ++i)
    {
        const Geometry::VertexHandle vertex{
            static_cast<Geometry::PropertyIndex>(i)};
        const Geometry::HalfedgeHandle representative = g.Halfedge(vertex);
        ASSERT_TRUE(representative.IsValid());
        EXPECT_LT(representative.Index, g.HalfedgesSize());
        EXPECT_EQ(g.FromVertex(representative), vertex);

        std::size_t incidentCount = 0u;
        for (const Geometry::HalfedgeHandle halfedge :
             g.HalfedgesAroundVertex(vertex))
        {
            EXPECT_EQ(g.FromVertex(halfedge), vertex);
            ++incidentCount;
        }
        EXPECT_EQ(incidentCount, i == center.Index ? 2u : 1u);
    }

    for (std::size_t i = 0u; i < g.HalfedgesSize(); ++i)
    {
        const Geometry::HalfedgeHandle halfedge{
            static_cast<Geometry::PropertyIndex>(i)};
        const Geometry::HalfedgeHandle next = g.NextHalfedge(halfedge);
        const Geometry::HalfedgeHandle prev = g.PrevHalfedge(halfedge);
        ASSERT_LT(next.Index, g.HalfedgesSize());
        ASSERT_LT(prev.Index, g.HalfedgesSize());
        EXPECT_EQ(g.PrevHalfedge(next), halfedge);
        EXPECT_EQ(g.NextHalfedge(prev), halfedge);
        EXPECT_EQ(g.FromVertex(next), g.ToVertex(halfedge));
    }
}

// ---------------------------------------------------------------------------
// Graph Circulators (merged from Test_GraphCirculators.cpp)
// ---------------------------------------------------------------------------

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
static_assert(GraphLike<Geometry::HalfedgeMesh::Mesh>);

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

// ---------------------------------------------------------------------------
// Parallel Layers / Kahn layering (merged from Test_RuntimeGraph_ParallelLayers.cpp)
// ---------------------------------------------------------------------------

namespace
{
    [[nodiscard]] std::vector<std::vector<uint32_t>> TopoLayers(uint32_t passCount,
                                                                const std::vector<std::pair<uint32_t, uint32_t>>& edges)
    {
        std::vector<std::vector<uint32_t>> out;
        std::vector<std::vector<uint32_t>> adj(passCount);
        std::vector<uint32_t> indeg(passCount, 0);

        for (auto [u, v] : edges)
        {
            adj[u].push_back(v);
            indeg[v]++;
        }

        std::vector<uint32_t> layer;
        for (uint32_t i = 0; i < passCount; ++i)
            if (indeg[i] == 0) layer.push_back(i);

        uint32_t processed = 0;
        while (!layer.empty())
        {
            out.push_back(layer);
            processed += (uint32_t)layer.size();

            std::vector<uint32_t> next;
            for (uint32_t u : layer)
            {
                for (uint32_t v : adj[u])
                {
                    if (--indeg[v] == 0)
                        next.push_back(v);
                }
            }

            layer = std::move(next);
        }

        EXPECT_EQ(processed, passCount);
        return out;
    }
}

TEST(RuntimeGraph, TopologicalLayersIndependentPasses)
{
    // Pass A (0) and B (1) are independent.
    // Pass C (2) depends on both.
    const auto layers = TopoLayers(/*passCount*/ 3, {{0, 2}, {1, 2}});

    ASSERT_EQ(layers.size(), 2u);
    EXPECT_EQ(layers[0].size(), 2u);
    EXPECT_EQ(layers[1].size(), 1u);
    EXPECT_EQ(layers[1][0], 2u);
}

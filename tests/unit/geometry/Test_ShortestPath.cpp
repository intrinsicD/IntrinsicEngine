// tests/Test_ShortestPath.cpp — Dijkstra shortest-path tests for graph-domain input.
// Covers: mesh-backed graph views, pure graphs, path reconstruction, and empty-set semantics.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cmath>
#include <limits>
#include <optional>
#include <queue>
#include <random>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

import Geometry;

#include "Test_MeshBuilders.h"

// GEOM-012 Slice A: the inline mesh-backed graph borrow has been promoted to
// `Geometry::DomainViews::BorrowMeshAsGraphReadOnly`. ShortestPath only reads
// from the borrowed graph, so the read-only contract applies even on
// face-bearing meshes such as `MakeSingleTriangle`.
using Geometry::DomainViews::BorrowMeshAsGraphReadOnly;

namespace
{
    constexpr double kDistanceTolerance = 1e-9;

    struct ReferenceShortestPathResult
    {
        std::vector<double> Distances;
        std::vector<Geometry::VertexHandle> Predecessors;
        std::size_t SettledVertexCount{0};
        std::size_t RelaxedEdgeCount{0};
        std::size_t QueuePushCount{0};
        std::size_t ReachedGoalCount{0};
        bool Converged{false};
        bool EarlyTerminated{false};
    };

    struct ReferenceFrontNode
    {
        Geometry::VertexHandle Vertex{};
        double Distance{0.0};
    };

    struct ReferenceFrontNodeGreater
    {
        [[nodiscard]] bool operator()(const ReferenceFrontNode& a, const ReferenceFrontNode& b) const noexcept
        {
            if (a.Distance != b.Distance) return a.Distance > b.Distance;
            return a.Vertex.Index > b.Vertex.Index;
        }
    };

    [[nodiscard]] Geometry::VertexHandle V(std::size_t index)
    {
        return Geometry::VertexHandle{static_cast<Geometry::PropertyIndex>(index)};
    }

    [[nodiscard]] bool IsReferenceUsableVertex(const Geometry::Graph::Graph& graph, Geometry::VertexHandle v)
    {
        return v.IsValid() && graph.IsValid(v) && !graph.IsDeleted(v) && !graph.IsIsolated(v);
    }

    [[nodiscard]] double ReferenceEdgeLength(
        const Geometry::Graph::Graph& graph,
        Geometry::VertexHandle from,
        Geometry::VertexHandle to)
    {
        const glm::vec3 delta = graph.VertexPosition(to) - graph.VertexPosition(from);
        const auto lengthSquared = static_cast<double>(glm::dot(delta, delta));
        if (!(lengthSquared >= 0.0) || !std::isfinite(lengthSquared))
        {
            return std::numeric_limits<double>::infinity();
        }
        return std::sqrt(lengthSquared);
    }

    [[nodiscard]] std::optional<ReferenceShortestPathResult> ReferenceDijkstraCommon(
        const Geometry::Graph::Graph& graph,
        const std::vector<Geometry::VertexHandle>& seedVertices,
        const std::vector<Geometry::VertexHandle>& goalVertices,
        const Geometry::ShortestPath::DijkstraParams& params)
    {
        if (graph.VerticesSize() == 0) return std::nullopt;
        if (seedVertices.empty() && goalVertices.empty()) return std::nullopt;

        const std::size_t vertexCount = graph.VerticesSize();
        const double kInfinity = std::numeric_limits<double>::infinity();

        ReferenceShortestPathResult result{};
        result.Distances.assign(vertexCount, kInfinity);
        result.Predecessors.assign(vertexCount, Geometry::VertexHandle{});

        std::vector<std::uint8_t> goalMask(vertexCount, 0);
        std::size_t validGoalCount = 0;
        for (const Geometry::VertexHandle target : goalVertices)
        {
            if (!IsReferenceUsableVertex(graph, target)) continue;
            if (goalMask[target.Index] != 0) continue;
            goalMask[target.Index] = 1;
            ++validGoalCount;
        }

        std::priority_queue<ReferenceFrontNode, std::vector<ReferenceFrontNode>, ReferenceFrontNodeGreater> queue;
        std::size_t validStartCount = 0;
        for (const Geometry::VertexHandle source : seedVertices)
        {
            if (!IsReferenceUsableVertex(graph, source)) continue;
            if (result.Distances[source.Index] > 0.0) result.Distances[source.Index] = 0.0;
            result.Predecessors[source.Index] = Geometry::VertexHandle{};
            queue.push(ReferenceFrontNode{source, 0.0});
            ++result.QueuePushCount;
            ++validStartCount;
        }

        if (validStartCount == 0) return std::nullopt;
        if (!goalVertices.empty() && validGoalCount == 0) return std::nullopt;

        std::vector<std::uint8_t> settled(vertexCount, 0);
        const std::size_t settleBudget = params.MaxSettledVertices > 0 ? params.MaxSettledVertices : vertexCount;
        const bool trackGoals = !goalVertices.empty();
        bool reachedGoal = goalVertices.empty();

        while (!queue.empty() && result.SettledVertexCount < settleBudget)
        {
            const ReferenceFrontNode node = queue.top();
            queue.pop();

            if (node.Distance > result.Distances[node.Vertex.Index]) continue;
            if (settled[node.Vertex.Index] != 0) continue;
            settled[node.Vertex.Index] = 1;
            ++result.SettledVertexCount;

            if (goalMask[node.Vertex.Index] != 0)
            {
                ++result.ReachedGoalCount;
                reachedGoal = true;
                if (params.StopWhenAllTargetsSettled && result.ReachedGoalCount >= validGoalCount)
                {
                    result.Converged = true;
                    result.EarlyTerminated = true;
                    break;
                }
            }

            std::size_t safety = 0;
            const std::size_t safetyLimit = graph.HalfedgesSize();
            for (const Geometry::HalfedgeHandle h : graph.HalfedgesAroundVertex(node.Vertex))
            {
                if (++safety > safetyLimit) break;

                const Geometry::VertexHandle next = graph.ToVertex(h);
                if (!IsReferenceUsableVertex(graph, next)) continue;

                const double stepLength = ReferenceEdgeLength(graph, node.Vertex, next);
                if (!std::isfinite(stepLength)) continue;

                const double newDistance = node.Distance + stepLength;
                ++result.RelaxedEdgeCount;
                if (newDistance < result.Distances[next.Index])
                {
                    result.Distances[next.Index] = newDistance;
                    result.Predecessors[next.Index] = node.Vertex;
                    queue.push(ReferenceFrontNode{next, newDistance});
                    ++result.QueuePushCount;
                }
            }
        }

        if (result.SettledVertexCount >= settleBudget && !queue.empty())
        {
            result.Converged = false;
        }
        else if (!trackGoals)
        {
            result.Converged = true;
        }
        else if (result.ReachedGoalCount >= validGoalCount)
        {
            result.Converged = true;
        }

        if (!reachedGoal) return std::nullopt;
        return result;
    }

    [[nodiscard]] std::optional<ReferenceShortestPathResult> ReferenceDijkstra(
        const Geometry::Graph::Graph& graph,
        const std::vector<Geometry::VertexHandle>& startVertices,
        const std::vector<Geometry::VertexHandle>& endVertices,
        const Geometry::ShortestPath::DijkstraParams& params = {})
    {
        if (startVertices.empty() && endVertices.empty()) return std::nullopt;

        const std::vector<Geometry::VertexHandle> emptyGoals;
        const auto& seedVertices = startVertices.empty() ? endVertices : startVertices;
        const auto& goalVertices = startVertices.empty() ? emptyGoals : endVertices;
        return ReferenceDijkstraCommon(graph, seedVertices, goalVertices, params);
    }

    void ExpectDijkstraMatchesPriorityQueueReference(
        const Geometry::Graph::Graph& input,
        const std::vector<Geometry::VertexHandle>& sources,
        const std::vector<Geometry::VertexHandle>& targets,
        const Geometry::ShortestPath::DijkstraParams& params = {})
    {
        Geometry::Graph::Graph indexedGraph = input;
        const auto actual = Geometry::ShortestPath::Dijkstra(indexedGraph, sources, targets, params);
        const auto expected = ReferenceDijkstra(input, sources, targets, params);

        ASSERT_EQ(actual.has_value(), expected.has_value());
        if (!actual || !expected) return;

        ASSERT_EQ(indexedGraph.VerticesSize(), expected->Distances.size());
        for (std::size_t i = 0; i < expected->Distances.size(); ++i)
        {
            const Geometry::VertexHandle vertex = V(i);
            const double actualDistance = actual->Distances[vertex];
            const double expectedDistance = expected->Distances[i];
            if (std::isinf(expectedDistance))
            {
                EXPECT_TRUE(std::isinf(actualDistance)) << "vertex " << i;
            }
            else
            {
                EXPECT_NEAR(actualDistance, expectedDistance, kDistanceTolerance) << "vertex " << i;
            }
            EXPECT_EQ(actual->Predecessors[vertex], expected->Predecessors[i]) << "vertex " << i;
        }

        EXPECT_EQ(actual->SettledVertexCount, expected->SettledVertexCount);
        EXPECT_EQ(actual->RelaxedEdgeCount, expected->RelaxedEdgeCount);
        EXPECT_EQ(actual->QueuePushCount, expected->QueuePushCount);
        EXPECT_EQ(actual->ReachedGoalCount, expected->ReachedGoalCount);
        EXPECT_EQ(actual->Converged, expected->Converged);
        EXPECT_EQ(actual->EarlyTerminated, expected->EarlyTerminated);
    }

    [[nodiscard]] Geometry::Graph::Graph MakeSeededConnectedGraph(std::uint32_t seed)
    {
        Geometry::Graph::Graph graph;
        std::mt19937 rng(seed);
        std::uniform_real_distribution<float> coord(-3.0f, 3.0f);

        constexpr std::size_t kVertexCount = 18;
        for (std::size_t i = 0; i < kVertexCount; ++i)
        {
            graph.AddVertex(glm::vec3{coord(rng), coord(rng), static_cast<float>(i % 5) * 0.25f});
        }

        for (std::size_t i = 1; i < kVertexCount; ++i)
        {
            EXPECT_TRUE(graph.AddEdge(V(i - 1), V(i)).has_value());
        }

        std::uniform_int_distribution<int> vertexDist(0, static_cast<int>(kVertexCount - 1));
        for (std::size_t i = 0; i < 48; ++i)
        {
            const auto a = static_cast<std::size_t>(vertexDist(rng));
            const auto b = static_cast<std::size_t>(vertexDist(rng));
            if (a == b) continue;
            (void)graph.AddEdge(V(a), V(b));
        }

        return graph;
    }

    [[nodiscard]] Geometry::Graph::Graph MakeDisconnectedGraph()
    {
        Geometry::Graph::Graph graph;
        for (std::size_t i = 0; i < 8; ++i)
        {
            const float row = i < 4 ? 0.0f : 4.0f;
            graph.AddVertex(glm::vec3{static_cast<float>(i % 4), row, 0.0f});
        }

        EXPECT_TRUE(graph.AddEdge(V(0), V(1)).has_value());
        EXPECT_TRUE(graph.AddEdge(V(1), V(2)).has_value());
        EXPECT_TRUE(graph.AddEdge(V(2), V(3)).has_value());
        EXPECT_TRUE(graph.AddEdge(V(4), V(5)).has_value());
        EXPECT_TRUE(graph.AddEdge(V(5), V(6)).has_value());
        EXPECT_TRUE(graph.AddEdge(V(6), V(7)).has_value());
        return graph;
    }

    [[nodiscard]] Geometry::Graph::Graph MakeEqualWeightDiamondGraph()
    {
        Geometry::Graph::Graph graph;
        graph.AddVertex(glm::vec3{0.0f, 0.0f, 0.0f});
        graph.AddVertex(glm::vec3{1.0f, 0.0f, 0.0f});
        graph.AddVertex(glm::vec3{0.0f, 1.0f, 0.0f});
        graph.AddVertex(glm::vec3{1.0f, 1.0f, 0.0f});

        EXPECT_TRUE(graph.AddEdge(V(0), V(1)).has_value());
        EXPECT_TRUE(graph.AddEdge(V(0), V(2)).has_value());
        EXPECT_TRUE(graph.AddEdge(V(1), V(3)).has_value());
        EXPECT_TRUE(graph.AddEdge(V(2), V(3)).has_value());
        return graph;
    }
}

TEST(ShortestPath, MeshBackedGraphViewTriangleChoosesDirectEdge)
{
    auto mesh = MakeSingleTriangle();
    std::vector<Geometry::VertexHandle> sources{Geometry::VertexHandle{0}};
    std::vector<Geometry::VertexHandle> targets{Geometry::VertexHandle{2}};

    // ShortestPath is graph-domain only; mesh-backed coverage passes a graph view
    // that shares the mesh property storage.
    Geometry::Graph::Graph graph = BorrowMeshAsGraphReadOnly(mesh);

    auto result = Geometry::ShortestPath::Dijkstra(graph, sources, targets);
    ASSERT_TRUE(result.has_value());

    EXPECT_NEAR(result->Distances[Geometry::VertexHandle{0}], 0.0, 1e-9);
    EXPECT_NEAR(result->Distances[Geometry::VertexHandle{2}], 1.0, 1e-6);
    EXPECT_EQ(result->Predecessors[Geometry::VertexHandle{2}], Geometry::VertexHandle{0});

    auto path = Geometry::ShortestPath::ExtractPathGraph(graph, *result, sources, targets);
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(path->VertexCount(), 2u);
    EXPECT_EQ(path->EdgeCount(), 1u);
}

TEST(ShortestPath, MeshBackedGraphViewReusesSharedConnectivityProperties)
{
    auto mesh = MakeSingleTriangle();

    const auto meshVertexConnectivity = mesh.VertexProperties().Get<Geometry::Graph::VertexConnectivity>("v:connectivity");
    const auto meshHalfedgeConnectivity = mesh.HalfedgeProperties().Get<Geometry::Graph::HalfedgeConnectivity>("h:connectivity");
    ASSERT_TRUE(meshVertexConnectivity.IsValid());
    ASSERT_TRUE(meshHalfedgeConnectivity.IsValid());

    Geometry::Graph::Graph graph = BorrowMeshAsGraphReadOnly(mesh);

    const auto graphVertexConnectivity = graph.VertexProperties().Get<Geometry::Graph::VertexConnectivity>("v:connectivity");
    const auto graphHalfedgeConnectivity = graph.HalfedgeProperties().Get<Geometry::Graph::HalfedgeConnectivity>("h:connectivity");
    ASSERT_TRUE(graphVertexConnectivity.IsValid());
    ASSERT_TRUE(graphHalfedgeConnectivity.IsValid());

    EXPECT_EQ(meshVertexConnectivity.Handle().Id(), graphVertexConnectivity.Handle().Id());
    EXPECT_EQ(meshHalfedgeConnectivity.Handle().Id(), graphHalfedgeConnectivity.Handle().Id());
    EXPECT_FALSE(mesh.VertexProperties().Exists("v:graph_connectivity"));
    EXPECT_FALSE(mesh.HalfedgeProperties().Exists("h:graph_connectivity"));
}

TEST(ShortestPath, ReturnsNulloptWhenBothSetsEmpty)
{
    auto mesh = MakeSingleTriangle();
    std::vector<Geometry::VertexHandle> empty;

    Geometry::Graph::Graph graph = BorrowMeshAsGraphReadOnly(mesh);

    auto result = Geometry::ShortestPath::Dijkstra(graph, empty, empty);
    EXPECT_FALSE(result.has_value());
}

TEST(ShortestPath, ReverseTreeWhenStartsEmpty)
{
    auto mesh = MakeSingleTriangle();
    Geometry::Graph::Graph graph = BorrowMeshAsGraphReadOnly(mesh);
    std::vector<Geometry::VertexHandle> empty;
    std::vector<Geometry::VertexHandle> targets{Geometry::VertexHandle{2}};

    auto result = Geometry::ShortestPath::Dijkstra(graph, empty, targets);
    ASSERT_TRUE(result.has_value());

    EXPECT_NEAR(result->Distances[Geometry::VertexHandle{2}], 0.0, 1e-9);
    EXPECT_NEAR(result->Distances[Geometry::VertexHandle{0}], 1.0, 1e-6);
    EXPECT_NEAR(result->Distances[Geometry::VertexHandle{1}], 1.0, 1e-6);
    EXPECT_EQ(result->Predecessors[Geometry::VertexHandle{0}], Geometry::VertexHandle{2});
    EXPECT_EQ(result->Predecessors[Geometry::VertexHandle{1}], Geometry::VertexHandle{2});

    auto path = Geometry::ShortestPath::ExtractPathGraph(graph, *result, empty, targets);
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(path->VertexCount(), 3u);
    EXPECT_EQ(path->EdgeCount(), 2u);
}

TEST(ShortestPath, ForwardTreeWhenTargetsEmpty)
{
    auto mesh = MakeSingleTriangle();
    Geometry::Graph::Graph graph = BorrowMeshAsGraphReadOnly(mesh);
    std::vector<Geometry::VertexHandle> sources{Geometry::VertexHandle{0}};
    std::vector<Geometry::VertexHandle> empty;

    auto result = Geometry::ShortestPath::Dijkstra(graph, sources, empty);
    ASSERT_TRUE(result.has_value());

    EXPECT_NEAR(result->Distances[Geometry::VertexHandle{0}], 0.0, 1e-9);
    EXPECT_NEAR(result->Distances[Geometry::VertexHandle{1}], 1.0, 1e-6);
    EXPECT_NEAR(result->Distances[Geometry::VertexHandle{2}], 1.0, 1e-6);
    EXPECT_EQ(result->Predecessors[Geometry::VertexHandle{1}], Geometry::VertexHandle{0});
    EXPECT_EQ(result->Predecessors[Geometry::VertexHandle{2}], Geometry::VertexHandle{0});

    auto path = Geometry::ShortestPath::ExtractPathGraph(graph, *result, sources, empty);
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(path->VertexCount(), 3u);
    EXPECT_EQ(path->EdgeCount(), 2u);
}

TEST(ShortestPath, GraphChainMatchesMeshBackedGraphViewContract)
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

TEST(ShortestPath, ReturnsNulloptForEmptyMeshBackedGraphView)
{
    Geometry::HalfedgeMesh::Mesh mesh;
    Geometry::Graph::Graph graph = BorrowMeshAsGraphReadOnly(mesh);
    std::vector<Geometry::VertexHandle> sources{Geometry::VertexHandle{0}};
    std::vector<Geometry::VertexHandle> targets{Geometry::VertexHandle{1}};

    auto result = Geometry::ShortestPath::Dijkstra(graph, sources, targets);
    EXPECT_FALSE(result.has_value());
}


TEST(ShortestPath, EarlyTerminatesAfterAllTargetsSettled)
{
    Geometry::Graph::Graph graph;
    auto v0 = graph.AddVertex(glm::vec3{0.0f, 0.0f, 0.0f});
    auto v1 = graph.AddVertex(glm::vec3{1.0f, 0.0f, 0.0f});
    auto v2 = graph.AddVertex(glm::vec3{2.0f, 0.0f, 0.0f});
    auto v3 = graph.AddVertex(glm::vec3{3.0f, 0.0f, 0.0f});
    auto v4 = graph.AddVertex(glm::vec3{4.0f, 0.0f, 0.0f});

    ASSERT_TRUE(graph.AddEdge(v0, v1).has_value());
    ASSERT_TRUE(graph.AddEdge(v1, v2).has_value());
    ASSERT_TRUE(graph.AddEdge(v2, v3).has_value());
    ASSERT_TRUE(graph.AddEdge(v3, v4).has_value());

    std::vector<Geometry::VertexHandle> sources{v0};
    std::vector<Geometry::VertexHandle> targets{v2};

    auto result = Geometry::ShortestPath::Dijkstra(graph, sources, targets);
    ASSERT_TRUE(result.has_value());

    EXPECT_TRUE(result->Converged);
    EXPECT_TRUE(result->EarlyTerminated);
    EXPECT_EQ(result->ReachedGoalCount, 1u);
    EXPECT_EQ(result->SettledVertexCount, 3u);
    EXPECT_TRUE(std::isinf(result->Distances[v3]));
    EXPECT_TRUE(std::isinf(result->Distances[v4]));
}

TEST(ShortestPath, SettleBudgetPreventsConvergenceWhenTooSmall)
{
    Geometry::Graph::Graph graph;
    auto v0 = graph.AddVertex(glm::vec3{0.0f, 0.0f, 0.0f});
    auto v1 = graph.AddVertex(glm::vec3{1.0f, 0.0f, 0.0f});
    auto v2 = graph.AddVertex(glm::vec3{2.0f, 0.0f, 0.0f});

    ASSERT_TRUE(graph.AddEdge(v0, v1).has_value());
    ASSERT_TRUE(graph.AddEdge(v1, v2).has_value());

    std::vector<Geometry::VertexHandle> sources{v0};
    std::vector<Geometry::VertexHandle> targets{v2};

    Geometry::ShortestPath::DijkstraParams params{};
    params.MaxSettledVertices = 2;
    auto result = Geometry::ShortestPath::Dijkstra(graph, sources, targets, params);
    EXPECT_FALSE(result.has_value());
}

TEST(ShortestPath, IndexedHeapMatchesPriorityQueueReferenceOnSeededRandomGraphs)
{
    for (const std::uint32_t seed : {11u, 29u, 47u, 83u})
    {
        const Geometry::Graph::Graph graph = MakeSeededConnectedGraph(seed);

        ExpectDijkstraMatchesPriorityQueueReference(graph, {V(0)}, {});
        ExpectDijkstraMatchesPriorityQueueReference(graph, {}, {V(17)});
        ExpectDijkstraMatchesPriorityQueueReference(graph, {V(0)}, {V(17)});
        ExpectDijkstraMatchesPriorityQueueReference(graph, {V(0), V(9)}, {V(17)});

        Geometry::ShortestPath::DijkstraParams noEarlyStop{};
        noEarlyStop.StopWhenAllTargetsSettled = false;
        ExpectDijkstraMatchesPriorityQueueReference(graph, {V(0)}, {V(8), V(17)}, noEarlyStop);
    }
}

TEST(ShortestPath, IndexedHeapMatchesPriorityQueueReferenceOnDisconnectedComponents)
{
    const Geometry::Graph::Graph graph = MakeDisconnectedGraph();

    ExpectDijkstraMatchesPriorityQueueReference(graph, {V(0)}, {});
    ExpectDijkstraMatchesPriorityQueueReference(graph, {V(0)}, {V(3)});
    ExpectDijkstraMatchesPriorityQueueReference(graph, {V(0)}, {V(7)});
    ExpectDijkstraMatchesPriorityQueueReference(graph, {V(0), V(4)}, {V(7)});
}

TEST(ShortestPath, IndexedHeapPreservesEqualWeightTieBreak)
{
    const Geometry::Graph::Graph graph = MakeEqualWeightDiamondGraph();
    ExpectDijkstraMatchesPriorityQueueReference(graph, {V(0)}, {V(3)});

    Geometry::Graph::Graph indexedGraph = graph;
    const std::vector<Geometry::VertexHandle> sources{V(0)};
    const std::vector<Geometry::VertexHandle> targets{V(3)};
    const auto result = Geometry::ShortestPath::Dijkstra(indexedGraph, sources, targets);
    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->Distances[V(3)], 2.0, 1e-6);
    EXPECT_EQ(result->Predecessors[V(3)], V(1));
}

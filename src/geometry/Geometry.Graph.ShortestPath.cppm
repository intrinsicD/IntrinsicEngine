module;

#include <cstddef>
#include <optional>
#include <span>

export module Geometry.Graph.ShortestPath;

import Geometry.HalfedgeMesh;
import Geometry.Graph;
import Geometry.Properties;

export namespace Geometry::ShortestPath
{
    struct DijkstraParams
    {
        // Safety cap for vertex-settle operations. If zero, defaults to VerticesSize().
        std::size_t MaxSettledVertices{0};

        // Stop as soon as every valid target vertex is settled. Enabled by default
        // for source-to-target queries; ignored for full-tree queries where no
        // explicit targets are provided.
        bool StopWhenAllTargetsSettled{true};
    };

    struct ShortestPathResult
    {
        VertexProperty<double> Distances{};
        VertexProperty<VertexHandle> Predecessors{};

        // Diagnostics for convergence / traversal cost.
        std::size_t SettledVertexCount{0};
        std::size_t RelaxedEdgeCount{0};
        std::size_t QueuePushCount{0};
        std::size_t ReachedGoalCount{0};
        bool Converged{false};
        bool EarlyTerminated{false};
    };
    // Compute a shortest-path tree on a graph-domain view.
    // Mesh-backed callers should construct a Graph::Graph view from the mesh
    // property sets before calling into this API.
    //
    // Empty-set semantics:
    //   - startVertices empty, endVertices empty -> std::nullopt (do nothing)
    //   - startVertices empty, endVertices non-empty -> reverse tree rooted at targets
    //   - startVertices non-empty, endVertices empty -> forward tree rooted at sources
    //   - both non-empty -> source-to-target query
    //
    // Distances/Predecessors are written into persistent vertex properties so the
    // result can be inspected after the call returns.

    [[nodiscard]] std::optional<ShortestPathResult> Dijkstra(
        Graph::Graph& graph,
        std::span<const VertexHandle> startVertices,
        std::span<const VertexHandle> endVertices,
        const DijkstraParams& params = {});

    [[nodiscard]] std::optional<Graph::Graph> ExtractPathGraph(
        const Graph::Graph& graph,
        const ShortestPathResult& result,
        std::span<const VertexHandle> startVertices,
        std::span<const VertexHandle> endVertices);
}

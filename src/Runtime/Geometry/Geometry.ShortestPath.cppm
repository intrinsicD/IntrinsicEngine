module;

#include <optional>
#include <span>

export module Geometry:ShortestPath;

import :HalfedgeMesh;
import :Graph;
import :Properties;

export namespace Geometry::ShortestPath
{
    struct ShortestPathResult
    {
        VertexProperty<double> Distances{};
        VertexProperty<VertexHandle> Predecessors{};
    };
    // Compute a shortest-path tree on a mesh or graph.
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
        Halfedge::Mesh& mesh,
        std::span<const VertexHandle> startVertices,
        std::span<const VertexHandle> endVertices);

    [[nodiscard]] std::optional<ShortestPathResult> Dijkstra(
        Graph::Graph& graph,
        std::span<const VertexHandle> startVertices,
        std::span<const VertexHandle> endVertices);

    [[nodiscard]] std::optional<Graph::Graph> ExtractPathGraph(
        const Halfedge::Mesh& mesh,
        const ShortestPathResult& result,
        std::span<const VertexHandle> startVertices,
        std::span<const VertexHandle> endVertices);

    [[nodiscard]] std::optional<Graph::Graph> ExtractPathGraph(
        const Graph::Graph& graph,
        const ShortestPathResult& result,
        std::span<const VertexHandle> startVertices,
        std::span<const VertexHandle> endVertices);
}
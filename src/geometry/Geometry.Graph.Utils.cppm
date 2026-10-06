// Graph construction and geometric queries with borrowed input adapters.
module;

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>
#include <glm/glm.hpp>

export module Geometry.Graph.Utils;

export import Geometry.Graph;

import Geometry.Properties;
import Geometry.Circulators;
export import Geometry.SpatialQueries;

export namespace Geometry::Graph
{
    enum class KNNConnectivity : std::uint8_t
    {
        Union,
        Mutual
    };

    struct KNNBuildParams
    {
        std::uint32_t K{8};
        float MinDistanceEpsilon{1.0e-12f};
        KNNConnectivity Connectivity{KNNConnectivity::Union};
    };

    struct KNNBuildResult
    {
        std::size_t VertexCount{0};
        std::size_t RequestedK{0};
        std::size_t EffectiveK{0};
        std::size_t CandidateEdgeCount{0};
        std::size_t InsertedEdgeCount{0};
        std::size_t DegeneratePairCount{0};
    };

    struct KNNFromIndicesParams
    {
        float MinDistanceEpsilon{1.0e-12f};
        KNNConnectivity Connectivity{KNNConnectivity::Union};
    };

    enum class EdgeLengthStatus : std::uint8_t
    {
        Success,
        EmptyGraph,
        InsufficientOutput,
        InvalidEdge,
        NonFinitePosition,
        ZeroLengthEdge
    };

    struct EdgeLengthParams
    {
        float MinLength{1.0e-12F};
        bool RejectZeroLengthEdges{true};
    };

    struct EdgeLengthResult
    {
        EdgeLengthStatus Status{EdgeLengthStatus::Success};
        std::size_t EdgeCount{0};
        std::size_t FilledCount{0};
        EdgeHandle FirstFailedEdge{};
        float MinLength{std::numeric_limits<float>::infinity()};
        float MaxLength{0.0F};
    };

    enum class EdgeQueryStatus : std::uint8_t
    {
        Success,
        EmptyGraph,
        InvalidQueryPoint,
        InvalidParameters,
        InvalidSeedVertex,
        NoCandidates,
        NonFinitePosition,
        ZeroLengthEdge
    };

    struct ClosestEdgeQueryResult
    {
        EdgeQueryStatus Status{EdgeQueryStatus::NoCandidates};
        EdgeHandle Edge{};
        glm::vec3 ClosestPoint{0.0F};
        float SquaredDistance{std::numeric_limits<float>::infinity()};
        float SegmentT{0.0F};
    };

    struct EdgeQuerySetResult
    {
        EdgeQueryStatus Status{EdgeQueryStatus::NoCandidates};
        std::vector<ClosestEdgeQueryResult> Edges{};
    };

    // Rebuilds `graph` from a point set using an undirected k-nearest-neighbor construction.
    // Returns std::nullopt for degenerate input (empty points or k == 0).
    [[nodiscard]] std::optional<KNNBuildResult> BuildKNNGraph(Graph& graph, std::span<const glm::vec3> points,
        const KNNBuildParams& params = {});

    // Complete min(k+1,N) rows use distance/source-ID order and no self exclusion.
    // The self/epsilon filter runs afterward; malformed rows leave graph unchanged.
    [[nodiscard]] std::optional<KNNBuildResult> BuildKNNGraphFromNeighbors(
        Graph& graph, std::span<const glm::vec3> points, PointNeighborhoods rows,
        const KNNBuildParams& params = {});

    // Builds an undirected graph directly from precomputed per-vertex kNN index lists.
    // The implementation validates indices and rejects degenerate pairs using epsilon.
    [[nodiscard]] std::optional<KNNBuildResult> BuildKNNGraphFromIndices(Graph& graph,
        std::span<const glm::vec3> points, std::span<const std::vector<std::uint32_t>> knnIndices,
        const KNNFromIndicesParams& params = {});

    // Fills a dense edge-length span indexed by EdgeHandle::Index.
    // The operation fails closed by default on zero-length or non-finite edges.
    [[nodiscard]] EdgeLengthResult FillEdgeLengths(
        const Graph& graph, std::span<float> outLengths, const EdgeLengthParams& params = {});

    // Publishes cached edge lengths in the conventional "e:length" property.
    [[nodiscard]] EdgeLengthResult EnsureEdgeLengths(Graph& graph, const EdgeLengthParams& params = {});

    [[nodiscard]] ClosestEdgeQueryResult ClosestEdge(const Graph& graph, glm::vec3 point);

    [[nodiscard]] EdgeQuerySetResult KClosestEdges(const Graph& graph, glm::vec3 point, std::size_t k);

    [[nodiscard]] EdgeQuerySetResult EdgesWithinRadius(const Graph& graph, glm::vec3 point, float radius);

    [[nodiscard]] ClosestEdgeQueryResult ClosestEdgeWithinOneRing(
        const Graph& graph, VertexHandle seedVertex, glm::vec3 point);
}

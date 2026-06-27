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

    struct ForceDirectedLayoutParams
    {
        std::uint32_t MaxIterations{128};
        float AreaExtent{2.0F};
        float InitialTemperatureFactor{0.25F};
        float CoolingFactor{0.95F};
        float MinDistanceEpsilon{1.0e-6F};
        float ConvergenceTolerance{1.0e-4F};
        float Gravity{0.05F};
    };

    struct ForceDirectedLayoutResult
    {
        std::size_t ActiveVertexCount{0};
        std::size_t ActiveEdgeCount{0};
        std::uint32_t IterationsPerformed{0};
        float FinalTemperature{0.0F};
        float MaxDisplacement{0.0F};
        bool Converged{false};
    };

    struct SpectralLayoutParams
    {
        enum class LaplacianVariant : std::uint8_t
        {
            Combinatorial,
            NormalizedSymmetric
        };

        std::uint32_t MaxIterations{96};
        float StepScale{0.85F};
        float ConvergenceTolerance{1.0e-5F};
        float MinNormEpsilon{1.0e-8F};
        float AreaExtent{2.0F};
        LaplacianVariant Variant{LaplacianVariant::Combinatorial};
    };

    struct SpectralLayoutResult
    {
        std::size_t ActiveVertexCount{0};
        std::size_t ActiveEdgeCount{0};
        std::uint32_t IterationsPerformed{0};
        float SubspaceDelta{0.0F};
        bool Converged{false};
    };

    struct HierarchicalLayoutParams
    {
        std::uint32_t RootVertexIndex{kInvalidIndex};
        std::uint32_t CrossingMinimizationSweeps{4};
        float LayerSpacing{1.0F};
        float NodeSpacing{1.0F};
        float ComponentSpacing{2.0F};
    };

    struct HierarchicalLayoutResult
    {
        std::size_t ActiveVertexCount{0};
        std::size_t ActiveEdgeCount{0};
        std::size_t ComponentCount{0};
        std::size_t LayerCount{0};
        std::size_t MaxLayerWidth{0};
        std::size_t CrossingCount{0};
    };

    struct EdgeCrossingParams
    {
        float IntersectionEpsilon{1.0e-6F};
        bool IgnoreIncidentEdges{true};
        bool CountCollinearOverlap{false};
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

    struct GraphGaussianNoiseParams
    {
        float StdDevFraction{0.0F};
        std::uint64_t Seed{0};
    };

    enum class GaussianNoiseStatus : std::uint8_t
    {
        Success,
        EmptyInput,
        InvalidParameters,
        NonFinitePosition,
        DegenerateScale
    };

    struct GaussianNoiseResult
    {
        GaussianNoiseStatus Status{GaussianNoiseStatus::Success};
        std::size_t ElementCount{0};
        std::size_t DisplacedCount{0};
        float Scale{0.0F};
        glm::vec3 MeanDisplacement{0.0F};
        float MaxDisplacement{0.0F};
    };

    // A lightweight halfedge-based graph (no faces), designed for DOD-friendly algorithms.
    // Storage is via PropertySets, so user-defined properties are supported on vertices/halfedges/edges.
    // Rebuilds `graph` from a point set using an undirected k-nearest-neighbor construction.
    // Returns std::nullopt for degenerate input (empty points or k == 0).
    [[nodiscard]] std::optional<KNNBuildResult> BuildKNNGraph(Graph& graph, std::span<const glm::vec3> points,
        const KNNBuildParams& params = {});

    // Builds an undirected graph directly from precomputed per-vertex kNN index lists.
    // The implementation validates indices and rejects degenerate pairs using epsilon.
    [[nodiscard]] std::optional<KNNBuildResult> BuildKNNGraphFromIndices(Graph& graph,
        std::span<const glm::vec3> points, std::span<const std::vector<std::uint32_t>> knnIndices,
        const KNNFromIndicesParams& params = {});

    // Computes a Fruchterman-Reingold style 2D embedding for the current graph topology.
    // `ioPositions` is updated in-place and must have at least graph.VerticesSize() entries.
    [[nodiscard]] std::optional<ForceDirectedLayoutResult> ComputeForceDirectedLayout(
        const Graph& graph, std::span<glm::vec2> ioPositions, const ForceDirectedLayoutParams& params = {});

    // Computes a 2D spectral embedding using the first two non-constant eigenmodes
    // of the graph Laplacian via projected orthogonal iteration.
    [[nodiscard]] std::optional<SpectralLayoutResult> ComputeSpectralLayout(
        const Graph& graph, std::span<glm::vec2> ioPositions, const SpectralLayoutParams& params = {});

    // Computes a deterministic hierarchical (layered) embedding using BFS layers
    // and barycentric crossing-minimization sweeps within each layer.
    [[nodiscard]] std::optional<HierarchicalLayoutResult> ComputeHierarchicalLayout(
        const Graph& graph, std::span<glm::vec2> ioPositions, const HierarchicalLayoutParams& params = {});

    // Counts geometric edge crossings for a 2D embedding of the graph.
    // Returns std::nullopt for degenerate input (insufficient positions or non-finite coordinates).
    [[nodiscard]] std::optional<std::size_t> CountEdgeCrossings(
        const Graph& graph, std::span<const glm::vec2> positions, const EdgeCrossingParams& params = {});

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

    [[nodiscard]] GaussianNoiseResult ApplyGaussianNoise(Graph& graph, const GraphGaussianNoiseParams& params = {});
}

module;

#include <cstddef>
#include <cstdint>
#include <utility>
#include <string>
#include <optional>
#include <span>
#include <vector>
#include <glm/glm.hpp>

export module Geometry:Graph;

import :Properties;

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

    // A lightweight halfedge-based graph (no faces), designed for DOD-friendly algorithms.
    // Storage is via PropertySets, so user-defined properties are supported on vertices/halfedges/edges.
    class Graph
    {
    public:
        Graph();
        Graph(const Graph& rhs);
        Graph(Graph&&) noexcept = default;
        ~Graph();

        Graph& operator=(const Graph& rhs);
        Graph& operator=(Graph&&) noexcept = default;

        [[nodiscard]] VertexHandle AddVertex();
        [[nodiscard]] VertexHandle AddVertex(glm::vec3 position);

        void Clear();
        void FreeMemory();
        void Reserve(std::size_t nVertices, std::size_t nEdges);

        // Topology
        [[nodiscard]] std::optional<EdgeHandle> AddEdge(VertexHandle v0, VertexHandle v1);
        void DeleteEdge(EdgeHandle e);
        void DeleteVertex(VertexHandle v);

        void GarbageCollection();
        [[nodiscard]] bool HasGarbage() const noexcept { return m_HasGarbage; }

        [[nodiscard]] std::size_t VerticesSize() const noexcept { return m_Vertices.Size(); }
        [[nodiscard]] std::size_t HalfedgesSize() const noexcept { return m_Halfedges.Size(); }
        [[nodiscard]] std::size_t EdgesSize() const noexcept { return m_Edges.Size(); }

        [[nodiscard]] std::size_t VertexCount() const noexcept { return VerticesSize() - m_DeletedVertices; }
        [[nodiscard]] std::size_t EdgeCount() const noexcept { return EdgesSize() - m_DeletedEdges; }

        [[nodiscard]] bool IsDeleted(VertexHandle v) const { return m_VDeleted[v]; }
        [[nodiscard]] bool IsDeleted(EdgeHandle e) const { return m_EDeleted[e]; }
        [[nodiscard]] bool IsDeleted(HalfedgeHandle h) const { return m_EDeleted[Edge(h)]; }

        [[nodiscard]] bool IsValid(VertexHandle v) const { return v.IsValid() && v.Index < VerticesSize(); }
        [[nodiscard]] bool IsValid(EdgeHandle e) const { return e.IsValid() && e.Index < EdgesSize(); }
        [[nodiscard]] bool IsValid(HalfedgeHandle h) const { return h.IsValid() && h.Index < HalfedgesSize(); }

        // Connectivity
        [[nodiscard]] HalfedgeHandle Halfedge(VertexHandle v) const;
        void SetHalfedge(VertexHandle v, HalfedgeHandle h);

        [[nodiscard]] VertexHandle ToVertex(HalfedgeHandle h) const;
        void SetVertex(HalfedgeHandle h, VertexHandle v);

        [[nodiscard]] HalfedgeHandle NextHalfedge(HalfedgeHandle h) const;
        [[nodiscard]] HalfedgeHandle PrevHalfedge(HalfedgeHandle h) const;
        void SetNextHalfedge(HalfedgeHandle h, HalfedgeHandle next);
        void SetPrevHalfedge(HalfedgeHandle h, HalfedgeHandle prev);

        [[nodiscard]] HalfedgeHandle OppositeHalfedge(HalfedgeHandle h) const;

        [[nodiscard]] EdgeHandle Edge(HalfedgeHandle h) const;
        [[nodiscard]] HalfedgeHandle Halfedge(EdgeHandle e, unsigned int i) const;

        [[nodiscard]] bool IsIsolated(VertexHandle v) const;
        [[nodiscard]] bool IsBoundary(VertexHandle v) const;

        [[nodiscard]] std::optional<HalfedgeHandle> FindHalfedge(VertexHandle start, VertexHandle end) const;
        [[nodiscard]] std::optional<EdgeHandle> FindEdge(VertexHandle a, VertexHandle b) const;

        [[nodiscard]] glm::vec3 VertexPosition(VertexHandle v) const;
        void SetVertexPosition(VertexHandle v, glm::vec3 position);
        [[nodiscard]] std::pair<VertexHandle, VertexHandle> EdgeVertices(EdgeHandle e) const;

        // Properties
        template <class T>
        [[nodiscard]] VertexProperty<T> GetOrAddVertexProperty(std::string name, T defaultValue = T())
        {
            return Geometry::VertexProperty<T>(m_Vertices.GetOrAdd<T>(std::move(name), std::move(defaultValue)));
        }

        template <class T>
        [[nodiscard]] HalfedgeProperty<T> GetOrAddHalfedgeProperty(std::string name, T defaultValue = T())
        {
            return Geometry::HalfedgeProperty<T>(m_Halfedges.GetOrAdd<T>(std::move(name), std::move(defaultValue)));
        }

        template <class T>
        [[nodiscard]] EdgeProperty<T> GetOrAddEdgeProperty(std::string name, T defaultValue = T())
        {
            return Geometry::EdgeProperty<T>(m_Edges.GetOrAdd<T>(std::move(name), std::move(defaultValue)));
        }

    private:
        struct VertexConnectivity
        {
            HalfedgeHandle Halfedge{};
        };

        struct HalfedgeConnectivity
        {
            VertexHandle Vertex{};
            HalfedgeHandle Next{};
            HalfedgeHandle Prev{};
        };

        void EnsureProperties();

        [[nodiscard]] VertexHandle NewVertex();
        [[nodiscard]] HalfedgeHandle NewEdge();
        [[nodiscard]] HalfedgeHandle NewEdge(VertexHandle start, VertexHandle end);

        Vertices m_Vertices;
        Halfedges m_Halfedges;
        Edges m_Edges;

        VertexProperty<glm::vec3> m_VPoint;
        VertexProperty<VertexConnectivity> m_VConn;
        HalfedgeProperty<HalfedgeConnectivity> m_HConn;

        VertexProperty<bool> m_VDeleted;
        EdgeProperty<bool> m_EDeleted;

        std::size_t m_DeletedVertices{0};
        std::size_t m_DeletedEdges{0};
        bool m_HasGarbage{false};
    };

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
}

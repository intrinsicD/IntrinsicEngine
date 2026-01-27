module;

#include <cstddef>
#include <string>
#include <optional>
#include <glm/glm.hpp>

export module Geometry:Graph;

import :Properties;

export namespace Geometry::Graph
{
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
}

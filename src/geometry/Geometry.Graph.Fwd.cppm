module;

#include <cstddef>

export module Geometry.Graph.Fwd;

import Geometry.Properties;

export namespace Geometry::Graph
{
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

    struct GraphProperties
    {
        PropertySet Vertices;
        PropertySet Halfedges;
        PropertySet Edges;

        std::size_t m_DeletedVertices{0};
        std::size_t m_DeletedEdges{0};
    };
}

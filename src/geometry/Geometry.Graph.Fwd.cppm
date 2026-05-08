module;

#include <cstddef>

export module Geometry.GraphFwd;

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

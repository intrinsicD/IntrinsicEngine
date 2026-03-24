module;

#include <cstddef>

export module Geometry.GraphFwd;

import Geometry.Properties;

export namespace Geometry::Graph
{
    struct GraphProperties
    {
        PropertySet Vertices;
        PropertySet Halfedges;
        PropertySet Edges;

        std::size_t m_DeletedVertices{0};
        std::size_t m_DeletedEdges{0};
    };
}

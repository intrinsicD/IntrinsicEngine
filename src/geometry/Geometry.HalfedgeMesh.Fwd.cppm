module;

#include <cstdint>

export module Geometry.HalfedgeMesh.Fwd;

export import Geometry.Graph.Fwd;
import Geometry.Properties;

export namespace Geometry::HalfedgeMesh
{
    // Graph traversal connectivity is shared with Geometry::Graph so graph views
    // over mesh property storage can reuse the canonical face-free properties.
    using VertexConnectivity = Graph::VertexConnectivity;
    using HalfedgeConnectivity = Graph::HalfedgeConnectivity;

    struct HalfedgeFaceConnectivity
    {
        FaceHandle Face{};         // incident face (invalid => boundary)
    };

    struct FaceConnectivity
    {
        HalfedgeHandle Halfedge{};
    };

    // GPU-friendly edge representation: pair of vertex indices.
    // Layout-compatible with the SSBO EdgePair struct consumed by line shaders.
    struct EdgeVertexPair
    {
        uint32_t i0;
        uint32_t i1;
    };
    static_assert(sizeof(EdgeVertexPair) == 8);

    struct MeshProperties
    {
        PropertySet Vertices{};
        PropertySet Halfedges{};
        PropertySet Edges{};
        PropertySet Faces{};

        std::size_t DeletedVertices{0};
        std::size_t DeletedEdges{0};
        std::size_t DeletedFaces{0};
    };
}
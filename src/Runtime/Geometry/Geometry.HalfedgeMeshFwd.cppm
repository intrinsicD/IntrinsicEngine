module;

#include <cstdint>

export module Geometry.HalfedgeMeshFwd;

import Geometry.Properties;

export namespace Geometry::Halfedge
{
    // PMP-style connectivity
    struct VertexConnectivity
    {
        HalfedgeHandle Halfedge{};
    };

    struct HalfedgeConnectivity
    {
        VertexHandle Vertex{};     // to-vertex
        FaceHandle Face{};         // incident face (invalid => boundary)
        HalfedgeHandle Next{};
        HalfedgeHandle Prev{};
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
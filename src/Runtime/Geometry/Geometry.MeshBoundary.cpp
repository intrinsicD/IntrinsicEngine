module;

#include <cstddef>
#include <optional>
#include <vector>

export module Geometry:MeshBoundary.Impl;

import :MeshBoundary;

namespace Geometry::MeshBoundary
{
    [[nodiscard]] std::optional<BoundaryResult> Boundary(
        Halfedge::Mesh& mesh,
        const BoundaryParams& params)
    {
        BoundaryResult result;

        const std::size_t vertexCount = mesh.VerticesSize();
        const std::size_t edgeCount = mesh.EdgesSize();
        const std::size_t faceCount = mesh.FacesSize();

        result.IsBoundaryVertex = VertexProperty(mesh.VertexProperties().GetOrAdd<bool>("boundary_vertex", false));
        result.IsBoundaryEdge = EdgeProperty(mesh.EdgeProperties().GetOrAdd<bool>("boundary_edge", false));
        result.IsBoundaryFace = FaceProperty(mesh.FaceProperties().GetOrAdd<bool>("boundary_face", false));

        Property<bool> VertexFeatures;
        if (params.MarkBoundaryVerticesAsFeature)
        {
            VertexFeatures = mesh.VertexProperties().GetOrAdd<bool>("vertex_feature", false);
        }
        Property<bool> EdgeFeatures;
        if (params.MarkBoundaryEdgeAsFeature)
        {
            EdgeFeatures = mesh.EdgeProperties().GetOrAdd<bool>("edge_feature", false);
        }
        Property<bool> FaceFeatures;
        if (params.MarkBoundaryFaceAsFeature)
        {
            FaceFeatures = mesh.FaceProperties().GetOrAdd<bool>("face_feature", false);
        }
        for (std::size_t i = 0; i < vertexCount; ++i)
        {
            const VertexHandle v{static_cast<PropertyIndex>(i)};
            if (mesh.IsDeleted(v))
            {
                continue;
            }

            if (mesh.IsBoundary(v))
            {
                result.BoundaryVertices.push_back(v);
                result.IsBoundaryVertex[v] = true;
                if (params.MarkBoundaryVerticesAsFeature)
                {
                    VertexFeatures[v.Index] = true;
                }
            }
        }

        for (std::size_t i = 0; i < edgeCount; ++i)
        {
            const EdgeHandle e{static_cast<PropertyIndex>(i)};
            if (mesh.IsDeleted(e))
            {
                continue;
            }

            if (mesh.IsBoundary(e))
            {
                result.BoundaryEdges.push_back(e);
                result.IsBoundaryEdge[e] = true;
                if (params.MarkBoundaryEdgeAsFeature)
                {
                    EdgeFeatures[e.Index] = true;
                }
            }
        }

        for (std::size_t i = 0; i < faceCount; ++i)
        {
            const FaceHandle f{static_cast<PropertyIndex>(i)};
            if (mesh.IsDeleted(f))
            {
                continue;
            }

            if (mesh.IsBoundary(f))
            {
                result.BoundaryFaces.push_back(f);
                result.IsBoundaryFace[f] = true;
                if (params.MarkBoundaryFaceAsFeature)
                {
                    FaceFeatures[f.Index] = true;
                }
            }
        }

        return result;
    }
}

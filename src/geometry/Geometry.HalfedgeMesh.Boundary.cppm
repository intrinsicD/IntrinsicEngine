module;

#include <optional>
#include <vector>

export module Geometry.HalfedgeMesh.Boundary;

import Geometry.HalfedgeMesh;
import Geometry.Properties;

export namespace Geometry::MeshBoundary
{
    struct BoundaryParams
    {
        bool MarkBoundaryVerticesAsFeature{false};
        bool MarkBoundaryEdgeAsFeature{false};
        bool MarkBoundaryFaceAsFeature{false};
    };

    struct BoundaryResult
    {
        VertexProperty<bool> IsBoundaryVertex{};
        EdgeProperty<bool> IsBoundaryEdge{};
        FaceProperty<bool> IsBoundaryFace{};

        std::vector<VertexHandle> BoundaryVertices{};
        std::vector<EdgeHandle> BoundaryEdges{};
        std::vector<FaceHandle> BoundaryFaces{};
    };

    [[nodiscard]] std::optional<BoundaryResult> Boundary(
        HalfedgeMesh::Mesh& mesh,
        const BoundaryParams& params);
}

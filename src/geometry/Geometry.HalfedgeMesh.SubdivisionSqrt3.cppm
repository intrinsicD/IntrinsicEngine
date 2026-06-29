module;

#include <cstddef>
#include <optional>

export module Geometry.HalfedgeMesh.SubdivisionSqrt3;

import Geometry.Properties;
import Geometry.HalfedgeMesh;

export namespace Geometry::SubdivisionSqrt3
{
    // Sqrt(3) / Kobbelt subdivision for triangle halfedge meshes.
    //
    // Each iteration inserts one centroid vertex per triangle, creates three
    // child triangles per parent, relaxes original vertices with
    // beta(n) = (4 - 2*cos(2*pi/n)) / 9 toward their one-ring centroid, and
    // flips original interior edges. Boundary edges are left unflipped and
    // boundary vertices use the same 1D boundary stencil as Loop subdivision.
    struct Sqrt3Params
    {
        std::size_t Iterations{1};
        std::size_t MaxOutputFaces{0};
    };

    struct Sqrt3Result
    {
        std::size_t IterationsPerformed{0};
        std::size_t FinalVertexCount{0};
        std::size_t FinalFaceCount{0};
    };

    [[nodiscard]] std::optional<Sqrt3Result> Subdivide(
        const HalfedgeMesh::Mesh& input,
        HalfedgeMesh::Mesh& output,
        const Sqrt3Params& params = {});
}

module;

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

export module Geometry.MarchingCubes;

import Geometry.Grid;
import Geometry.HalfedgeMesh;
import Geometry.Properties;

export namespace Geometry::MarchingCubes
{
    // =========================================================================
    // Marching Cubes — Isosurface Extraction from Scalar Fields
    // =========================================================================
    //
    // Extracts a triangle mesh approximating an isosurface from a scalar field
    // defined on a regular 3D grid.
    //
    // Algorithm: Lorensen & Cline, "Marching Cubes: A High Resolution 3D
    // Surface Construction Algorithm", SIGGRAPH 1987.
    //
    // For each cell (cube) in the grid, the algorithm classifies the 8 corner
    // vertices as inside (value < isovalue) or outside (value >= isovalue).
    // The 256 possible configurations are resolved via lookup tables that
    // specify which edges of the cube are intersected by the isosurface and
    // how to triangulate those intersection points.
    //
    // Vertex positions on intersected edges are computed via linear
    // interpolation between the two endpoint values. Per-vertex normals are
    // computed from the gradient of the scalar field (central differences).
    //
    // Vertex welding: Adjacent cells share edges. The implementation assigns
    // each edge a unique key based on axis direction and grid-vertex origin,
    // ensuring intersection vertices on shared edges are created once and
    // reused, producing a watertight mesh without duplicate vertices.

    // -------------------------------------------------------------------------
    // Parameters and result
    // -------------------------------------------------------------------------

    struct MarchingCubesParams
    {
        // Isovalue: the scalar value defining the surface.
        // Vertices with value < Isovalue are "inside"; >= Isovalue are "outside".
        float Isovalue{0.0f};

        // If true, compute per-vertex normals from the scalar field gradient.
        bool ComputeNormals{true};
    };

    struct MarchingCubesResult
    {
        // Output triangle mesh (indexed triangle soup with welded vertices).
        std::vector<glm::vec3> Vertices;
        std::vector<glm::vec3> Normals;  // per-vertex (if requested); same size as Vertices
        std::vector<std::array<std::size_t, 3>> Triangles;  // index triplets

        std::size_t VertexCount{0};
        std::size_t TriangleCount{0};
    };

    // -------------------------------------------------------------------------
    // Extraction
    // -------------------------------------------------------------------------

    // Extract an isosurface from a DenseGrid.
    //
    // The grid must have a float property with the given name (default: "scalar").
    // Returns nullopt if:
    //   - The grid dimensions are invalid
    //   - The named property does not exist or is not float
    //   - The isosurface is empty
    [[nodiscard]] std::optional<MarchingCubesResult> Extract(
        const Grid::DenseGrid& grid,
        const MarchingCubesParams& params = {},
        std::string_view scalarPropertyName = "scalar");

    // -------------------------------------------------------------------------
    // Conversion to HalfedgeMesh
    // -------------------------------------------------------------------------

    // Convert a MarchingCubesResult (indexed triangle soup) to a HalfedgeMesh.
    // Some triangles may fail to add if they produce non-manifold edges;
    // the function skips those and returns the best possible mesh.
    //
    // Returns nullopt if the result is empty.
    [[nodiscard]] std::optional<Halfedge::Mesh> ToMesh(const MarchingCubesResult& result);

} // namespace Geometry::MarchingCubes

module;

#include <array>
#include <cstddef>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

export module Geometry:MarchingCubes;

import :HalfedgeMesh;

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
    // ScalarGrid — regular 3D grid of scalar values
    // -------------------------------------------------------------------------

    struct ScalarGrid
    {
        // Grid dimensions: number of cells along each axis.
        // The grid has (NX+1) x (NY+1) x (NZ+1) vertices.
        std::size_t NX{0};
        std::size_t NY{0};
        std::size_t NZ{0};

        // Grid origin (minimum corner in world space).
        glm::vec3 Origin{0.0f};

        // Cell spacing along each axis.
        glm::vec3 Spacing{1.0f};

        // Scalar values at grid vertices, linearized as:
        //   index = z * (NY+1) * (NX+1) + y * (NX+1) + x
        // Total size: (NX+1) * (NY+1) * (NZ+1)
        std::vector<float> Values;

        // Access the scalar value at grid vertex (x, y, z).
        [[nodiscard]] float At(std::size_t x, std::size_t y, std::size_t z) const
        {
            return Values[z * (NY + 1) * (NX + 1) + y * (NX + 1) + x];
        }

        // Set the scalar value at grid vertex (x, y, z).
        void Set(std::size_t x, std::size_t y, std::size_t z, float value)
        {
            Values[z * (NY + 1) * (NX + 1) + y * (NX + 1) + x] = value;
        }

        // Compute the world-space position of grid vertex (x, y, z).
        [[nodiscard]] glm::vec3 VertexPosition(std::size_t x, std::size_t y, std::size_t z) const
        {
            return Origin + Spacing * glm::vec3(
                static_cast<float>(x),
                static_cast<float>(y),
                static_cast<float>(z));
        }

        // Linear index of grid vertex (x, y, z).
        [[nodiscard]] std::size_t LinearIndex(std::size_t x, std::size_t y, std::size_t z) const
        {
            return z * (NY + 1) * (NX + 1) + y * (NX + 1) + x;
        }

        // Check whether the grid dimensions and value count are consistent.
        [[nodiscard]] bool IsValid() const noexcept
        {
            return NX > 0 && NY > 0 && NZ > 0
                && Values.size() == (NX + 1) * (NY + 1) * (NZ + 1);
        }
    };

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

    // Extract an isosurface from the scalar grid.
    //
    // Returns nullopt if:
    //   - The grid is invalid (dimensions or value count mismatch)
    //   - The isosurface is empty (no cells straddle the isovalue)
    [[nodiscard]] std::optional<MarchingCubesResult> Extract(
        const ScalarGrid& grid,
        const MarchingCubesParams& params = {});

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

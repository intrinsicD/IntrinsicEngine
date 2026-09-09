// Triangle-surface distance methods: heat diffusion and virtual-source
// propagation, with separate formulations and explicit propagation diagnostics.
module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>
#include <glm/glm.hpp>

export module Geometry.Geodesic;

import Geometry.Properties;
import Geometry.HalfedgeMesh;

export namespace Geometry::Geodesic
{
    enum class VirtualSourceStatus : std::uint8_t
    {
        Success,
        EmptyMesh,
        UnsupportedSubmeshView,
        InvalidParameters,
        InvalidSources,
        InvalidPositions,
        NonTriangleFace,
        DegenerateFace,
        ExpansionLimit,
        NumericalFailure,
    };

    struct VirtualSourceParams
    {
        std::size_t MaxHalfedgeExpansions{10000000};
    };

    struct VirtualSourceResult
    {
        VirtualSourceStatus Status{VirtualSourceStatus::EmptyMesh};
        std::size_t Iterations{0};
        std::size_t HalfedgeExpansions{0};
        std::size_t TriangleUpdates{0};
        std::size_t SourceCount{0};
        std::size_t UnreachableVertexCount{0};
        double MeanEdgeLength{0};
        // Absolute vertex slots, in position units. Unreachable/deleted slots
        // are +infinity; sources, including isolated sources, are exactly zero.
        // Failure returns no distance field and never changes mesh properties.
        std::vector<double> Distances{};
        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == VirtualSourceStatus::Success;
        }
    };

    [[nodiscard]] const char* ToString(VirtualSourceStatus status) noexcept;

    // CPU reference of Trettner, Bommes & Kobbelt (2021). Requires live,
    // nondegenerate triangles. Sources are vertex slots; duplicates are ignored.
    // The typed position span can bind any float3 property on mesh vertices.
    [[nodiscard]] VirtualSourceResult ComputeVirtualSourceDistance(
        const HalfedgeMesh::Mesh& mesh, std::span<const glm::vec3> positions,
        std::span<const std::size_t> sourceVertices, const VirtualSourceParams& params = {});

    [[nodiscard]] VirtualSourceResult ComputeVirtualSourceDistance(
        const HalfedgeMesh::Mesh& mesh, std::span<const std::size_t> sourceVertices,
        const VirtualSourceParams& params = {});

    // =========================================================================
    // Geodesic Distance via the Heat Method
    // =========================================================================
    //
    // Implementation of Crane, Weischedel & Wardetzky, "Geodesics in Heat:
    // A New Approach to Computing Distance Using Heat Kernels" (2013).
    //
    // The heat method computes approximate geodesic distances on a triangle
    // mesh in three steps:
    //
    //   1. Heat diffusion: Solve (M + t*L) u = δ_S
    //      where M = Hodge star 0 (mass matrix), L = weak cotan Laplacian,
    //      t = h² (h = mean edge length), and δ_S is a Dirac impulse
    //      at the source vertices.
    //
    //   2. Gradient normalization: For each face, compute the gradient of u,
    //      then normalize to get the unit vector field X = -∇u / |∇u|.
    //      This field points along the shortest paths.
    //
    //   3. Poisson solve: Recover the distance function φ by solving
    //      L φ = div(X), where div(X) is the integrated divergence of X.
    //      The solution is unique up to a constant; we shift so min(φ) = 0.
    //
    // Advantages over Dijkstra / fast marching:
    //   - Handles any triangle mesh topology (including non-convex, genus > 0)
    //   - Same factorization reused for multiple source sets
    //   - Accuracy improves with mesh refinement (converges to true geodesic)
    //
    // The DEC module provides all required operators (Hodge stars, Laplacian,
    // exterior derivatives). The CG solver handles the two linear solves.

    struct GeodesicParams
    {
        // Time step for heat diffusion. If 0, uses h² where h is the mean
        // edge length (recommended default from the paper).
        double TimeStep{0.0};

        // CG solver tolerance
        double SolverTolerance{1e-8};

        // CG solver maximum iterations
        std::size_t MaxSolverIterations{2000};
    };

    struct GeodesicResult
    {
        // Number of CG iterations used in each solve step
        std::size_t HeatSolveIterations{0};
        std::size_t PoissonSolveIterations{0};

        // Whether both solves converged
        bool Converged{false};
        // Per-vertex geodesic distances from the source set.
        // Deleted/isolated vertices have distance 0.
        VertexProperty<double> DistanceProperty{};
        VertexProperty<bool> IsSourceProperty{};
    };

    // -------------------------------------------------------------------------
    // Compute geodesic distances from a set of source vertices
    // -------------------------------------------------------------------------
    //
    // sourceVertices: indices of source vertices (0-based, into VerticesSize())
    //
    // Returns nullopt if the mesh is empty, has no faces, or the source set
    // is empty.
    [[nodiscard]] std::optional<GeodesicResult> ComputeDistance(
        HalfedgeMesh::Mesh& mesh,
        std::span<const std::size_t> sourceVertices,
        const GeodesicParams& params = {});

} // namespace Geometry::Geodesic

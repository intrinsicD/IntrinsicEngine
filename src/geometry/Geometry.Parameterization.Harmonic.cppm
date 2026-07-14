module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

export module Geometry.Parameterization.Harmonic;

import Geometry.HalfedgeMesh;
import Geometry.Parameterization.Diagnostics;

export namespace Geometry::Parameterization
{
    // =========================================================================
    // Harmonic / Tutte fixed-boundary parameterization (GEOM-019)
    // =========================================================================
    //
    // Maps a connected disk-topology triangle mesh (one boundary loop and
    // Euler characteristic one) into the plane by fixing the boundary loop to
    // a convex target shape and solving a sparse SPD Laplacian system for the
    // interior vertices. Two weightings:
    //
    //   * WeightType::Cotangent — harmonic map (minimizes Dirichlet energy);
    //     cotangent weights, non-convex (negative) weights optionally clamped.
    //   * WeightType::Uniform   — Tutte barycentric embedding (graph Laplacian);
    //     always produces a valid (flip-free) embedding for a convex boundary.
    //
    // The interior system A u = b is symmetric positive definite for positive
    // weights and is solved with the geometry-owned `Geometry.Sparse` LDLT
    // factorization (the GEOM-020 SPD seam). No Eigen types are exposed here.
    //
    // This is a peer of `Geometry::Parameterization::ComputeLSCM` (free-boundary
    // conformal); the LSCM API is unchanged and remains reachable.

    enum class HarmonicWeightType : std::uint8_t
    {
        Cotangent, // harmonic (default)
        Uniform    // Tutte barycentric
    };

    enum class HarmonicBoundaryPolicy : std::uint8_t
    {
        Circle, // map boundary loop onto the unit circle (centered at 0.5,0.5; r=0.5)
        Square, // map boundary loop onto the unit square perimeter [0,1]^2
        Custom  // caller supplies boundary (and optional interior) pin UVs
    };

    enum class HarmonicStatus : std::uint8_t
    {
        Success,
        EmptyMesh,
        NotTriangleMesh,
        NotDiskTopology,        // disconnected/non-manifold, chi != 1, or boundary-loop count != 1
        DegenerateBoundary,     // boundary loop shorter than 3 vertices
        InsufficientVertices,
        MismatchedBoundaryArray,// PinnedVertices.size() != PinnedUVs.size()
        InvalidPins,            // duplicate / deleted / out-of-range / uncovered boundary
        NonFiniteBoundaryUV,
        SingularSystem,
        SolverFailed
    };

    struct HarmonicParams
    {
        HarmonicWeightType Weights{HarmonicWeightType::Cotangent};
        HarmonicBoundaryPolicy Boundary{HarmonicBoundaryPolicy::Circle};

        // Circle/Square: space boundary vertices by cumulative edge arc length
        // when true, or uniformly by vertex count when false.
        bool ArcLengthSpacing{true};

        // Clamp negative cotangent weights to zero. Keeps the system positive
        // definite (and the embedding well-behaved) on obtuse triangulations.
        bool ClampNonConvexWeights{true};

        // Custom boundary policy and/or additional interior pins. Each pinned
        // vertex index is fixed to the matching UV. For Custom, the pins must
        // cover every boundary-loop vertex.
        std::vector<std::uint32_t> PinnedVertices;
        std::vector<glm::vec2> PinnedUVs;
    };

    struct HarmonicResult
    {
        // Per-vertex UVs, size = mesh.VerticesSize(); deleted/isolated = (0,0).
        std::vector<glm::vec2> UVs;
        HarmonicStatus Status{HarmonicStatus::EmptyMesh};

        std::size_t BoundaryVertexCount{0};
        std::size_t InteriorVertexCount{0};

        // GEOM-018 distortion / flipped-element diagnostics over the result UVs.
        ParameterizationDiagnostics Diagnostics{};
    };

    [[nodiscard]] const char* ToString(HarmonicStatus status) noexcept;

    [[nodiscard]] std::optional<HarmonicResult> ComputeHarmonic(
        const HalfedgeMesh::Mesh& mesh, const HarmonicParams& params = {});
}

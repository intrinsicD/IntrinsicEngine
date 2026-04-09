module;

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include <glm/glm.hpp>

export module Geometry.VectorHeatMethod;

import Geometry.Properties;
import Geometry.HalfedgeMesh;

export namespace Geometry::VectorHeatMethod
{
    // =========================================================================
    // Vector Heat Method — Parallel Transport on Triangle Meshes
    // =========================================================================
    //
    // Implementation of Sharp, Soliman & Crane, "The Vector Heat Method"
    // (ACM Transactions on Graphics, 2019).
    //
    // Extends the scalar heat method to transport tangent vectors along
    // geodesics via a complex-valued connection Laplacian. The connection
    // Laplacian encodes how tangent frames rotate across edges, enabling:
    //
    //   1. Parallel transport of tangent vectors from source vertices
    //   2. Logarithmic map computation (geodesic polar coordinates)
    //   3. Extension of tangent vector fields defined at sparse samples
    //
    // The algorithm operates in three stages:
    //
    //   1. Build the connection Laplacian Δ_∇ (complex-valued Hermitian matrix).
    //      For each edge (i,j), the cotan weight is multiplied by a unit
    //      complex rotation exp(iρ_ij) encoding the angle between tangent
    //      bases at vertices i and j via parallel transport along the edge.
    //
    //   2. Solve the vector heat equation: (M + t·Δ_∇) X = X₀
    //      where M is the (real-valued) mass matrix, Δ_∇ is the connection
    //      Laplacian, t = h² (mean edge length squared), and X₀ is the
    //      initial tangent vector field (complex per vertex, encoded in
    //      each vertex's local tangent basis).
    //
    //   3. Normalize the result to unit magnitude and optionally recover
    //      the logarithmic map via scalar geodesic distance.
    //
    // Tangent basis convention: Each vertex has a local 2D tangent basis
    // (e1, e2) constructed from the first outgoing halfedge direction
    // projected into the tangent plane. A tangent vector at vertex i is
    // represented as the complex number (x + iy) where x is the e1
    // component and y is the e2 component.

    struct VectorHeatParams
    {
        // Time step for heat diffusion. If 0, uses h² where h is the mean
        // edge length (recommended default from the paper).
        double TimeStep{0.0};

        // CG solver tolerance.
        double SolverTolerance{1e-8};

        // CG solver maximum iterations.
        std::size_t MaxSolverIterations{2000};
    };

    struct VectorHeatResult
    {
        // Number of CG iterations used in the vector heat solve.
        std::size_t SolveIterations{0};

        // Whether the solve converged.
        bool Converged{false};

        // Per-vertex transported tangent vector in 3D (extrinsic).
        // Each vector lies in the tangent plane at its vertex.
        // Magnitude is normalized to 1 (unit vector field).
        VertexProperty<glm::vec3> TransportedVectors{};

        // Per-vertex angle of the transported vector in the local tangent
        // basis (radians). This is the connection-aware angle relative to
        // each vertex's reference direction.
        VertexProperty<double> TransportedAngles{};
    };

    struct LogarithmicMapResult
    {
        // Number of CG iterations used in the vector and scalar solves.
        std::size_t VectorSolveIterations{0};
        std::size_t ScalarSolveIterations{0};
        std::size_t PoissonSolveIterations{0};

        // Whether all solves converged.
        bool Converged{false};

        // Per-vertex 2D logarithmic map coordinates centered at the source.
        // logmap[v] = distance(source, v) * direction(source → v)
        // in the tangent plane of the source vertex.
        VertexProperty<glm::vec2> LogMapCoords{};

        // Per-vertex geodesic distance (same as scalar heat method).
        VertexProperty<double> Distance{};
    };

    // -------------------------------------------------------------------------
    // Parallel transport: transport a tangent vector from source vertices
    // -------------------------------------------------------------------------
    //
    // sourceVertices: indices of vertices where initial vectors are defined.
    // sourceVectors: 3D tangent vectors at each source (must be tangent to
    //   the surface — the component normal to the surface is projected out).
    //   Must have the same length as sourceVertices.
    //
    // Returns nullopt if the mesh is empty, has no faces, sources are empty,
    // or sourceVertices.size() != sourceVectors.size().
    [[nodiscard]] std::optional<VectorHeatResult> TransportVectors(
        Halfedge::Mesh& mesh,
        std::span<const std::size_t> sourceVertices,
        std::span<const glm::vec3> sourceVectors,
        const VectorHeatParams& params = {});

    // -------------------------------------------------------------------------
    // Logarithmic map: geodesic polar coordinates from a single source
    // -------------------------------------------------------------------------
    //
    // Computes the logarithmic map centered at sourceVertex: for each mesh
    // vertex v, returns 2D coordinates (r·cos(θ), r·sin(θ)) where r is the
    // geodesic distance and θ is the direction from source to v, measured
    // in the source's tangent plane.
    //
    // This combines scalar geodesic distance (heat method) with vector
    // parallel transport to reconstruct full polar coordinates.
    //
    // Returns nullopt if the mesh is empty or sourceVertex is invalid.
    [[nodiscard]] std::optional<LogarithmicMapResult> ComputeLogMap(
        Halfedge::Mesh& mesh,
        std::size_t sourceVertex,
        const VectorHeatParams& params = {});

} // namespace Geometry::VectorHeatMethod

module;

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

export module Geometry:DEC;

import :Properties;
import :HalfedgeMesh;

export namespace Geometry::DEC
{
    // -------------------------------------------------------------------------
    // SparseMatrix — Compressed Sparse Row (CSR) representation
    // -------------------------------------------------------------------------
    // Lightweight CSR matrix for DEC operator storage. DEC operators have
    // well-known sparsity patterns (d0, d1 have exactly 2-3 nonzeros per row;
    // Hodge stars are diagonal; the Laplacian has valence+1 per row).
    // No general-purpose solver is included — this is pure operator assembly.
    //
    // Convention: rows = domain dimension count, cols = range dimension count.
    // For d0: rows = #edges, cols = #vertices  (maps 0-forms to 1-forms)
    // For d1: rows = #faces, cols = #edges     (maps 1-forms to 2-forms)

    struct SparseMatrix
    {
        std::size_t Rows{0};
        std::size_t Cols{0};

        // CSR storage: Values[RowOffsets[i] .. RowOffsets[i+1]) are nonzeros in row i,
        //              with column indices ColIndices[k] and values Values[k].
        std::vector<std::size_t> RowOffsets;  // Size = Rows + 1
        std::vector<std::size_t> ColIndices;  // Size = nnz
        std::vector<double> Values;           // Size = nnz

        [[nodiscard]] std::size_t NonZeros() const noexcept { return Values.size(); }

        [[nodiscard]] bool IsEmpty() const noexcept { return Rows == 0 && Cols == 0; }

        // Sparse matrix-vector product: y = A * x
        // x.size() must equal Cols, y.size() must equal Rows.
        void Multiply(std::span<const double> x, std::span<double> y) const;

        // Sparse matrix-transpose-vector product: y = Aᵀ * x
        // x.size() must equal Rows, y.size() must equal Cols.
        void MultiplyTranspose(std::span<const double> x, std::span<double> y) const;
    };

    // -------------------------------------------------------------------------
    // DiagonalMatrix — for Hodge star operators (always diagonal for DEC)
    // -------------------------------------------------------------------------

    struct DiagonalMatrix
    {
        std::size_t Size{0};
        std::vector<double> Diagonal;  // Size = Size

        [[nodiscard]] bool IsEmpty() const noexcept { return Size == 0; }

        // Diagonal matrix-vector product: y_i = D_ii * x_i
        void Multiply(std::span<const double> x, std::span<double> y) const;

        // Inverse diagonal: y_i = (1 / D_ii) * x_i
        // Entries with |D_ii| < epsilon are treated as zero (y_i = 0).
        void MultiplyInverse(std::span<const double> x, std::span<double> y,
                             double epsilon = 1e-12) const;
    };

    // -------------------------------------------------------------------------
    // DECOperators — the complete set of DEC operators built from a mesh
    // -------------------------------------------------------------------------
    // These operators define the discrete exterior calculus on a triangle mesh.
    //
    // Discrete differential forms:
    //   0-forms: scalar values on vertices (e.g., temperature, height)
    //   1-forms: scalar values on edges    (e.g., flux across edge)
    //   2-forms: scalar values on faces    (e.g., density per triangle)
    //
    // Exterior derivatives (incidence matrices of the simplicial complex):
    //   d0 : Ω⁰ → Ω¹   (vertices → edges)   [#E × #V sparse matrix]
    //   d1 : Ω¹ → Ω²   (edges → faces)       [#F × #E sparse matrix]
    //
    //   d0 encodes the signed vertex-edge incidence: each row (edge) has
    //   exactly two nonzeros (+1 at the to-vertex, -1 at the from-vertex).
    //
    //   d1 encodes the signed edge-face incidence: each row (face) has
    //   entries ±1 for each edge bounding the face, sign determined by
    //   whether the edge orientation agrees with the face winding.
    //
    // Hodge stars (mass matrices — metric-dependent):
    //   ⋆0 : Ω⁰ → Ω⁰   diagonal, entry i = Voronoi area of vertex i
    //   ⋆1 : Ω¹ → Ω¹   diagonal, entry j = cotan weight of edge j
    //                     = (cot α_ij + cot β_ij) / 2
    //   ⋆2 : Ω² → Ω²   diagonal, entry k = 1 / (area of face k)
    //                     (technically area of primal / area of dual,
    //                      but for triangles: primal area / 1 = area,
    //                      dual of 2-form is 0-form scaled by 1/area)
    //
    // Laplace-Beltrami (cotan Laplacian):
    //   L = ⋆0⁻¹ d0ᵀ ⋆1 d0
    //
    //   Stored in "weak" form: L_weak = d0ᵀ ⋆1 d0  (#V × #V)
    //   The mass-normalized version is L = ⋆0⁻¹ L_weak.
    //   Users choose which form they need for their application.

    struct DECOperators
    {
        // Exterior derivatives
        SparseMatrix D0;  // #E × #V — gradient on 0-forms
        SparseMatrix D1;  // #F × #E — curl on 1-forms

        // Hodge stars (diagonal)
        DiagonalMatrix Hodge0;  // #V — Voronoi vertex areas (barycentric or mixed)
        DiagonalMatrix Hodge1;  // #E — cotan weights: (cot α + cot β) / 2
        DiagonalMatrix Hodge2;  // #F — inverse triangle area: 1 / A_f

        // Laplace-Beltrami (weak form): L = d0ᵀ ⋆1 d0
        // This is the #V × #V symmetric negative-semidefinite cotan Laplacian.
        SparseMatrix Laplacian;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return !D0.IsEmpty() && !D1.IsEmpty()
                && !Hodge0.IsEmpty() && !Hodge1.IsEmpty() && !Hodge2.IsEmpty()
                && !Laplacian.IsEmpty();
        }
    };

    // -------------------------------------------------------------------------
    // Assembly functions
    // -------------------------------------------------------------------------

    // Build the exterior derivative d0: Ω⁰ → Ω¹ (vertex → edge).
    // Uses the mesh's edge orientation convention: halfedge e*2 goes from
    // FromVertex to ToVertex, so d0[e, ToVertex] = +1, d0[e, FromVertex] = -1.
    [[nodiscard]] SparseMatrix BuildExteriorDerivative0(const Halfedge::Mesh& mesh);

    // Build the exterior derivative d1: Ω¹ → Ω² (edge → face).
    // For each face, iterates its halfedge loop. If the halfedge's canonical
    // edge orientation (halfedge index is even) agrees with the face winding,
    // the entry is +1; otherwise -1.
    [[nodiscard]] SparseMatrix BuildExteriorDerivative1(const Halfedge::Mesh& mesh);

    // Build the Hodge star ⋆0: diagonal matrix of Voronoi vertex areas.
    // Uses the mixed Voronoi area (Meyer et al., "Discrete Differential-Geometry
    // Operators for Triangulated 2-Manifolds", 2003):
    //   - For non-obtuse triangles: standard Voronoi area contribution.
    //   - For obtuse triangles at vertex i: A_f / 2.
    //   - For obtuse triangles not at vertex i: A_f / 4.
    [[nodiscard]] DiagonalMatrix BuildHodgeStar0(const Halfedge::Mesh& mesh);

    // Build the Hodge star ⋆1: diagonal matrix of cotan weights per edge.
    //   ⋆1[e] = (cot α_e + cot β_e) / 2
    // where α_e and β_e are the angles opposite edge e in its two adjacent
    // triangles. Boundary edges use only the one available angle.
    [[nodiscard]] DiagonalMatrix BuildHodgeStar1(const Halfedge::Mesh& mesh);

    // Build the Hodge star ⋆2: diagonal matrix of 1/(triangle area).
    [[nodiscard]] DiagonalMatrix BuildHodgeStar2(const Halfedge::Mesh& mesh);

    // Build the weak cotan Laplacian: L = d0ᵀ ⋆1 d0.
    // This is the standard #V × #V symmetric negative-semidefinite Laplacian.
    // Off-diagonal entry L[i,j] = -(cot α_ij + cot β_ij) / 2
    // Diagonal entry L[i,i] = -Σ_j L[i,j]
    //
    // This is assembled directly (not via matrix multiplication) for efficiency
    // and numerical robustness.
    [[nodiscard]] SparseMatrix BuildLaplacian(const Halfedge::Mesh& mesh);

    // Build all DEC operators at once.
    [[nodiscard]] DECOperators BuildOperators(const Halfedge::Mesh& mesh);

    // -------------------------------------------------------------------------
    // Conjugate Gradient Solver (Jacobi-preconditioned)
    // -------------------------------------------------------------------------
    // Solves symmetric positive-definite linear systems arising from DEC
    // operators (e.g., Poisson equations, heat diffusion).

    struct CGParams
    {
        std::size_t MaxIterations{1000};
        double Tolerance{1e-8};
    };

    struct CGResult
    {
        std::size_t Iterations{0};
        double ResidualNorm{0.0};
        bool Converged{false};
    };

    // Solve A*x = b where A is a symmetric positive-definite SparseMatrix.
    // Uses Jacobi (diagonal) preconditioning.
    // x is used as the initial guess and overwritten with the solution.
    [[nodiscard]] CGResult SolveCG(
        const SparseMatrix& A,
        std::span<const double> b,
        std::span<double> x,
        const CGParams& params = {});

    // Solve (alpha*M + beta*A)*x = b where M is diagonal, A is sparse.
    // The combined system must be symmetric positive-definite.
    // x is used as the initial guess and overwritten with the solution.
    [[nodiscard]] CGResult SolveCGShifted(
        const DiagonalMatrix& M, double alpha,
        const SparseMatrix& A, double beta,
        std::span<const double> b,
        std::span<double> x,
        const CGParams& params = {});

} // namespace Geometry::DEC

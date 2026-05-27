module;

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <span>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

module Geometry.DEC;

import Geometry.Properties;
import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.Utils;
import Geometry.Sparse;

namespace Geometry::DEC
{
    using MeshUtils::EdgeCotanWeight;
    using MeshUtils::TriangleArea;
    using MeshUtils::ComputeMixedVoronoiAreas;

    // -------------------------------------------------------------------------
    // BuildExteriorDerivative0
    // -------------------------------------------------------------------------
    // d0 is #E × #V.
    // For each edge e with canonical halfedge h = 2*e (even index):
    //   d0[e, ToVertex(h)] = +1
    //   d0[e, FromVertex(h)] = -1

    SparseMatrix BuildExteriorDerivative0(const HalfedgeMesh::Mesh& mesh)
    {
        const std::size_t nV = mesh.VerticesSize();
        const std::size_t nE = mesh.EdgesSize();

        SparseMatrix d0;
        d0.Rows = nE;
        d0.Cols = nV;
        d0.RowOffsets.resize(nE + 1);
        d0.ColIndices.reserve(2 * nE);
        d0.Values.reserve(2 * nE);

        std::size_t offset = 0;
        for (std::size_t e = 0; e < nE; ++e)
        {
            d0.RowOffsets[e] = offset;

            EdgeHandle eh{static_cast<PropertyIndex>(e)};
            if (mesh.IsDeleted(eh))
            {
                // Deleted edge: empty row
                continue;
            }

            // Canonical halfedge: even index = 2*e
            HalfedgeHandle h0{static_cast<PropertyIndex>(2u * e)};
            std::size_t vTo = mesh.ToVertex(h0).Index;
            std::size_t vFrom = mesh.FromVertex(h0).Index;

            // Store in sorted column order for CSR consistency
            if (vFrom < vTo)
            {
                d0.ColIndices.push_back(vFrom);
                d0.Values.push_back(-1.0);
                d0.ColIndices.push_back(vTo);
                d0.Values.push_back(+1.0);
            }
            else
            {
                d0.ColIndices.push_back(vTo);
                d0.Values.push_back(+1.0);
                d0.ColIndices.push_back(vFrom);
                d0.Values.push_back(-1.0);
            }

            offset += 2;
        }

        d0.RowOffsets[nE] = offset;
        return d0;
    }

    // -------------------------------------------------------------------------
    // BuildExteriorDerivative1
    // -------------------------------------------------------------------------
    // d1 is #F × #E.
    // For each face f, iterate its halfedge loop. For each halfedge h in the loop:
    //   edge index e = h >> 1
    //   If h is the canonical (even) halfedge: d1[f, e] = +1
    //   If h is the opposite (odd) halfedge:   d1[f, e] = -1

    SparseMatrix BuildExteriorDerivative1(const HalfedgeMesh::Mesh& mesh)
    {
        const std::size_t nE = mesh.EdgesSize();
        const std::size_t nF = mesh.FacesSize();

        SparseMatrix d1;
        d1.Rows = nF;
        d1.Cols = nE;

        // Triangle meshes: 3 edges per face
        d1.RowOffsets.resize(nF + 1);
        d1.ColIndices.reserve(3 * nF);
        d1.Values.reserve(3 * nF);

        std::size_t offset = 0;
        for (std::size_t f = 0; f < nF; ++f)
        {
            d1.RowOffsets[f] = offset;

            FaceHandle fh{static_cast<PropertyIndex>(f)};
            if (mesh.IsDeleted(fh))
            {
                continue;
            }

            // Collect (edgeIndex, sign) pairs for this face, then sort by column
            struct Entry
            {
                std::size_t Col;
                double Val;
            };
            std::vector<Entry> entries;
            entries.reserve(4);  // triangles have 3, quads have 4

            for (const HalfedgeHandle h : mesh.HalfedgesAroundFace(fh))
            {
                std::size_t eIdx = h.Index >> 1u;
                bool isCanonical = (h.Index & 1u) == 0;
                double sign = isCanonical ? +1.0 : -1.0;

                entries.push_back({eIdx, sign});
            }

            // Sort by column index for CSR consistency
            std::sort(entries.begin(), entries.end(),
                      [](const Entry& a, const Entry& b) { return a.Col < b.Col; });

            for (const auto& entry : entries)
            {
                d1.ColIndices.push_back(entry.Col);
                d1.Values.push_back(entry.Val);
            }

            offset += entries.size();
        }

        d1.RowOffsets[nF] = offset;
        return d1;
    }

    // -------------------------------------------------------------------------
    // BuildHodgeStar0
    // -------------------------------------------------------------------------
    // ⋆0 diagonal: mixed Voronoi area per vertex (Meyer et al., 2003).
    //
    // For each triangle, distribute area to its three vertices:
    //   - If the triangle is non-obtuse: each vertex gets its Voronoi cell area
    //     within the triangle = (1/8)(|e_opp|² cot(α_opp) + ...) for each
    //     adjacent edge.
    //   - If the triangle is obtuse at vertex i: vertex i gets A_f/2,
    //     the other two vertices each get A_f/4.

    DiagonalMatrix BuildHodgeStar0(const HalfedgeMesh::Mesh& mesh)
    {
        // Delegates to MeshUtils::ComputeMixedVoronoiAreas (Meyer et al., 2003),
        // which is shared with Curvature and Smoothing modules.
        auto areas = ComputeMixedVoronoiAreas(mesh);

        DiagonalMatrix hodge0;
        hodge0.Size = areas.size();
        hodge0.Diagonal = std::move(areas);
        return hodge0;
    }

    // -------------------------------------------------------------------------
    // BuildHodgeStar1
    // -------------------------------------------------------------------------
    // ⋆1 diagonal: cotan weight per edge.
    //   ⋆1[e] = (cot α_e + cot β_e) / 2
    // where α and β are the angles opposite edge e in the two triangles
    // sharing it. Boundary edges use only the single available angle.

    DiagonalMatrix BuildHodgeStar1(const HalfedgeMesh::Mesh& mesh)
    {
        const std::size_t nE = mesh.EdgesSize();

        DiagonalMatrix hodge1;
        hodge1.Size = nE;
        hodge1.Diagonal.assign(nE, 0.0);

        for (std::size_t ei = 0; ei < nE; ++ei)
        {
            EdgeHandle eh{static_cast<PropertyIndex>(ei)};
            if (mesh.IsDeleted(eh))
            {
                continue;
            }

            hodge1.Diagonal[ei] = EdgeCotanWeight(mesh, eh);
        }

        return hodge1;
    }

    // -------------------------------------------------------------------------
    // BuildHodgeStar1 — weighted variant
    // -------------------------------------------------------------------------

    DiagonalMatrix BuildHodgeStar1(const HalfedgeMesh::Mesh& mesh,
                                    const EdgeWeightConfig& config)
    {
        if (config.Mode == EdgeWeightMode::Cotan)
        {
            return BuildHodgeStar1(mesh);
        }

        const std::size_t nE = mesh.EdgesSize();

        DiagonalMatrix hodge1;
        hodge1.Size = nE;
        hodge1.Diagonal.assign(nE, 0.0);

        // Compute automatic time parameter if needed.
        double timeParam = config.TimeParam;
        if (config.Mode == EdgeWeightMode::HeatKernel && timeParam <= 0.0)
        {
            // Default: t = mean squared edge length
            double sumLenSq = 0.0;
            std::size_t edgeCount = 0;
            for (std::size_t ei = 0; ei < nE; ++ei)
            {
                EdgeHandle eh{static_cast<PropertyIndex>(ei)};
                if (mesh.IsDeleted(eh))
                    continue;

                HalfedgeHandle h0{static_cast<PropertyIndex>(2u * ei)};
                auto p0 = glm::dvec3(mesh.Position(mesh.FromVertex(h0)));
                auto p1 = glm::dvec3(mesh.Position(mesh.ToVertex(h0)));
                auto d = p1 - p0;
                sumLenSq += glm::dot(d, d);
                ++edgeCount;
            }
            timeParam = (edgeCount > 0) ? (sumLenSq / static_cast<double>(edgeCount)) : 1.0;
        }

        // Assemble weights per edge
        for (std::size_t ei = 0; ei < nE; ++ei)
        {
            EdgeHandle eh{static_cast<PropertyIndex>(ei)};
            if (mesh.IsDeleted(eh))
                continue;

            HalfedgeHandle h0{static_cast<PropertyIndex>(2u * ei)};
            auto p0 = glm::dvec3(mesh.Position(mesh.FromVertex(h0)));
            auto p1 = glm::dvec3(mesh.Position(mesh.ToVertex(h0)));
            auto d = p1 - p0;
            double distSq = glm::dot(d, d);

            // w_ij = exp(-||p_i - p_j||² / (4t))
            hodge1.Diagonal[ei] = std::exp(-distSq / (4.0 * timeParam));
        }

        return hodge1;
    }

    // -------------------------------------------------------------------------
    // BuildHodgeStar2
    // -------------------------------------------------------------------------
    // ⋆2 diagonal: 1 / (area of face).

    DiagonalMatrix BuildHodgeStar2(const HalfedgeMesh::Mesh& mesh)
    {
        const std::size_t nF = mesh.FacesSize();

        DiagonalMatrix hodge2;
        hodge2.Size = nF;
        hodge2.Diagonal.assign(nF, 0.0);

        for (std::size_t fi = 0; fi < nF; ++fi)
        {
            FaceHandle fh{static_cast<PropertyIndex>(fi)};
            MeshUtils::TriangleFaceView tri{};
            if (!MeshUtils::TryGetTriangleFaceView(mesh, fh, tri))
            {
                continue;
            }

            double area = TriangleArea(tri.P0, tri.P1, tri.P2);

            if (area > 1e-12)
            {
                hodge2.Diagonal[fi] = 1.0 / area;
            }
        }

        return hodge2;
    }

    // -------------------------------------------------------------------------
    // AssembleLaplacianFromWeights — shared CSR assembly logic
    // -------------------------------------------------------------------------
    // Builds L from a pre-computed diagonal edge weight matrix:
    //   L[i,j] = -w_e for edge (i,j), L[i,i] = Σ_j w_{ij}
    // The result is symmetric, with zero row sums and non-positive off-diagonal.

    static SparseMatrix AssembleLaplacianFromWeights(
        const HalfedgeMesh::Mesh& mesh, const DiagonalMatrix& weights)
    {
        const std::size_t nV = mesh.VerticesSize();

        std::vector<std::size_t> rowNnz(nV, 0);

        for (std::size_t vi = 0; vi < nV; ++vi)
        {
            VertexHandle vh{static_cast<PropertyIndex>(vi)};
            if (mesh.IsDeleted(vh) || mesh.IsIsolated(vh))
                continue;
            rowNnz[vi] = 1;  // diagonal
            for (const HalfedgeHandle h : mesh.HalfedgesAroundVertex(vh))
            {
                (void)h;
                rowNnz[vi] += 1;
            }
        }

        SparseMatrix L;
        L.Rows = nV;
        L.Cols = nV;
        L.RowOffsets.resize(nV + 1);
        L.RowOffsets[0] = 0;
        for (std::size_t i = 0; i < nV; ++i)
            L.RowOffsets[i + 1] = L.RowOffsets[i] + rowNnz[i];

        std::size_t totalNnz = L.RowOffsets[nV];
        L.ColIndices.resize(totalNnz, 0);
        L.Values.resize(totalNnz, 0.0);

        for (std::size_t vi = 0; vi < nV; ++vi)
        {
            VertexHandle vh{static_cast<PropertyIndex>(vi)};
            if (mesh.IsDeleted(vh) || mesh.IsIsolated(vh))
                continue;

            struct Entry
            {
                std::size_t Col;
                double Val;
            };
            std::vector<Entry> entries;
            entries.reserve(rowNnz[vi]);

            double diagSum = 0.0;

            for (const HalfedgeHandle h : mesh.HalfedgesAroundVertex(vh))
            {
                EdgeHandle e = mesh.Edge(h);
                double w = weights.Diagonal[e.Index];

                VertexHandle vOther = mesh.ToVertex(h);
                entries.push_back({vOther.Index, -w});
                diagSum += w;
            }

            entries.push_back({vi, diagSum});

            std::sort(entries.begin(), entries.end(),
                      [](const Entry& a, const Entry& b) { return a.Col < b.Col; });

            std::size_t base = L.RowOffsets[vi];
            for (std::size_t k = 0; k < entries.size(); ++k)
            {
                L.ColIndices[base + k] = entries[k].Col;
                L.Values[base + k] = entries[k].Val;
            }
        }

        return L;
    }

    // -------------------------------------------------------------------------
    // BuildLaplacian
    // -------------------------------------------------------------------------
    // Weak cotan Laplacian: L = d0ᵀ ⋆1 d0  (#V × #V).
    // Convention: L * 1 = 0 (constant functions are in the kernel).

    SparseMatrix BuildLaplacian(const HalfedgeMesh::Mesh& mesh)
    {
        DiagonalMatrix hodge1 = BuildHodgeStar1(mesh);
        return AssembleLaplacianFromWeights(mesh, hodge1);
    }

    // -------------------------------------------------------------------------
    // BuildLaplacian — weighted variant
    // -------------------------------------------------------------------------

    SparseMatrix BuildLaplacian(const HalfedgeMesh::Mesh& mesh,
                                 const EdgeWeightConfig& config)
    {
        if (config.Mode == EdgeWeightMode::Cotan)
        {
            return BuildLaplacian(mesh);
        }

        DiagonalMatrix weights = BuildHodgeStar1(mesh, config);
        return AssembleLaplacianFromWeights(mesh, weights);
    }

    // -------------------------------------------------------------------------
    // BuildOperators
    // -------------------------------------------------------------------------

    DECOperators BuildOperators(const HalfedgeMesh::Mesh& mesh)
    {
        DECOperators ops;

        ops.D0 = BuildExteriorDerivative0(mesh);
        ops.D1 = BuildExteriorDerivative1(mesh);
        ops.Hodge0 = BuildHodgeStar0(mesh);
        ops.Hodge1 = BuildHodgeStar1(mesh);
        ops.Hodge2 = BuildHodgeStar2(mesh);
        ops.Laplacian = BuildLaplacian(mesh);

        return ops;
    }

    DECOperators BuildOperators(const HalfedgeMesh::Mesh& mesh,
                                 const EdgeWeightConfig& config)
    {
        if (config.Mode == EdgeWeightMode::Cotan)
        {
            return BuildOperators(mesh);
        }

        DECOperators ops;

        // Topology-only operators are weight-independent
        ops.D0 = BuildExteriorDerivative0(mesh);
        ops.D1 = BuildExteriorDerivative1(mesh);

        // Area-based Hodge stars are metric-dependent but not edge-weight-dependent
        ops.Hodge0 = BuildHodgeStar0(mesh);
        ops.Hodge2 = BuildHodgeStar2(mesh);

        // Weight-dependent operators
        ops.Hodge1 = BuildHodgeStar1(mesh, config);
        ops.Laplacian = BuildLaplacian(mesh, config);

        return ops;
    }

    // -------------------------------------------------------------------------
    // Conjugate Gradient Solver — Jacobi-preconditioned
    // -------------------------------------------------------------------------
    //
    // Standard preconditioned CG (Hestenes-Stiefel, 1952) with diagonal
    // (Jacobi) preconditioning. Convergence for well-conditioned SPD systems
    // from DEC operators (Laplacian, shifted heat operator) is typically
    // achieved in O(√κ) iterations where κ is the condition number.

    CGResult SolveCG(
        const SparseMatrix& A,
        std::span<const double> b,
        std::span<double> x,
        const CGParams& params)
    {
        return Geometry::Sparse::SolveCG(A, b, x, params);
    }

    CGResult SolveCGShifted(
        const DiagonalMatrix& M, double alpha,
        const SparseMatrix& A, double beta,
        std::span<const double> b,
        std::span<double> x,
        const CGParams& params)
    {
        return Geometry::Sparse::SolveCGShifted(M, alpha, A, beta, b, x, params);
    }

    // -------------------------------------------------------------------------
    // BuildLaplacianCache
    // -------------------------------------------------------------------------

    // -------------------------------------------------------------------------
    // PopulateLaplacianCache — shared logic for derived matrix forms
    // -------------------------------------------------------------------------
    // Given a LaplacianCache with Operators already set, computes the
    // derived forms: MassInverse, MassSqrtInverse, SymmetricNormalizedLaplacian.

    static void PopulateLaplacianCacheDerived(LaplacianCache& cache)
    {
        const auto& hodge0 = cache.Operators.Hodge0;
        const std::size_t nV = hodge0.Size;

        // MassInverse: ⋆0⁻¹[i] = 1 / ⋆0[i]
        cache.MassInverse.Size = nV;
        cache.MassInverse.Diagonal.resize(nV);
        for (std::size_t i = 0; i < nV; ++i)
        {
            double a = hodge0.Diagonal[i];
            cache.MassInverse.Diagonal[i] = (std::abs(a) > 1e-12) ? (1.0 / a) : 0.0;
        }

        // MassSqrtInverse: ⋆0^{-1/2}[i] = 1 / sqrt(⋆0[i])
        cache.MassSqrtInverse.Size = nV;
        cache.MassSqrtInverse.Diagonal.resize(nV);
        for (std::size_t i = 0; i < nV; ++i)
        {
            double a = hodge0.Diagonal[i];
            cache.MassSqrtInverse.Diagonal[i] = (a > 1e-12) ? (1.0 / std::sqrt(a)) : 0.0;
        }

        // SymmetricNormalizedLaplacian: L_sym = D^{-1/2} L D^{-1/2}
        // where D = ⋆0 (diagonal mass matrix).
        // L_sym[i,j] = L[i,j] / sqrt(⋆0[i] * ⋆0[j])
        const auto& L = cache.Operators.Laplacian;
        auto& Lsym = cache.SymmetricNormalizedLaplacian;
        Lsym.Rows = L.Rows;
        Lsym.Cols = L.Cols;
        Lsym.RowOffsets = L.RowOffsets;  // Same sparsity pattern
        Lsym.ColIndices = L.ColIndices;
        Lsym.Values.resize(L.Values.size());

        const auto& dInvSqrt = cache.MassSqrtInverse.Diagonal;
        for (std::size_t i = 0; i < L.Rows; ++i)
        {
            double di = dInvSqrt[i];
            for (std::size_t k = L.RowOffsets[i]; k < L.RowOffsets[i + 1]; ++k)
            {
                std::size_t j = L.ColIndices[k];
                double dj = dInvSqrt[j];
                Lsym.Values[k] = L.Values[k] * di * dj;
            }
        }
    }

    LaplacianCache BuildLaplacianCache(const HalfedgeMesh::Mesh& mesh)
    {
        LaplacianCache cache;
        cache.Operators = BuildOperators(mesh);
        if (!cache.Operators.IsValid())
            return cache;
        PopulateLaplacianCacheDerived(cache);
        return cache;
    }

    LaplacianCache BuildLaplacianCache(const HalfedgeMesh::Mesh& mesh,
                                        const EdgeWeightConfig& config)
    {
        if (config.Mode == EdgeWeightMode::Cotan)
        {
            return BuildLaplacianCache(mesh);
        }

        LaplacianCache cache;
        cache.Operators = BuildOperators(mesh, config);
        if (!cache.Operators.IsValid())
            return cache;
        PopulateLaplacianCacheDerived(cache);
        return cache;
    }

    // -------------------------------------------------------------------------
    // AnalyzeLaplacian
    // -------------------------------------------------------------------------

    LaplacianDiagnostics AnalyzeLaplacian(const SparseMatrix& L, double tolerance)
    {
        LaplacianDiagnostics diag;

        if (L.IsEmpty() || L.Rows != L.Cols)
            return diag;

        const std::size_t n = L.Rows;

        // Build a quick lookup: for each (i,j), find the value.
        // We'll iterate the CSR structure.

        // 1. Symmetry check: for each L[i,j], verify L[j,i] exists and matches.
        diag.IsSymmetric = true;
        diag.MaxSymmetryError = 0.0;

        for (std::size_t i = 0; i < n; ++i)
        {
            for (std::size_t k = L.RowOffsets[i]; k < L.RowOffsets[i + 1]; ++k)
            {
                std::size_t j = L.ColIndices[k];
                double lij = L.Values[k];

                // Find L[j,i]
                double lji = 0.0;
                bool found = false;
                for (std::size_t m = L.RowOffsets[j]; m < L.RowOffsets[j + 1]; ++m)
                {
                    if (L.ColIndices[m] == i)
                    {
                        lji = L.Values[m];
                        found = true;
                        break;
                    }
                }

                double err = found ? std::abs(lij - lji) : std::abs(lij);
                diag.MaxSymmetryError = std::max(diag.MaxSymmetryError, err);
                if (err > tolerance)
                    diag.IsSymmetric = false;
            }
        }

        // 2. Row sum check: each row should sum to zero.
        diag.HasZeroRowSums = true;
        diag.MaxRowSumError = 0.0;

        for (std::size_t i = 0; i < n; ++i)
        {
            double rowSum = 0.0;
            for (std::size_t k = L.RowOffsets[i]; k < L.RowOffsets[i + 1]; ++k)
                rowSum += L.Values[k];

            double err = std::abs(rowSum);
            diag.MaxRowSumError = std::max(diag.MaxRowSumError, err);
            if (err > tolerance)
                diag.HasZeroRowSums = false;
        }

        // 3. Off-diagonal non-positive and diagonal positive checks.
        diag.HasNonPositiveOffDiag = true;
        diag.HasPositiveDiagonal = true;
        diag.MaxOffDiagPositive = 0.0;

        for (std::size_t i = 0; i < n; ++i)
        {
            bool hasDiag = false;
            for (std::size_t k = L.RowOffsets[i]; k < L.RowOffsets[i + 1]; ++k)
            {
                std::size_t j = L.ColIndices[k];
                double val = L.Values[k];

                if (j == i)
                {
                    hasDiag = true;
                    if (val < -tolerance)
                        diag.HasPositiveDiagonal = false;
                }
                else
                {
                    if (val > tolerance)
                    {
                        diag.HasNonPositiveOffDiag = false;
                        diag.MaxOffDiagPositive = std::max(diag.MaxOffDiagPositive, val);
                    }
                }
            }

            // Empty row (deleted/isolated vertex): skip diagonal check
            if (!hasDiag && L.RowOffsets[i] < L.RowOffsets[i + 1])
                diag.HasPositiveDiagonal = false;
        }

        // 4. Diagonal dominance: |L[i,i]| >= Σ_{j≠i} |L[i,j]|
        diag.IsDiagonallyDominant = true;
        for (std::size_t i = 0; i < n; ++i)
        {
            double diagVal = 0.0;
            double offDiagSum = 0.0;

            for (std::size_t k = L.RowOffsets[i]; k < L.RowOffsets[i + 1]; ++k)
            {
                if (L.ColIndices[k] == i)
                    diagVal = std::abs(L.Values[k]);
                else
                    offDiagSum += std::abs(L.Values[k]);
            }

            if (diagVal + tolerance < offDiagSum)
                diag.IsDiagonallyDominant = false;
        }

        return diag;
    }

} // namespace Geometry::DEC

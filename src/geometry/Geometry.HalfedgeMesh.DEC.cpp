module;

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <map>
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
    using MeshUtils::VertexNormal;

    namespace
    {
        // Clamped cotangent of the apex angle opposite halfedge h (the angle in
        // h's triangle facing h's edge), Heron/metric form. Boundary/deleted
        // halfedges and degenerate/non-finite triangles fail closed to 0. This
        // mirrors MeshUtils::ClampedHalfedgeCotan but stays const-friendly for the
        // assembly path (which does not mutate the mesh / publish properties).
        [[nodiscard]] double ClampedApexCotan(const HalfedgeMesh::Mesh& mesh,
                                              HalfedgeHandle h, double bound)
        {
            if (mesh.IsDeleted(h) || mesh.IsBoundary(h))
                return 0.0;
            const glm::dvec3 pFrom(mesh.Position(mesh.FromVertex(h)));
            const glm::dvec3 pTo(mesh.Position(mesh.ToVertex(h)));
            const glm::dvec3 pApex(mesh.Position(mesh.ToVertex(mesh.NextHalfedge(h))));
            const auto finite = [](const glm::dvec3& p) {
                return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
            };
            if (!finite(pFrom) || !finite(pTo) || !finite(pApex))
                return 0.0;
            const double cSq = glm::dot(pTo - pFrom, pTo - pFrom);
            const double aSq = glm::dot(pApex - pTo, pApex - pTo);
            const double bSq = glm::dot(pApex - pFrom, pApex - pFrom);
            const double area = 0.5 * glm::length(glm::cross(pFrom - pApex, pTo - pApex));
            if (area <= 1e-12)
                return 0.0;
            return std::clamp((aSq + bSq - cSq) / (4.0 * area), -bound, bound);
        }

        // Clamped (cot α + cot β)/2 edge weight built from the two halfedge apex
        // cotans — the feature-aware ModifiedNormal cotan term.
        [[nodiscard]] double ClampedEdgeCotanWeight(const HalfedgeMesh::Mesh& mesh,
                                                    EdgeHandle e, double bound)
        {
            const HalfedgeHandle h0{static_cast<PropertyIndex>(2u * e.Index)};
            const HalfedgeHandle h1 = mesh.OppositeHalfedge(h0);
            return 0.5 * (ClampedApexCotan(mesh, h0, bound) + ClampedApexCotan(mesh, h1, bound));
        }
    }

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
    // BuildConsistentMass — full Galerkin (consistent) mass matrix
    // -------------------------------------------------------------------------
    // Per triangle of area A, the linear-FEM mass matrix is
    //   (A/12) * [[2,1,1],[1,2,1],[1,1,2]]
    // assembled over all triangles. Symmetric and SPD. Degenerate / non-finite
    // triangles are skipped (fail closed) — area <= tol or NaN area drops out.

    SparseMatrix BuildConsistentMass(const HalfedgeMesh::Mesh& mesh)
    {
        const std::size_t nV = mesh.VerticesSize();
        std::vector<std::map<std::size_t, double>> rows(nV);

        for (std::size_t fi = 0; fi < mesh.FacesSize(); ++fi)
        {
            FaceHandle fh{static_cast<PropertyIndex>(fi)};
            MeshUtils::TriangleFaceView tri{};
            if (!MeshUtils::TryGetTriangleFaceView(mesh, fh, tri))
            {
                continue;
            }
            const double area = TriangleArea(tri.P0, tri.P1, tri.P2);
            if (!(area > 1e-12)) // rejects zero-area and non-finite (NaN) areas
            {
                continue;
            }
            const std::array<std::size_t, 3> idx{tri.V0.Index, tri.V1.Index, tri.V2.Index};
            const double off = area / 12.0;   // M_ij, i != j
            const double diag = area / 6.0;   // M_ii = 2 * A/12
            for (std::size_t a = 0; a < 3; ++a)
            {
                for (std::size_t b = 0; b < 3; ++b)
                {
                    rows[idx[a]][idx[b]] += (a == b) ? diag : off;
                }
            }
        }

        SparseMatrix M;
        M.Rows = nV;
        M.Cols = nV;
        M.RowOffsets.resize(nV + 1);
        M.RowOffsets[0] = 0;
        for (std::size_t i = 0; i < nV; ++i)
        {
            M.RowOffsets[i + 1] = M.RowOffsets[i] + rows[i].size();
        }
        const std::size_t nnz = M.RowOffsets[nV];
        M.ColIndices.resize(nnz);
        M.Values.resize(nnz);
        for (std::size_t i = 0; i < nV; ++i)
        {
            std::size_t k = M.RowOffsets[i];
            for (const auto& [col, val] : rows[i]) // std::map keeps columns sorted
            {
                M.ColIndices[k] = col;
                M.Values[k] = val;
                ++k;
            }
        }
        return M;
    }

    // -------------------------------------------------------------------------
    // BuildHodgeStar0 — mass-mode variant
    // -------------------------------------------------------------------------

    DiagonalMatrix BuildHodgeStar0(const HalfedgeMesh::Mesh& mesh, MassMode mode)
    {
        if (mode == MassMode::Voronoi)
        {
            return BuildHodgeStar0(mesh); // byte-identical to the default path
        }

        const std::size_t nV = mesh.VerticesSize();

        if (mode == MassMode::Sum || mode == MassMode::Galerkin)
        {
            // Lumped mass = row-sum of the consistent (Galerkin) mass matrix.
            const SparseMatrix M = BuildConsistentMass(mesh);
            DiagonalMatrix hodge0;
            hodge0.Size = nV;
            hodge0.Diagonal.assign(nV, 0.0);
            for (std::size_t i = 0; i < nV; ++i)
            {
                double sum = 0.0;
                for (std::size_t k = M.RowOffsets[i]; k < M.RowOffsets[i + 1]; ++k)
                {
                    sum += M.Values[k];
                }
                hodge0.Diagonal[i] = sum;
            }
            return hodge0;
        }

        // Barycentric: each vertex receives one third of each incident triangle.
        DiagonalMatrix hodge0;
        hodge0.Size = nV;
        hodge0.Diagonal.assign(nV, 0.0);
        for (std::size_t fi = 0; fi < mesh.FacesSize(); ++fi)
        {
            FaceHandle fh{static_cast<PropertyIndex>(fi)};
            MeshUtils::TriangleFaceView tri{};
            if (!MeshUtils::TryGetTriangleFaceView(mesh, fh, tri))
            {
                continue;
            }
            const double area = TriangleArea(tri.P0, tri.P1, tri.P2);
            if (!(area > 1e-12))
            {
                continue;
            }
            const double third = area / 3.0;
            hodge0.Diagonal[tri.V0.Index] += third;
            hodge0.Diagonal[tri.V1.Index] += third;
            hodge0.Diagonal[tri.V2.Index] += third;
        }
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

        // Compute automatic time parameter if needed (HeatKernel only).
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

        const double featureExp = std::max(0.0, config.FeatureExponent);

        // Assemble weights per edge per mode (all fail closed to 0).
        for (std::size_t ei = 0; ei < nE; ++ei)
        {
            EdgeHandle eh{static_cast<PropertyIndex>(ei)};
            if (mesh.IsDeleted(eh))
                continue;

            HalfedgeHandle h0{static_cast<PropertyIndex>(2u * ei)};
            const glm::dvec3 p0 = glm::dvec3(mesh.Position(mesh.FromVertex(h0)));
            const glm::dvec3 p1 = glm::dvec3(mesh.Position(mesh.ToVertex(h0)));
            const glm::dvec3 d = p1 - p0;
            const double distSq = glm::dot(d, d);

            double w = 0.0;
            switch (config.Mode)
            {
            case EdgeWeightMode::HeatKernel:
                // w_ij = exp(-||p_i - p_j||² / (4t))
                w = std::exp(-distSq / (4.0 * timeParam));
                break;
            case EdgeWeightMode::Graph:
                // w_ij = 1 (combinatorial)
                w = 1.0;
                break;
            case EdgeWeightMode::Fujiwara:
            {
                // w_ij = 1 / ||p_i - p_j||, fail closed on degenerate length.
                const double len = std::sqrt(distSq);
                w = (std::isfinite(len) && len > 1e-12) ? (1.0 / len) : 0.0;
                break;
            }
            case EdgeWeightMode::ModifiedNormal:
            {
                // w_ij = (cot α + cot β)/2 · |n_i · n_j|^featureExp
                const double cotw = ClampedEdgeCotanWeight(mesh, eh, MeshUtils::kHalfedgeCotanClamp);
                const glm::dvec3 ni = glm::dvec3(VertexNormal(mesh, mesh.FromVertex(h0)));
                const glm::dvec3 nj = glm::dvec3(VertexNormal(mesh, mesh.ToVertex(h0)));
                const double li = glm::length(ni);
                const double lj = glm::length(nj);
                double feature = 0.0;
                if (li > 1e-12 && lj > 1e-12)
                {
                    const double dotN = std::abs(glm::dot(ni / li, nj / lj));
                    feature = (featureExp == 1.0) ? dotN : std::pow(dotN, featureExp);
                }
                w = cotw * feature;
                break;
            }
            case EdgeWeightMode::Cotan:
            default:
                // Cotan handled by the early return above; defensive no-op.
                w = 0.0;
                break;
            }

            hodge1.Diagonal[ei] = std::isfinite(w) ? w : 0.0;
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
        // Fully-default config reproduces the unparameterized operators exactly.
        if (config.Mode == EdgeWeightMode::Cotan && config.Mass == MassMode::Voronoi)
        {
            return BuildOperators(mesh);
        }

        DECOperators ops;

        // Topology-only operators are weight-independent.
        ops.D0 = BuildExteriorDerivative0(mesh);
        ops.D1 = BuildExteriorDerivative1(mesh);
        ops.Hodge2 = BuildHodgeStar2(mesh);

        // Stiffness (edge-weight-dependent) operators.
        if (config.Mode == EdgeWeightMode::Cotan)
        {
            ops.Hodge1 = BuildHodgeStar1(mesh);
            ops.Laplacian = BuildLaplacian(mesh);
        }
        else
        {
            ops.Hodge1 = BuildHodgeStar1(mesh, config);
            ops.Laplacian = BuildLaplacian(mesh, config);
        }

        // Mass (Hodge0) operators. Galerkin additionally populates the consistent
        // mass matrix and lumps its row-sums into the diagonal Hodge0.
        if (config.Mass == MassMode::Galerkin)
        {
            ops.ConsistentMass = BuildConsistentMass(mesh);
        }
        ops.Hodge0 = BuildHodgeStar0(mesh, config.Mass);

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
        if (config.Mode == EdgeWeightMode::Cotan && config.Mass == MassMode::Voronoi)
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

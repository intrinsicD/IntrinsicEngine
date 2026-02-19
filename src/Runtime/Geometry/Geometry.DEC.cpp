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

module Geometry:DEC.Impl;

import :DEC;
import :Properties;
import :HalfedgeMesh;
import :MeshUtils;

namespace Geometry::DEC
{
    using MeshUtils::Cotan;
    using MeshUtils::TriangleArea;

    // -------------------------------------------------------------------------
    // SparseMatrix operations
    // -------------------------------------------------------------------------

    void SparseMatrix::Multiply(std::span<const double> x, std::span<double> y) const
    {
        assert(x.size() >= Cols);
        assert(y.size() >= Rows);

        for (std::size_t i = 0; i < Rows; ++i)
        {
            double sum = 0.0;
            for (std::size_t k = RowOffsets[i]; k < RowOffsets[i + 1]; ++k)
            {
                sum += Values[k] * x[ColIndices[k]];
            }
            y[i] = sum;
        }
    }

    void SparseMatrix::MultiplyTranspose(std::span<const double> x, std::span<double> y) const
    {
        assert(x.size() >= Rows);
        assert(y.size() >= Cols);

        // Zero the output first
        for (std::size_t j = 0; j < Cols; ++j)
        {
            y[j] = 0.0;
        }

        for (std::size_t i = 0; i < Rows; ++i)
        {
            for (std::size_t k = RowOffsets[i]; k < RowOffsets[i + 1]; ++k)
            {
                y[ColIndices[k]] += Values[k] * x[i];
            }
        }
    }

    // -------------------------------------------------------------------------
    // DiagonalMatrix operations
    // -------------------------------------------------------------------------

    void DiagonalMatrix::Multiply(std::span<const double> x, std::span<double> y) const
    {
        assert(x.size() >= Size);
        assert(y.size() >= Size);

        for (std::size_t i = 0; i < Size; ++i)
        {
            y[i] = Diagonal[i] * x[i];
        }
    }

    void DiagonalMatrix::MultiplyInverse(std::span<const double> x, std::span<double> y,
                                          double epsilon) const
    {
        assert(x.size() >= Size);
        assert(y.size() >= Size);

        for (std::size_t i = 0; i < Size; ++i)
        {
            if (std::abs(Diagonal[i]) < epsilon)
            {
                y[i] = 0.0;
            }
            else
            {
                y[i] = x[i] / Diagonal[i];
            }
        }
    }

    // -------------------------------------------------------------------------
    // BuildExteriorDerivative0
    // -------------------------------------------------------------------------
    // d0 is #E × #V.
    // For each edge e with canonical halfedge h = 2*e (even index):
    //   d0[e, ToVertex(h)] = +1
    //   d0[e, FromVertex(h)] = -1

    SparseMatrix BuildExteriorDerivative0(const Halfedge::Mesh& mesh)
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

    SparseMatrix BuildExteriorDerivative1(const Halfedge::Mesh& mesh)
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

            HalfedgeHandle hStart = mesh.Halfedge(fh);
            HalfedgeHandle h = hStart;
            do
            {
                std::size_t eIdx = h.Index >> 1u;
                bool isCanonical = (h.Index & 1u) == 0;
                double sign = isCanonical ? +1.0 : -1.0;

                entries.push_back({eIdx, sign});
                h = mesh.NextHalfedge(h);
            } while (h != hStart);

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

    DiagonalMatrix BuildHodgeStar0(const Halfedge::Mesh& mesh)
    {
        const std::size_t nV = mesh.VerticesSize();
        const std::size_t nF = mesh.FacesSize();

        DiagonalMatrix hodge0;
        hodge0.Size = nV;
        hodge0.Diagonal.assign(nV, 0.0);

        for (std::size_t fi = 0; fi < nF; ++fi)
        {
            FaceHandle fh{static_cast<PropertyIndex>(fi)};
            if (mesh.IsDeleted(fh))
            {
                continue;
            }

            // Get the three vertices of this triangle
            HalfedgeHandle h0 = mesh.Halfedge(fh);
            HalfedgeHandle h1 = mesh.NextHalfedge(h0);
            HalfedgeHandle h2 = mesh.NextHalfedge(h1);

            VertexHandle va = mesh.ToVertex(h0);
            VertexHandle vb = mesh.ToVertex(h1);
            VertexHandle vc = mesh.ToVertex(h2);

            glm::vec3 pa = mesh.Position(va);
            glm::vec3 pb = mesh.Position(vb);
            glm::vec3 pc = mesh.Position(vc);

            // Edge vectors from each vertex
            glm::vec3 eAB = pb - pa;
            glm::vec3 eAC = pc - pa;
            glm::vec3 eBC = pc - pb;

            double area = TriangleArea(pa, pb, pc);
            if (area < 1e-12)
            {
                continue;  // Degenerate triangle
            }

            // Check for obtuse angles
            double dotA = static_cast<double>(glm::dot(eAB, eAC));  // angle at A
            double dotB = static_cast<double>(glm::dot(-eAB, eBC));  // angle at B
            double dotC = static_cast<double>(glm::dot(-eAC, -eBC)); // angle at C

            if (dotA < 0.0)
            {
                // Obtuse at A
                hodge0.Diagonal[va.Index] += area / 2.0;
                hodge0.Diagonal[vb.Index] += area / 4.0;
                hodge0.Diagonal[vc.Index] += area / 4.0;
            }
            else if (dotB < 0.0)
            {
                // Obtuse at B
                hodge0.Diagonal[va.Index] += area / 4.0;
                hodge0.Diagonal[vb.Index] += area / 2.0;
                hodge0.Diagonal[vc.Index] += area / 4.0;
            }
            else if (dotC < 0.0)
            {
                // Obtuse at C
                hodge0.Diagonal[va.Index] += area / 4.0;
                hodge0.Diagonal[vb.Index] += area / 4.0;
                hodge0.Diagonal[vc.Index] += area / 2.0;
            }
            else
            {
                // Non-obtuse: Voronoi area per vertex
                // Voronoi area at vertex X = (1/8) Σ (|e|² cot(opposite_angle))
                // for the two edges adjacent to X in this triangle.

                double cotA = Cotan(eAB, eAC);
                double cotB = Cotan(-eAB, eBC);
                double cotC = Cotan(-eAC, -eBC);

                double lenSqAB = static_cast<double>(glm::dot(eAB, eAB));
                double lenSqAC = static_cast<double>(glm::dot(eAC, eAC));
                double lenSqBC = static_cast<double>(glm::dot(eBC, eBC));

                // At vertex A: edges AB and AC; opposite angles are C and B
                hodge0.Diagonal[va.Index] += (lenSqAB * cotC + lenSqAC * cotB) / 8.0;
                // At vertex B: edges AB and BC; opposite angles are C and A
                hodge0.Diagonal[vb.Index] += (lenSqAB * cotC + lenSqBC * cotA) / 8.0;
                // At vertex C: edges AC and BC; opposite angles are B and A
                hodge0.Diagonal[vc.Index] += (lenSqAC * cotB + lenSqBC * cotA) / 8.0;
            }
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

    DiagonalMatrix BuildHodgeStar1(const Halfedge::Mesh& mesh)
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

            // Two halfedges of this edge
            HalfedgeHandle h0{static_cast<PropertyIndex>(2u * ei)};
            HalfedgeHandle h1 = mesh.OppositeHalfedge(h0);

            double cotSum = 0.0;

            // Halfedge h0 side: if not boundary, compute cotan of opposite angle
            if (!mesh.IsBoundary(h0))
            {
                // The opposite vertex is the one across from this edge in the face.
                // For halfedge h0: from->to is the edge. The opposite vertex is
                // NextHalfedge(h0)->ToVertex.
                VertexHandle vOpp = mesh.ToVertex(mesh.NextHalfedge(h0));
                VertexHandle vFrom = mesh.FromVertex(h0);
                VertexHandle vTo = mesh.ToVertex(h0);

                glm::vec3 u = mesh.Position(vFrom) - mesh.Position(vOpp);
                glm::vec3 v = mesh.Position(vTo) - mesh.Position(vOpp);

                cotSum += Cotan(u, v);
            }

            // Halfedge h1 side
            if (!mesh.IsBoundary(h1))
            {
                VertexHandle vOpp = mesh.ToVertex(mesh.NextHalfedge(h1));
                VertexHandle vFrom = mesh.FromVertex(h1);
                VertexHandle vTo = mesh.ToVertex(h1);

                glm::vec3 u = mesh.Position(vFrom) - mesh.Position(vOpp);
                glm::vec3 v = mesh.Position(vTo) - mesh.Position(vOpp);

                cotSum += Cotan(u, v);
            }

            hodge1.Diagonal[ei] = cotSum / 2.0;
        }

        return hodge1;
    }

    // -------------------------------------------------------------------------
    // BuildHodgeStar2
    // -------------------------------------------------------------------------
    // ⋆2 diagonal: 1 / (area of face).

    DiagonalMatrix BuildHodgeStar2(const Halfedge::Mesh& mesh)
    {
        const std::size_t nF = mesh.FacesSize();

        DiagonalMatrix hodge2;
        hodge2.Size = nF;
        hodge2.Diagonal.assign(nF, 0.0);

        for (std::size_t fi = 0; fi < nF; ++fi)
        {
            FaceHandle fh{static_cast<PropertyIndex>(fi)};
            if (mesh.IsDeleted(fh))
            {
                continue;
            }

            HalfedgeHandle h0 = mesh.Halfedge(fh);
            HalfedgeHandle h1 = mesh.NextHalfedge(h0);
            HalfedgeHandle h2 = mesh.NextHalfedge(h1);

            glm::vec3 pa = mesh.Position(mesh.ToVertex(h0));
            glm::vec3 pb = mesh.Position(mesh.ToVertex(h1));
            glm::vec3 pc = mesh.Position(mesh.ToVertex(h2));

            double area = TriangleArea(pa, pb, pc);

            if (area > 1e-12)
            {
                hodge2.Diagonal[fi] = 1.0 / area;
            }
        }

        return hodge2;
    }

    // -------------------------------------------------------------------------
    // BuildLaplacian
    // -------------------------------------------------------------------------
    // Weak cotan Laplacian: L = d0ᵀ ⋆1 d0  (#V × #V).
    //
    // Assembled directly for efficiency. For each edge e = (i, j):
    //   w_e = (cot α_e + cot β_e) / 2  (the Hodge star 1 weight)
    //   L[i,j] += -w_e
    //   L[j,i] += -w_e
    //   L[i,i] += w_e
    //   L[j,j] += w_e
    //
    // The result is symmetric negative-semidefinite.
    // Convention: L * 1 = 0 (constant functions are in the kernel).

    SparseMatrix BuildLaplacian(const Halfedge::Mesh& mesh)
    {
        const std::size_t nV = mesh.VerticesSize();

        // Phase 1: Build the Hodge star 1 weights
        DiagonalMatrix hodge1 = BuildHodgeStar1(mesh);

        // Phase 2: Accumulate entries in COO format, then convert to CSR.
        // For each non-deleted vertex, we need the diagonal entry + one entry
        // per adjacent edge (i.e., the 1-ring neighbors).

        // First pass: count nonzeros per row = 1 (diagonal) + valence
        std::vector<std::size_t> rowNnz(nV, 0);

        for (std::size_t vi = 0; vi < nV; ++vi)
        {
            VertexHandle vh{static_cast<PropertyIndex>(vi)};
            if (mesh.IsDeleted(vh) || mesh.IsIsolated(vh))
            {
                continue;
            }
            // 1 for diagonal + count neighbors
            rowNnz[vi] = 1;  // diagonal
            HalfedgeHandle hStart = mesh.Halfedge(vh);
            HalfedgeHandle h = hStart;
            do
            {
                rowNnz[vi] += 1;
                h = mesh.CWRotatedHalfedge(h);
            } while (h != hStart);
        }

        // Build row offsets
        SparseMatrix L;
        L.Rows = nV;
        L.Cols = nV;
        L.RowOffsets.resize(nV + 1);
        L.RowOffsets[0] = 0;
        for (std::size_t i = 0; i < nV; ++i)
        {
            L.RowOffsets[i + 1] = L.RowOffsets[i] + rowNnz[i];
        }

        std::size_t totalNnz = L.RowOffsets[nV];
        L.ColIndices.resize(totalNnz, 0);
        L.Values.resize(totalNnz, 0.0);

        // Phase 3: Fill in entries for each vertex row
        // We need to iterate the 1-ring for each vertex and collect (neighbor, weight).
        // Then sort by column index and write.
        for (std::size_t vi = 0; vi < nV; ++vi)
        {
            VertexHandle vh{static_cast<PropertyIndex>(vi)};
            if (mesh.IsDeleted(vh) || mesh.IsIsolated(vh))
            {
                continue;
            }

            // Collect (neighbor_index, -weight) pairs
            struct Entry
            {
                std::size_t Col;
                double Val;
            };
            std::vector<Entry> entries;
            entries.reserve(rowNnz[vi]);

            double diagSum = 0.0;

            HalfedgeHandle hStart = mesh.Halfedge(vh);
            HalfedgeHandle h = hStart;
            do
            {
                EdgeHandle e = mesh.Edge(h);
                double w = hodge1.Diagonal[e.Index];

                // The other vertex of this edge
                VertexHandle vOther = mesh.ToVertex(h);
                entries.push_back({vOther.Index, -w});
                diagSum += w;

                h = mesh.CWRotatedHalfedge(h);
            } while (h != hStart);

            // Add diagonal
            entries.push_back({vi, diagSum});

            // Sort by column
            std::sort(entries.begin(), entries.end(),
                      [](const Entry& a, const Entry& b) { return a.Col < b.Col; });

            // Write to CSR
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
    // BuildOperators
    // -------------------------------------------------------------------------

    DECOperators BuildOperators(const Halfedge::Mesh& mesh)
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
        assert(A.Rows == A.Cols);
        assert(b.size() >= A.Rows);
        assert(x.size() >= A.Rows);

        const std::size_t n = A.Rows;
        CGResult result;

        // Extract diagonal for Jacobi preconditioner
        std::vector<double> diagInv(n, 1.0);
        for (std::size_t i = 0; i < n; ++i)
        {
            for (std::size_t k = A.RowOffsets[i]; k < A.RowOffsets[i + 1]; ++k)
            {
                if (A.ColIndices[k] == i)
                {
                    double d = A.Values[k];
                    diagInv[i] = (std::abs(d) > 1e-15) ? (1.0 / d) : 1.0;
                    break;
                }
            }
        }

        // r = b - A*x
        std::vector<double> r(n);
        std::vector<double> Ax(n);
        A.Multiply(x, Ax);
        for (std::size_t i = 0; i < n; ++i)
            r[i] = b[i] - Ax[i];

        // z = M^{-1} * r  (Jacobi preconditioner)
        std::vector<double> z(n);
        for (std::size_t i = 0; i < n; ++i)
            z[i] = diagInv[i] * r[i];

        // p = z
        std::vector<double> p(z);

        // rz = r^T * z
        double rz = 0.0;
        for (std::size_t i = 0; i < n; ++i)
            rz += r[i] * z[i];

        std::vector<double> Ap(n);
        const double b_norm = [&]() {
            double s = 0.0;
            for (std::size_t i = 0; i < n; ++i)
                s += b[i] * b[i];
            return std::sqrt(s);
        }();
        const double tol = params.Tolerance * std::max(b_norm, 1.0);

        for (std::size_t iter = 0; iter < params.MaxIterations; ++iter)
        {
            // Check convergence
            double rNorm = 0.0;
            for (std::size_t i = 0; i < n; ++i)
                rNorm += r[i] * r[i];
            rNorm = std::sqrt(rNorm);

            result.Iterations = iter + 1;
            result.ResidualNorm = rNorm;

            if (rNorm < tol)
            {
                result.Converged = true;
                return result;
            }

            // Ap = A * p
            A.Multiply(p, Ap);

            // alpha = rz / (p^T * Ap)
            double pAp = 0.0;
            for (std::size_t i = 0; i < n; ++i)
                pAp += p[i] * Ap[i];

            if (std::abs(pAp) < 1e-30)
                break;

            double alpha = rz / pAp;

            // x = x + alpha * p
            // r = r - alpha * Ap
            for (std::size_t i = 0; i < n; ++i)
            {
                x[i] += alpha * p[i];
                r[i] -= alpha * Ap[i];
            }

            // z = M^{-1} * r
            for (std::size_t i = 0; i < n; ++i)
                z[i] = diagInv[i] * r[i];

            // rz_new = r^T * z
            double rz_new = 0.0;
            for (std::size_t i = 0; i < n; ++i)
                rz_new += r[i] * z[i];

            double beta = rz_new / rz;
            rz = rz_new;

            // p = z + beta * p
            for (std::size_t i = 0; i < n; ++i)
                p[i] = z[i] + beta * p[i];
        }

        return result;
    }

    CGResult SolveCGShifted(
        const DiagonalMatrix& M, double alpha,
        const SparseMatrix& A, double beta,
        std::span<const double> b,
        std::span<double> x,
        const CGParams& params)
    {
        assert(A.Rows == A.Cols);
        assert(M.Size == A.Rows);
        assert(b.size() >= A.Rows);
        assert(x.size() >= A.Rows);

        const std::size_t n = A.Rows;
        CGResult result;

        // Combined matrix-vector product: y = (alpha*M + beta*A) * v
        auto combinedMV = [&](std::span<const double> v, std::span<double> y)
        {
            // y = beta * A * v
            A.Multiply(v, y);
            for (std::size_t i = 0; i < n; ++i)
                y[i] *= beta;
            // y += alpha * M * v
            for (std::size_t i = 0; i < n; ++i)
                y[i] += alpha * M.Diagonal[i] * v[i];
        };

        // Jacobi preconditioner: inverse of diagonal of (alpha*M + beta*A)
        std::vector<double> diagInv(n, 1.0);
        for (std::size_t i = 0; i < n; ++i)
        {
            double d = alpha * M.Diagonal[i];
            // Add A's diagonal contribution
            for (std::size_t k = A.RowOffsets[i]; k < A.RowOffsets[i + 1]; ++k)
            {
                if (A.ColIndices[k] == i)
                {
                    d += beta * A.Values[k];
                    break;
                }
            }
            diagInv[i] = (std::abs(d) > 1e-15) ? (1.0 / d) : 1.0;
        }

        // r = b - (alpha*M + beta*A)*x
        std::vector<double> r(n);
        std::vector<double> Cx(n);
        combinedMV(x, Cx);
        for (std::size_t i = 0; i < n; ++i)
            r[i] = b[i] - Cx[i];

        // z = M^{-1} * r
        std::vector<double> z(n);
        for (std::size_t i = 0; i < n; ++i)
            z[i] = diagInv[i] * r[i];

        std::vector<double> p(z);

        double rz = 0.0;
        for (std::size_t i = 0; i < n; ++i)
            rz += r[i] * z[i];

        std::vector<double> Cp(n);
        const double b_norm = [&]() {
            double s = 0.0;
            for (std::size_t i = 0; i < n; ++i)
                s += b[i] * b[i];
            return std::sqrt(s);
        }();
        const double tol = params.Tolerance * std::max(b_norm, 1.0);

        for (std::size_t iter = 0; iter < params.MaxIterations; ++iter)
        {
            double rNorm = 0.0;
            for (std::size_t i = 0; i < n; ++i)
                rNorm += r[i] * r[i];
            rNorm = std::sqrt(rNorm);

            result.Iterations = iter + 1;
            result.ResidualNorm = rNorm;

            if (rNorm < tol)
            {
                result.Converged = true;
                return result;
            }

            combinedMV(p, Cp);

            double pCp = 0.0;
            for (std::size_t i = 0; i < n; ++i)
                pCp += p[i] * Cp[i];

            if (std::abs(pCp) < 1e-30)
                break;

            double a = rz / pCp;

            for (std::size_t i = 0; i < n; ++i)
            {
                x[i] += a * p[i];
                r[i] -= a * Cp[i];
            }

            for (std::size_t i = 0; i < n; ++i)
                z[i] = diagInv[i] * r[i];

            double rz_new = 0.0;
            for (std::size_t i = 0; i < n; ++i)
                rz_new += r[i] * z[i];

            double bt = rz_new / rz;
            rz = rz_new;

            for (std::size_t i = 0; i < n; ++i)
                p[i] = z[i] + bt * p[i];
        }

        return result;
    }

} // namespace Geometry::DEC

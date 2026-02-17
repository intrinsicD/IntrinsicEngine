module;

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>
#include <optional>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

module Geometry:Parameterization.Impl;

import :Parameterization;
import :Properties;
import :HalfedgeMesh;
import :DEC;

namespace Geometry::Parameterization
{
    namespace
    {
        // =====================================================================
        // COO sparse matrix builder → CSR conversion
        // =====================================================================
        struct COOEntry
        {
            std::size_t Row;
            std::size_t Col;
            double Value;
        };

        DEC::SparseMatrix BuildCSRFromCOO(
            std::size_t rows, std::size_t cols,
            std::vector<COOEntry>& entries)
        {
            // Sort by (row, col)
            std::sort(entries.begin(), entries.end(), [](const COOEntry& a, const COOEntry& b) {
                return (a.Row < b.Row) || (a.Row == b.Row && a.Col < b.Col);
            });

            DEC::SparseMatrix mat;
            mat.Rows = rows;
            mat.Cols = cols;
            mat.RowOffsets.resize(rows + 1, 0);

            // Merge duplicates and build CSR
            std::vector<std::size_t> tempCols;
            std::vector<double> tempVals;

            for (std::size_t i = 0; i < entries.size(); )
            {
                std::size_t r = entries[i].Row;
                std::size_t c = entries[i].Col;
                double val = 0.0;

                while (i < entries.size() && entries[i].Row == r && entries[i].Col == c)
                {
                    val += entries[i].Value;
                    ++i;
                }

                if (std::abs(val) > 1e-15)
                {
                    tempCols.push_back(c);
                    tempVals.push_back(val);
                    mat.RowOffsets[r + 1]++;
                }
            }

            // Prefix sum
            for (std::size_t i = 0; i < rows; ++i)
                mat.RowOffsets[i + 1] += mat.RowOffsets[i];

            mat.ColIndices = std::move(tempCols);
            mat.Values = std::move(tempVals);
            return mat;
        }

        // Compute A^T * A as a CSR matrix
        DEC::SparseMatrix ComputeAtA(const DEC::SparseMatrix& A)
        {
            // Strategy: for each column pair (c1, c2), accumulate Σ_r A[r,c1]*A[r,c2]
            // Build column-wise data structure first for efficient dot products
            std::size_t n = A.Cols;

            // Build COO entries for the result
            std::vector<COOEntry> entries;
            entries.reserve(A.NonZeros() * 4); // rough estimate

            // For each row of A, all pairs of nonzero entries contribute to AtA
            for (std::size_t r = 0; r < A.Rows; ++r)
            {
                std::size_t start = A.RowOffsets[r];
                std::size_t end = A.RowOffsets[r + 1];

                for (std::size_t i = start; i < end; ++i)
                {
                    for (std::size_t j = start; j < end; ++j)
                    {
                        entries.push_back({A.ColIndices[i], A.ColIndices[j],
                                           A.Values[i] * A.Values[j]});
                    }
                }
            }

            return BuildCSRFromCOO(n, n, entries);
        }

        // Compute A^T * b where A is sparse and b is dense
        std::vector<double> ComputeAtb(const DEC::SparseMatrix& A, const std::vector<double>& b)
        {
            std::vector<double> result(A.Cols, 0.0);
            A.MultiplyTranspose(b, result);
            return result;
        }

        // Count boundary loops and collect boundary vertices of the first loop
        struct BoundaryInfo
        {
            std::size_t LoopCount{0};
            std::vector<std::size_t> LoopVertices; // vertex indices of first loop
        };

        BoundaryInfo FindBoundaryLoops(const Halfedge::Mesh& mesh)
        {
            BoundaryInfo info;
            std::vector<bool> visited(mesh.HalfedgesSize(), false);

            for (std::size_t hi = 0; hi < mesh.HalfedgesSize(); ++hi)
            {
                HalfedgeHandle h{static_cast<PropertyIndex>(hi)};
                if (visited[hi]) continue;
                if (mesh.IsDeleted(EdgeHandle{static_cast<PropertyIndex>(hi / 2)})) continue;
                if (!mesh.IsBoundary(h)) continue;

                // Found an unvisited boundary halfedge — walk the loop
                std::vector<std::size_t> loopVerts;
                HalfedgeHandle cur = h;
                std::size_t safety = 0;
                do
                {
                    visited[cur.Index] = true;
                    loopVerts.push_back(mesh.FromVertex(cur).Index);
                    cur = mesh.NextHalfedge(cur);
                    if (++safety > mesh.HalfedgesSize()) break;
                } while (cur != h);

                if (info.LoopCount == 0)
                    info.LoopVertices = std::move(loopVerts);

                ++info.LoopCount;
            }

            return info;
        }

        // Select two boundary vertices maximizing arc length separation
        std::pair<std::size_t, std::size_t> SelectPinVertices(
            const Halfedge::Mesh& mesh,
            const std::vector<std::size_t>& boundaryVerts)
        {
            if (boundaryVerts.size() < 2)
                return {boundaryVerts[0], boundaryVerts[0]};

            // Compute cumulative arc length
            std::vector<double> arcLen(boundaryVerts.size(), 0.0);
            for (std::size_t i = 1; i < boundaryVerts.size(); ++i)
            {
                glm::vec3 a = mesh.Position(VertexHandle{static_cast<PropertyIndex>(boundaryVerts[i - 1])});
                glm::vec3 b = mesh.Position(VertexHandle{static_cast<PropertyIndex>(boundaryVerts[i])});
                arcLen[i] = arcLen[i - 1] + static_cast<double>(glm::distance(a, b));
            }
            double totalArc = arcLen.back();

            // Find vertex closest to half the total arc length
            double halfArc = totalArc / 2.0;
            std::size_t bestIdx = 1;
            double bestDist = std::abs(arcLen[1] - halfArc);
            for (std::size_t i = 2; i < boundaryVerts.size(); ++i)
            {
                double d = std::abs(arcLen[i] - halfArc);
                if (d < bestDist)
                {
                    bestDist = d;
                    bestIdx = i;
                }
            }

            return {boundaryVerts[0], boundaryVerts[bestIdx]};
        }
    }

    // =========================================================================
    // LSCM Implementation
    // =========================================================================

    std::optional<ParameterizationResult> ComputeLSCM(
        const Halfedge::Mesh& mesh,
        const ParameterizationParams& params)
    {
        if (mesh.IsEmpty() || mesh.FaceCount() == 0 || mesh.VertexCount() < 3)
            return std::nullopt;

        const std::size_t nV = mesh.VerticesSize();
        const std::size_t nF = mesh.FacesSize();

        // ------------------------------------------------------------------
        // Step 1: Validate topology — must be triangle mesh with disk topology
        // ------------------------------------------------------------------
        // Check all faces are triangles
        for (std::size_t fi = 0; fi < nF; ++fi)
        {
            FaceHandle fh{static_cast<PropertyIndex>(fi)};
            if (mesh.IsDeleted(fh)) continue;
            if (mesh.Valence(fh) != 3) return std::nullopt;
        }

        auto boundary = FindBoundaryLoops(mesh);
        if (boundary.LoopCount != 1) return std::nullopt;

        // ------------------------------------------------------------------
        // Step 2: Select pin vertices
        // ------------------------------------------------------------------
        std::size_t pin0, pin1;
        glm::vec2 pinUV0, pinUV1;

        if (params.PinVertex0 != std::numeric_limits<std::size_t>::max() &&
            params.PinVertex1 != std::numeric_limits<std::size_t>::max())
        {
            pin0 = params.PinVertex0;
            pin1 = params.PinVertex1;
            if (pin0 >= nV || pin1 >= nV || pin0 == pin1) return std::nullopt;
            VertexHandle v0{static_cast<PropertyIndex>(pin0)};
            VertexHandle v1{static_cast<PropertyIndex>(pin1)};
            if (mesh.IsDeleted(v0) || mesh.IsDeleted(v1)) return std::nullopt;
        }
        else
        {
            auto [autoPin0, autoPin1] = SelectPinVertices(mesh, boundary.LoopVertices);
            pin0 = autoPin0;
            pin1 = autoPin1;
            if (pin0 == pin1) return std::nullopt;
        }

        pinUV0 = params.PinUV0;
        pinUV1 = params.PinUV1;

        // ------------------------------------------------------------------
        // Step 3: Build vertex index mapping (global → free-variable index)
        // ------------------------------------------------------------------
        std::vector<std::size_t> vertexToFree(nV, std::numeric_limits<std::size_t>::max());
        std::vector<std::size_t> freeToVertex;
        freeToVertex.reserve(nV);

        for (std::size_t vi = 0; vi < nV; ++vi)
        {
            VertexHandle vh{static_cast<PropertyIndex>(vi)};
            if (mesh.IsDeleted(vh) || mesh.IsIsolated(vh)) continue;
            if (vi == pin0 || vi == pin1) continue;

            vertexToFree[vi] = freeToVertex.size();
            freeToVertex.push_back(vi);
        }

        std::size_t nFree = freeToVertex.size();
        if (nFree == 0) return std::nullopt;

        // ------------------------------------------------------------------
        // Step 4: Assemble LSCM matrix in COO format
        // ------------------------------------------------------------------
        // System has 2*nFace rows (2 equations per triangle) and 2*nFree cols
        // (u and v for each free vertex). Columns [0..nFree) are u-coords,
        // [nFree..2*nFree) are v-coords.

        std::size_t nTriangles = mesh.FaceCount();
        std::vector<COOEntry> cooEntries;
        cooEntries.reserve(nTriangles * 12);  // ~6 nonzeros per row * 2 rows
        std::vector<double> rhs(2 * nTriangles, 0.0);

        std::size_t rowIdx = 0;
        for (std::size_t fi = 0; fi < nF; ++fi)
        {
            FaceHandle fh{static_cast<PropertyIndex>(fi)};
            if (mesh.IsDeleted(fh)) continue;

            HalfedgeHandle h0 = mesh.Halfedge(fh);
            HalfedgeHandle h1 = mesh.NextHalfedge(h0);
            HalfedgeHandle h2 = mesh.NextHalfedge(h1);

            VertexHandle va = mesh.ToVertex(h0);
            VertexHandle vb = mesh.ToVertex(h1);
            VertexHandle vc = mesh.ToVertex(h2);

            glm::vec3 pa = mesh.Position(va);
            glm::vec3 pb = mesh.Position(vb);
            glm::vec3 pc = mesh.Position(vc);

            // Local 2D coordinates in the triangle's plane
            glm::vec3 e1 = pb - pa;
            glm::vec3 e2 = pc - pa;
            glm::vec3 normal = glm::cross(e1, e2);
            float normalLen = glm::length(normal);
            if (normalLen < 1e-10f)
            {
                rowIdx += 2;
                continue; // degenerate triangle
            }

            glm::vec3 sAxis = glm::normalize(e1);
            glm::vec3 tAxis = glm::normalize(glm::cross(normal, e1));

            // Local 2D coords: (s0,t0) = (0,0), (s1,t1), (s2,t2)
            double s1 = static_cast<double>(glm::dot(e1, sAxis));
            double t1 = static_cast<double>(glm::dot(e1, tAxis));
            double s2 = static_cast<double>(glm::dot(e2, sAxis));
            double t2 = static_cast<double>(glm::dot(e2, tAxis));
            double s0 = 0.0, t0 = 0.0;

            double area = 0.5 * static_cast<double>(normalLen);
            double sqrtArea = std::sqrt(area);

            // LSCM coefficients for the conformal condition per triangle:
            // Row 2t:   sqrtA * [W_u * u + W_v * v] = 0
            // Row 2t+1: sqrtA * [W_v_perp * u + W_u * v] = 0
            //
            // For vertex a (index 0 in triangle):
            //   Wu_a = (s2 - s1),  Wv_a = -(t2 - t1)
            //   Wu_b = (s0 - s2),  Wv_b = -(t0 - t2)
            //   Wu_c = (s1 - s0),  Wv_c = -(t1 - t0)
            //
            // Row 2t:   sqrtA * [Wu_a*u_a + Wu_b*u_b + Wu_c*u_c + Wv_a*v_a + Wv_b*v_b + Wv_c*v_c]
            // Row 2t+1: sqrtA * [(t2-t1)*u_a + (t0-t2)*u_b + (t1-t0)*u_c +
            //                    (s2-s1)*v_a + (s0-s2)*v_b + (s1-s0)*v_c]

            double Wu[3] = {s2 - s1, s0 - s2, s1 - s0};
            double Wv[3] = {-(t2 - t1), -(t0 - t2), -(t1 - t0)};
            double Wt[3] = {t2 - t1, t0 - t2, t1 - t0};
            double Ws[3] = {s2 - s1, s0 - s2, s1 - s0};

            std::size_t vertIdx[3] = {va.Index, vb.Index, vc.Index};

            std::size_t r0 = rowIdx;
            std::size_t r1 = rowIdx + 1;

            for (int k = 0; k < 3; ++k)
            {
                std::size_t vi = vertIdx[k];
                std::size_t freeIdx = vertexToFree[vi];

                double wu = sqrtArea * Wu[k];
                double wv = sqrtArea * Wv[k];
                double wt = sqrtArea * Wt[k];
                double ws = sqrtArea * Ws[k];

                if (vi == pin0)
                {
                    // Pinned — move contribution to RHS
                    double u_pin = static_cast<double>(pinUV0.x);
                    double v_pin = static_cast<double>(pinUV0.y);
                    rhs[r0] -= wu * u_pin + wv * v_pin;
                    rhs[r1] -= wt * u_pin + ws * v_pin;
                }
                else if (vi == pin1)
                {
                    double u_pin = static_cast<double>(pinUV1.x);
                    double v_pin = static_cast<double>(pinUV1.y);
                    rhs[r0] -= wu * u_pin + wv * v_pin;
                    rhs[r1] -= wt * u_pin + ws * v_pin;
                }
                else if (freeIdx != std::numeric_limits<std::size_t>::max())
                {
                    // Free vertex — add to matrix
                    // u column = freeIdx, v column = nFree + freeIdx
                    if (std::abs(wu) > 1e-15) cooEntries.push_back({r0, freeIdx, wu});
                    if (std::abs(wv) > 1e-15) cooEntries.push_back({r0, nFree + freeIdx, wv});
                    if (std::abs(wt) > 1e-15) cooEntries.push_back({r1, freeIdx, wt});
                    if (std::abs(ws) > 1e-15) cooEntries.push_back({r1, nFree + freeIdx, ws});
                }
            }

            rowIdx += 2;
        }

        std::size_t nRows = rowIdx;
        std::size_t nCols = 2 * nFree;

        // ------------------------------------------------------------------
        // Step 5: Form normal equations A^T*A*x = A^T*b
        // ------------------------------------------------------------------
        auto A = BuildCSRFromCOO(nRows, nCols, cooEntries);
        auto AtA = ComputeAtA(A);
        auto Atb = ComputeAtb(A, rhs);

        // ------------------------------------------------------------------
        // Step 6: Solve via CG
        // ------------------------------------------------------------------
        std::vector<double> solution(nCols, 0.0);

        DEC::CGParams cgParams;
        cgParams.MaxIterations = params.MaxSolverIterations;
        cgParams.Tolerance = params.SolverTolerance;

        auto cgResult = DEC::SolveCG(AtA, Atb, solution, cgParams);

        // ------------------------------------------------------------------
        // Step 7: Extract UVs
        // ------------------------------------------------------------------
        ParameterizationResult result;
        result.UVs.resize(nV, glm::vec2(0.0f, 0.0f));
        result.CGIterations = cgResult.Iterations;
        result.Converged = cgResult.Converged;

        // Fill pinned vertices
        result.UVs[pin0] = pinUV0;
        result.UVs[pin1] = pinUV1;

        // Fill free vertices
        for (std::size_t i = 0; i < nFree; ++i)
        {
            std::size_t vi = freeToVertex[i];
            float u = static_cast<float>(solution[i]);
            float v = static_cast<float>(solution[nFree + i]);
            result.UVs[vi] = glm::vec2(u, v);
        }

        // ------------------------------------------------------------------
        // Step 8: Quality metrics — conformal distortion and flipped triangles
        // ------------------------------------------------------------------
        double sumDistortion = 0.0;
        double maxDistortion = 0.0;
        std::size_t flipped = 0;
        std::size_t validTriangles = 0;

        for (std::size_t fi = 0; fi < nF; ++fi)
        {
            FaceHandle fh{static_cast<PropertyIndex>(fi)};
            if (mesh.IsDeleted(fh)) continue;

            HalfedgeHandle h0 = mesh.Halfedge(fh);
            HalfedgeHandle h1 = mesh.NextHalfedge(h0);
            HalfedgeHandle h2 = mesh.NextHalfedge(h1);

            VertexHandle va = mesh.ToVertex(h0);
            VertexHandle vb = mesh.ToVertex(h1);
            VertexHandle vc = mesh.ToVertex(h2);

            // 3D triangle
            glm::vec3 pa = mesh.Position(va);
            glm::vec3 pb = mesh.Position(vb);
            glm::vec3 pc = mesh.Position(vc);

            // UV triangle
            glm::vec2 ua = result.UVs[va.Index];
            glm::vec2 ub = result.UVs[vb.Index];
            glm::vec2 uc = result.UVs[vc.Index];

            // Signed UV area
            double uvArea = 0.5 * static_cast<double>(
                (ub.x - ua.x) * (uc.y - ua.y) - (uc.x - ua.x) * (ub.y - ua.y));
            if (uvArea < 0.0) ++flipped;

            // Compute Jacobian singular values for conformal distortion
            // Using local 2D frame in 3D
            glm::vec3 e1_3d = pb - pa;
            glm::vec3 e2_3d = pc - pa;
            glm::vec2 e1_uv = ub - ua;
            glm::vec2 e2_uv = uc - ua;

            double area3d = 0.5 * static_cast<double>(glm::length(glm::cross(e1_3d, e2_3d)));
            if (area3d < 1e-12) continue;

            // Jacobian J maps from 3D local coords to UV:
            // J = [du/ds, du/dt; dv/ds, dv/dt]
            // Using local 2D coords in the triangle
            glm::vec3 sAxis = glm::normalize(e1_3d);
            glm::vec3 normal = glm::cross(e1_3d, e2_3d);
            glm::vec3 tAxis = glm::normalize(glm::cross(normal, e1_3d));

            double s1 = static_cast<double>(glm::dot(e1_3d, sAxis));
            double t1 = static_cast<double>(glm::dot(e1_3d, tAxis));
            double s2 = static_cast<double>(glm::dot(e2_3d, sAxis));
            double t2 = static_cast<double>(glm::dot(e2_3d, tAxis));

            double det = s1 * t2 - s2 * t1;
            if (std::abs(det) < 1e-12) continue;

            double invDet = 1.0 / det;

            // J = [du1, du2; dv1, dv2] * inv([s1,s2;t1,t2])
            double du1 = static_cast<double>(e1_uv.x);
            double du2 = static_cast<double>(e2_uv.x);
            double dv1 = static_cast<double>(e1_uv.y);
            double dv2 = static_cast<double>(e2_uv.y);

            double j00 = (du1 * t2 - du2 * t1) * invDet;
            double j01 = (-du1 * s2 + du2 * s1) * invDet;
            double j10 = (dv1 * t2 - dv2 * t1) * invDet;
            double j11 = (-dv1 * s2 + dv2 * s1) * invDet;

            // SVD of 2x2: singular values from eigenvalues of J^T*J
            double a = j00 * j00 + j10 * j10;
            double b = j00 * j01 + j10 * j11;
            double c = j01 * j01 + j11 * j11;

            double disc = std::sqrt(std::max(0.0, (a - c) * (a - c) + 4.0 * b * b));
            double sigma1 = std::sqrt(std::max(0.0, (a + c + disc) / 2.0));
            double sigma2 = std::sqrt(std::max(0.0, (a + c - disc) / 2.0));

            double sigMax = std::max(sigma1, sigma2);
            double sigMin = std::min(sigma1, sigma2);

            double distortion = (sigMin > 1e-12) ? sigMax / sigMin : 1e6;
            sumDistortion += distortion;
            if (distortion > maxDistortion) maxDistortion = distortion;
            ++validTriangles;
        }

        result.FlippedTriangleCount = flipped;
        result.MeanConformalDistortion = (validTriangles > 0) ? sumDistortion / static_cast<double>(validTriangles) : 0.0;
        result.MaxConformalDistortion = maxDistortion;

        return result;
    }

} // namespace Geometry::Parameterization

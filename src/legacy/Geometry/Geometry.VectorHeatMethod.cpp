module;

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <complex>
#include <numbers>
#include <optional>
#include <span>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

module Geometry.VectorHeatMethod;

import Geometry.Properties;
import Geometry.HalfedgeMesh;
import Geometry.DEC;
import Geometry.MeshUtils;
import Geometry.Geodesic;

namespace Geometry::VectorHeatMethod
{
    using Complex = std::complex<double>;
    using MeshUtils::Cotan;
    using MeshUtils::MeanEdgeLength;
    using MeshUtils::TriangleFaceView;
    using MeshUtils::TryGetTriangleFaceView;

    // =========================================================================
    // Tangent basis construction
    // =========================================================================
    // For each vertex, build an orthonormal tangent frame (e1, e2) where:
    //   - normal N is the area-weighted average of incident face normals
    //   - e1 is the projection of the first outgoing halfedge into the
    //     tangent plane, normalized
    //   - e2 = N × e1

    struct TangentBasis
    {
        glm::vec3 Normal{0.0f};
        glm::vec3 E1{0.0f};
        glm::vec3 E2{0.0f};
        bool Valid{false};
    };

    static std::vector<TangentBasis> BuildTangentBases(const Halfedge::Mesh& mesh)
    {
        const std::size_t nV = mesh.VerticesSize();
        std::vector<TangentBasis> bases(nV);

        // Compute area-weighted vertex normals
        for (std::size_t fi = 0; fi < mesh.FacesSize(); ++fi)
        {
            FaceHandle fh{static_cast<PropertyIndex>(fi)};
            TriangleFaceView tri{};
            if (!TryGetTriangleFaceView(mesh, fh, tri))
                continue;

            glm::vec3 faceNormal = glm::cross(tri.P1 - tri.P0, tri.P2 - tri.P0);
            // Unnormalized — magnitude proportional to area (good for weighting)
            bases[tri.V0.Index].Normal += faceNormal;
            bases[tri.V1.Index].Normal += faceNormal;
            bases[tri.V2.Index].Normal += faceNormal;
        }

        // Build orthonormal frame at each vertex
        for (std::size_t vi = 0; vi < nV; ++vi)
        {
            VertexHandle vh{static_cast<PropertyIndex>(vi)};
            if (mesh.IsDeleted(vh) || mesh.IsIsolated(vh))
                continue;

            glm::vec3 N = bases[vi].Normal;
            float nLen = glm::length(N);
            if (nLen < 1e-10f)
                continue;

            N /= nLen;
            bases[vi].Normal = N;

            // Find the first outgoing halfedge direction
            HalfedgeHandle h0 = mesh.Halfedge(vh);
            if (!h0.IsValid())
                continue;

            glm::vec3 edgeDir = mesh.Position(mesh.ToVertex(h0)) - mesh.Position(vh);

            // Project into tangent plane: e1 = edgeDir - (edgeDir·N)*N
            glm::vec3 e1 = edgeDir - glm::dot(edgeDir, N) * N;
            float e1Len = glm::length(e1);
            if (e1Len < 1e-10f)
            {
                // Degenerate first edge — try next
                for (const HalfedgeHandle h : mesh.HalfedgesAroundVertex(vh))
                {
                    edgeDir = mesh.Position(mesh.ToVertex(h)) - mesh.Position(vh);
                    e1 = edgeDir - glm::dot(edgeDir, N) * N;
                    e1Len = glm::length(e1);
                    if (e1Len >= 1e-10f)
                        break;
                }
                if (e1Len < 1e-10f)
                    continue;
            }

            e1 /= e1Len;
            glm::vec3 e2 = glm::cross(N, e1);

            bases[vi].E1 = e1;
            bases[vi].E2 = e2;
            bases[vi].Valid = true;
        }

        return bases;
    }

    // =========================================================================
    // Project a 3D vector into a vertex's tangent basis → complex number
    // =========================================================================
    static Complex ProjectToTangent(const glm::vec3& vec, const TangentBasis& basis)
    {
        double x = static_cast<double>(glm::dot(vec, basis.E1));
        double y = static_cast<double>(glm::dot(vec, basis.E2));
        return Complex(x, y);
    }

    // =========================================================================
    // Reconstruct a 3D vector from a complex number in a vertex's tangent basis
    // =========================================================================
    static glm::vec3 ReconstructFromTangent(const Complex& z, const TangentBasis& basis)
    {
        return static_cast<float>(z.real()) * basis.E1
             + static_cast<float>(z.imag()) * basis.E2;
    }

    // =========================================================================
    // Connection angle computation
    // =========================================================================
    // For an edge (i, j), the connection angle ρ_ij is the angle that
    // parallel-transports tangent basis at i to tangent basis at j.
    //
    // Computed as: ρ_ij = arg(e_ij_in_basis_j) - arg(e_ij_in_basis_i) + π
    //
    // where e_ij is the edge vector from i to j, projected into each
    // vertex's tangent basis.
    //
    // This formulation from Knöppel et al. (2013) / Sharp et al. (2019)
    // encodes how the tangent frames rotate as we cross the edge.

    static double ComputeConnectionAngle(
        const Halfedge::Mesh& mesh,
        HalfedgeHandle hij,
        const std::vector<TangentBasis>& bases)
    {
        VertexHandle vi = mesh.FromVertex(hij);
        VertexHandle vj = mesh.ToVertex(hij);

        glm::vec3 eij = mesh.Position(vj) - mesh.Position(vi);

        // Project edge into tangent basis at i
        Complex eij_i = ProjectToTangent(eij, bases[vi.Index]);
        // Project edge into tangent basis at j
        Complex eij_j = ProjectToTangent(eij, bases[vj.Index]);

        if (std::abs(eij_i) < 1e-12 || std::abs(eij_j) < 1e-12)
            return 0.0;

        // Connection angle: rotation that transports from basis_j to basis_i.
        // If a geometric vector has angle α_i in basis_i and α_j in basis_j,
        // then ρ_ij = α_i - α_j (since exp(iρ) * z_j = z_i for any shared vector).
        // Using the edge vector e_ij as the shared geometric vector:
        double angle_i = std::arg(eij_i);
        double angle_j = std::arg(eij_j);

        return angle_i - angle_j;
    }

    // =========================================================================
    // Complex connection Laplacian assembly
    // =========================================================================
    // The connection Laplacian Δ_∇ is a Hermitian #V × #V matrix:
    //
    //   Δ_∇[i,j] = -w_ij · exp(i·ρ_ij)   for edge (i,j)
    //   Δ_∇[i,i] = Σ_j w_ij              (real, same as scalar Laplacian)
    //
    // where w_ij = (cot α_ij + cot β_ij)/2 is the cotan weight and
    // ρ_ij is the connection angle encoding parallel transport.
    //
    // Stored as parallel real/imag CSR arrays sharing the same sparsity
    // pattern as the scalar Laplacian.

    struct ComplexSparseMatrix
    {
        std::size_t Rows{0};
        std::size_t Cols{0};
        std::vector<std::size_t> RowOffsets;
        std::vector<std::size_t> ColIndices;
        std::vector<double> ValuesReal;
        std::vector<double> ValuesImag;

        void Multiply(std::span<const double> xReal, std::span<const double> xImag,
                       std::span<double> yReal, std::span<double> yImag) const
        {
            for (std::size_t i = 0; i < Rows; ++i)
            {
                double sumR = 0.0;
                double sumI = 0.0;
                for (std::size_t k = RowOffsets[i]; k < RowOffsets[i + 1]; ++k)
                {
                    std::size_t j = ColIndices[k];
                    double aR = ValuesReal[k];
                    double aI = ValuesImag[k];
                    // Complex multiply: (aR + i·aI) * (xR + i·xI)
                    sumR += aR * xReal[j] - aI * xImag[j];
                    sumI += aR * xImag[j] + aI * xReal[j];
                }
                yReal[i] = sumR;
                yImag[i] = sumI;
            }
        }
    };

    struct ConnectionLaplacianData
    {
        ComplexSparseMatrix Matrix;
        DEC::DiagonalMatrix Mass; // Hodge0 (real diagonal)
    };

    static ConnectionLaplacianData BuildConnectionLaplacian(
        const Halfedge::Mesh& mesh,
        const std::vector<TangentBasis>& bases)
    {
        const std::size_t nV = mesh.VerticesSize();

        // Build Hodge star 1 weights (cotan weights per edge)
        DEC::DiagonalMatrix hodge1 = DEC::BuildHodgeStar1(mesh);
        DEC::DiagonalMatrix hodge0 = DEC::BuildHodgeStar0(mesh);

        // Count nonzeros per row (same pattern as scalar Laplacian)
        std::vector<std::size_t> rowNnz(nV, 0);
        for (std::size_t vi = 0; vi < nV; ++vi)
        {
            VertexHandle vh{static_cast<PropertyIndex>(vi)};
            if (mesh.IsDeleted(vh) || mesh.IsIsolated(vh))
                continue;
            rowNnz[vi] = 1; // diagonal
            for (const HalfedgeHandle h : mesh.HalfedgesAroundVertex(vh))
            {
                (void)h;
                rowNnz[vi] += 1;
            }
        }

        ComplexSparseMatrix L;
        L.Rows = nV;
        L.Cols = nV;
        L.RowOffsets.resize(nV + 1);
        L.RowOffsets[0] = 0;
        for (std::size_t i = 0; i < nV; ++i)
            L.RowOffsets[i + 1] = L.RowOffsets[i] + rowNnz[i];

        std::size_t totalNnz = L.RowOffsets[nV];
        L.ColIndices.resize(totalNnz, 0);
        L.ValuesReal.resize(totalNnz, 0.0);
        L.ValuesImag.resize(totalNnz, 0.0);

        // Fill entries
        for (std::size_t vi = 0; vi < nV; ++vi)
        {
            VertexHandle vh{static_cast<PropertyIndex>(vi)};
            if (mesh.IsDeleted(vh) || mesh.IsIsolated(vh))
                continue;

            struct Entry
            {
                std::size_t Col;
                double Real;
                double Imag;
            };
            std::vector<Entry> entries;
            entries.reserve(rowNnz[vi]);

            double diagSum = 0.0;

            for (const HalfedgeHandle h : mesh.HalfedgesAroundVertex(vh))
            {
                EdgeHandle e = mesh.Edge(h);
                double w = hodge1.Diagonal[e.Index];

                VertexHandle vOther = mesh.ToVertex(h);

                if (!bases[vi].Valid || !bases[vOther.Index].Valid)
                {
                    // Degenerate basis — fall back to scalar (real) weight
                    entries.push_back({vOther.Index, -w, 0.0});
                }
                else
                {
                    // Connection angle from vi to vOther
                    double rho = ComputeConnectionAngle(mesh, h, bases);

                    // Off-diagonal: -w * exp(i·ρ)
                    double cosRho = std::cos(rho);
                    double sinRho = std::sin(rho);
                    entries.push_back({vOther.Index, -w * cosRho, -w * sinRho});
                }

                diagSum += w;
            }

            // Diagonal entry is real (sum of weights)
            entries.push_back({vi, diagSum, 0.0});

            // Sort by column
            std::sort(entries.begin(), entries.end(),
                      [](const Entry& a, const Entry& b) { return a.Col < b.Col; });

            std::size_t base = L.RowOffsets[vi];
            for (std::size_t k = 0; k < entries.size(); ++k)
            {
                L.ColIndices[base + k] = entries[k].Col;
                L.ValuesReal[base + k] = entries[k].Real;
                L.ValuesImag[base + k] = entries[k].Imag;
            }
        }

        return ConnectionLaplacianData{std::move(L), std::move(hodge0)};
    }

    // =========================================================================
    // Complex CG solver for Hermitian positive-definite systems
    // =========================================================================
    // Solves (alpha*M + beta*Δ_∇) X = B
    // where M is a real diagonal mass matrix and Δ_∇ is the Hermitian
    // connection Laplacian. X and B are complex vectors stored as
    // separate real/imag arrays.

    struct ComplexCGResult
    {
        std::size_t Iterations{0};
        double ResidualNorm{0.0};
        bool Converged{false};
    };

    static ComplexCGResult SolveComplexCGShifted(
        const DEC::DiagonalMatrix& M, double alpha,
        const ComplexSparseMatrix& A, double beta,
        std::span<const double> bReal, std::span<const double> bImag,
        std::span<double> xReal, std::span<double> xImag,
        const DEC::CGParams& params)
    {
        const std::size_t n = A.Rows;
        ComplexCGResult result;

        // Combined matrix-vector: y = (alpha*M + beta*A) * x
        auto combinedMV = [&](std::span<const double> vR, std::span<const double> vI,
                               std::span<double> yR, std::span<double> yI)
        {
            // y = beta * A * v
            A.Multiply(vR, vI, yR, yI);
            for (std::size_t i = 0; i < n; ++i)
            {
                yR[i] *= beta;
                yI[i] *= beta;
            }
            // y += alpha * M * v (M is real diagonal)
            for (std::size_t i = 0; i < n; ++i)
            {
                yR[i] += alpha * M.Diagonal[i] * vR[i];
                yI[i] += alpha * M.Diagonal[i] * vI[i];
            }
        };

        // Jacobi preconditioner: inverse of real part of diagonal
        std::vector<double> diagInv(n, 1.0);
        for (std::size_t i = 0; i < n; ++i)
        {
            double d = alpha * M.Diagonal[i];
            // Add A's diagonal contribution (real part only — diagonal is real)
            for (std::size_t k = A.RowOffsets[i]; k < A.RowOffsets[i + 1]; ++k)
            {
                if (A.ColIndices[k] == i)
                {
                    d += beta * A.ValuesReal[k];
                    break;
                }
            }
            diagInv[i] = (std::abs(d) > 1e-15) ? (1.0 / d) : 1.0;
        }

        // Complex dot product (Hermitian inner product): <a, b> = Σ(aR*bR + aI*bI)
        // (real part only, since for CG on Hermitian systems, inner products are real)
        auto realDot = [&](std::span<const double> aR, std::span<const double> aI,
                            std::span<const double> bR, std::span<const double> bI) -> double
        {
            double s = 0.0;
            for (std::size_t i = 0; i < n; ++i)
                s += aR[i] * bR[i] + aI[i] * bI[i];
            return s;
        };

        // r = b - C*x
        std::vector<double> rR(n), rI(n);
        std::vector<double> CxR(n), CxI(n);
        combinedMV(xReal, xImag, CxR, CxI);
        for (std::size_t i = 0; i < n; ++i)
        {
            rR[i] = bReal[i] - CxR[i];
            rI[i] = bImag[i] - CxI[i];
        }

        // z = M^{-1} * r (Jacobi)
        std::vector<double> zR(n), zI(n);
        for (std::size_t i = 0; i < n; ++i)
        {
            zR[i] = diagInv[i] * rR[i];
            zI[i] = diagInv[i] * rI[i];
        }

        // p = z
        std::vector<double> pR(zR), pI(zI);

        double rz = realDot(rR, rI, zR, zI);

        std::vector<double> CpR(n), CpI(n);

        double bNorm = std::sqrt(realDot(bReal, bImag, bReal, bImag));
        double tol = params.Tolerance * std::max(bNorm, 1.0);

        for (std::size_t iter = 0; iter < params.MaxIterations; ++iter)
        {
            double rNorm = std::sqrt(realDot(rR, rI, rR, rI));
            result.Iterations = iter + 1;
            result.ResidualNorm = rNorm;

            if (rNorm < tol)
            {
                result.Converged = true;
                return result;
            }

            combinedMV(pR, pI, CpR, CpI);

            double pCp = realDot(pR, pI, CpR, CpI);
            if (std::abs(pCp) < 1e-30)
                break;

            double a = rz / pCp;

            for (std::size_t i = 0; i < n; ++i)
            {
                xReal[i] += a * pR[i];
                xImag[i] += a * pI[i];
                rR[i] -= a * CpR[i];
                rI[i] -= a * CpI[i];
            }

            for (std::size_t i = 0; i < n; ++i)
            {
                zR[i] = diagInv[i] * rR[i];
                zI[i] = diagInv[i] * rI[i];
            }

            double rz_new = realDot(rR, rI, zR, zI);
            double bt = rz_new / rz;
            rz = rz_new;

            for (std::size_t i = 0; i < n; ++i)
            {
                pR[i] = zR[i] + bt * pR[i];
                pI[i] = zI[i] + bt * pI[i];
            }
        }

        return result;
    }

    // =========================================================================
    // Parallel transport implementation
    // =========================================================================

    std::optional<VectorHeatResult> TransportVectors(
        Halfedge::Mesh& mesh,
        std::span<const std::size_t> sourceVertices,
        std::span<const glm::vec3> sourceVectors,
        const VectorHeatParams& params)
    {
        if (mesh.IsEmpty() || mesh.FaceCount() == 0
            || sourceVertices.empty()
            || sourceVertices.size() != sourceVectors.size())
            return std::nullopt;

        const std::size_t nV = mesh.VerticesSize();

        // Build tangent bases
        auto bases = BuildTangentBases(mesh);

        // Build connection Laplacian
        auto [connLap, mass] = BuildConnectionLaplacian(mesh, bases);

        // Time step
        double h = MeanEdgeLength(mesh);
        double t = params.TimeStep;
        if (t <= 0.0)
            t = h * h;

        // Build RHS: encode source vectors in local tangent bases
        std::vector<double> rhsReal(nV, 0.0);
        std::vector<double> rhsImag(nV, 0.0);

        for (std::size_t k = 0; k < sourceVertices.size(); ++k)
        {
            std::size_t si = sourceVertices[k];
            if (si >= nV)
                continue;

            VertexHandle vh{static_cast<PropertyIndex>(si)};
            if (mesh.IsDeleted(vh) || mesh.IsIsolated(vh) || !bases[si].Valid)
                continue;

            // Project source vector into tangent basis
            glm::vec3 sv = sourceVectors[k];
            // Remove normal component
            sv -= glm::dot(sv, bases[si].Normal) * bases[si].Normal;
            float svLen = glm::length(sv);
            if (svLen < 1e-10f)
                continue;

            Complex z = ProjectToTangent(sv, bases[si]);
            // Normalize to unit
            double zLen = std::abs(z);
            if (zLen < 1e-12)
                continue;
            z /= zLen;

            // RHS is a Dirac impulse (integrated 0-form), matching the
            // scalar heat method convention where rhs[s] = 1.0.
            rhsReal[si] = z.real();
            rhsImag[si] = z.imag();
        }

        // Solve (M + t·Δ_∇) X = M·X₀
        std::vector<double> xReal(nV, 0.0);
        std::vector<double> xImag(nV, 0.0);

        DEC::CGParams cgParams;
        cgParams.MaxIterations = params.MaxSolverIterations;
        cgParams.Tolerance = params.SolverTolerance;

        auto solveResult = SolveComplexCGShifted(
            mass, 1.0,
            connLap, t,
            rhsReal, rhsImag,
            xReal, xImag,
            cgParams);

        // Build result
        VectorHeatResult result;
        result.SolveIterations = solveResult.Iterations;
        result.Converged = solveResult.Converged;

        result.TransportedVectors = VertexProperty<glm::vec3>(
            mesh.VertexProperties().GetOrAdd<glm::vec3>("v:transported_vector", glm::vec3(0.0f)));
        result.TransportedAngles = VertexProperty<double>(
            mesh.VertexProperties().GetOrAdd<double>("v:transported_angle", 0.0));

        // Normalize and reconstruct 3D vectors
        for (std::size_t vi = 0; vi < nV; ++vi)
        {
            VertexHandle vh{static_cast<PropertyIndex>(vi)};
            if (mesh.IsDeleted(vh) || mesh.IsIsolated(vh) || !bases[vi].Valid)
            {
                result.TransportedVectors[vh] = glm::vec3(0.0f);
                result.TransportedAngles[vh] = 0.0;
                continue;
            }

            Complex z(xReal[vi], xImag[vi]);
            double mag = std::abs(z);

            if (mag < 1e-12)
            {
                result.TransportedVectors[vh] = glm::vec3(0.0f);
                result.TransportedAngles[vh] = 0.0;
                continue;
            }

            // Normalize to unit
            z /= mag;

            result.TransportedVectors[vh] = ReconstructFromTangent(z, bases[vi]);
            result.TransportedAngles[vh] = std::arg(z);
        }

        return result;
    }

    // =========================================================================
    // Logarithmic map implementation
    // =========================================================================

    std::optional<LogarithmicMapResult> ComputeLogMap(
        Halfedge::Mesh& mesh,
        std::size_t sourceVertex,
        const VectorHeatParams& params)
    {
        if (mesh.IsEmpty() || mesh.FaceCount() == 0)
            return std::nullopt;

        const std::size_t nV = mesh.VerticesSize();
        if (sourceVertex >= nV)
            return std::nullopt;

        VertexHandle srcVh{static_cast<PropertyIndex>(sourceVertex)};
        if (mesh.IsDeleted(srcVh) || mesh.IsIsolated(srcVh))
            return std::nullopt;

        // Step 1: Compute scalar geodesic distances (heat method)
        std::vector<std::size_t> sources = {sourceVertex};
        Geodesic::GeodesicParams geoParams;
        geoParams.TimeStep = params.TimeStep;
        geoParams.SolverTolerance = params.SolverTolerance;
        geoParams.MaxSolverIterations = params.MaxSolverIterations;

        auto geoResult = Geodesic::ComputeDistance(mesh, sources, geoParams);
        if (!geoResult.has_value())
            return std::nullopt;

        // Step 2: Transport a reference direction from the source
        // Use the tangent basis e1 direction at the source as the reference
        auto bases = BuildTangentBases(mesh);
        if (!bases[sourceVertex].Valid)
            return std::nullopt;

        glm::vec3 refDir = bases[sourceVertex].E1;
        std::vector<glm::vec3> srcVecs = {refDir};

        auto transportResult = TransportVectors(mesh, sources, srcVecs, params);
        if (!transportResult.has_value())
            return std::nullopt;

        // Step 3: Combine distance and direction into log map coordinates
        LogarithmicMapResult result;
        result.VectorSolveIterations = transportResult->SolveIterations;
        result.ScalarSolveIterations = geoResult->HeatSolveIterations;
        result.PoissonSolveIterations = geoResult->PoissonSolveIterations;
        result.Converged = geoResult->Converged && transportResult->Converged;

        result.LogMapCoords = VertexProperty<glm::vec2>(
            mesh.VertexProperties().GetOrAdd<glm::vec2>("v:logmap_coords", glm::vec2(0.0f)));
        result.Distance = geoResult->DistanceProperty;

        for (std::size_t vi = 0; vi < nV; ++vi)
        {
            VertexHandle vh{static_cast<PropertyIndex>(vi)};
            if (mesh.IsDeleted(vh) || mesh.IsIsolated(vh))
            {
                result.LogMapCoords[vh] = glm::vec2(0.0f);
                continue;
            }

            double dist = geoResult->DistanceProperty[vh];
            double angle = transportResult->TransportedAngles[vh];

            // Log map: polar coords (r, θ) → Cartesian in source tangent plane
            // The transported angle gives the direction from source to this vertex
            // We negate the angle because the transported vector points from source
            // toward vertex, but the log map angle is measured at the source
            result.LogMapCoords[vh] = glm::vec2(
                static_cast<float>(dist * std::cos(angle)),
                static_cast<float>(dist * std::sin(angle)));
        }

        return result;
    }

} // namespace Geometry::VectorHeatMethod

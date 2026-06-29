module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

module Geometry.SignedHeatMethod;

import Geometry.Properties;
import Geometry.HalfedgeMesh;
import Geometry.DEC;
import Geometry.HalfedgeMesh.Utils;
import Geometry.Sparse;

namespace Geometry::SignedHeatMethod
{
    namespace
    {
        constexpr double kEpsilon = 1.0e-12;

        struct FaceVector
        {
            glm::dvec3 Direction{0.0};
            bool Valid{false};
        };

        struct BoundarySource
        {
            std::vector<glm::dvec3> Vectors{};
            std::vector<double> Weights{};
            std::vector<bool> IsSource{};
            std::size_t SourceVertexCount{0};
            std::size_t InvalidHalfedgeCount{0};
            std::size_t DegenerateVertexCount{0};
        };

        [[nodiscard]] bool IsFinite(const glm::dvec3& value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        [[nodiscard]] glm::dvec3 ToDVec3(const glm::vec3& value) noexcept
        {
            return {static_cast<double>(value.x), static_cast<double>(value.y), static_cast<double>(value.z)};
        }

        void InitializeProperties(HalfedgeMesh::Mesh& mesh, SignedHeatResult& result)
        {
            result.SignedDistanceProperty = VertexProperty<double>(
                mesh.VertexProperties().GetOrAdd<double>("v:signed_heat_distance", 0.0));
            result.IsBoundarySourceProperty = VertexProperty<bool>(
                mesh.VertexProperties().GetOrAdd<bool>("v:is_signed_heat_source", false));

            for (std::size_t vi = 0; vi < mesh.VerticesSize(); ++vi)
            {
                VertexHandle v{static_cast<PropertyIndex>(vi)};
                result.SignedDistanceProperty[v] = 0.0;
                result.IsBoundarySourceProperty[v] = false;
            }
        }

        [[nodiscard]] bool HasValidTriangleFace(const HalfedgeMesh::Mesh& mesh)
        {
            for (std::size_t fi = 0; fi < mesh.FacesSize(); ++fi)
            {
                MeshUtils::TriangleFaceView tri{};
                if (MeshUtils::TryGetTriangleFaceView(mesh, FaceHandle{static_cast<PropertyIndex>(fi)}, tri))
                {
                    const glm::dvec3 areaVector = MeshUtils::FaceAreaVector(mesh, tri.Face);
                    if (glm::length(areaVector) > kEpsilon && IsFinite(areaVector))
                    {
                        return true;
                    }
                }
            }
            return false;
        }

        [[nodiscard]] Geometry::Sparse::SparseMatrix CombineMassAndLaplacian(
            const DEC::DiagonalMatrix& mass,
            double massScale,
            const DEC::SparseMatrix& laplacian,
            double laplacianScale)
        {
            Geometry::Sparse::SparseBuilder builder(laplacian.Rows, laplacian.Cols);
            builder.Reserve(laplacian.Values.size() + mass.Diagonal.size());

            for (std::size_t row = 0; row < laplacian.Rows; ++row)
            {
                for (std::size_t k = laplacian.RowOffsets[row]; k < laplacian.RowOffsets[row + 1]; ++k)
                {
                    builder.Add(row, laplacian.ColIndices[k], laplacianScale * laplacian.Values[k]);
                }
            }

            const std::size_t diagonalCount = std::min(mass.Size, mass.Diagonal.size());
            for (std::size_t i = 0; i < diagonalCount; ++i)
            {
                builder.Add(i, i, massScale * mass.Diagonal[i]);
            }

            return builder.Build().Matrix;
        }

        [[nodiscard]] glm::dvec3 BoundaryNormalForHalfedge(
            const HalfedgeMesh::Mesh& mesh,
            HalfedgeHandle h)
        {
            if (!mesh.IsValid(h) || mesh.IsDeleted(h))
            {
                return glm::dvec3(0.0);
            }

            FaceHandle face = mesh.Face(h);
            if (!face.IsValid())
            {
                face = mesh.Face(mesh.OppositeHalfedge(h));
            }
            if (!face.IsValid())
            {
                return glm::dvec3(0.0);
            }

            const glm::dvec3 pFrom = ToDVec3(mesh.Position(mesh.FromVertex(h)));
            const glm::dvec3 pTo = ToDVec3(mesh.Position(mesh.ToVertex(h)));
            const glm::dvec3 tangent = pTo - pFrom;
            const double tangentLength = glm::length(tangent);
            if (!(tangentLength > kEpsilon) || !IsFinite(tangent))
            {
                return glm::dvec3(0.0);
            }

            const glm::dvec3 areaVector = MeshUtils::FaceAreaVector(mesh, face);
            const double normalLength = glm::length(areaVector);
            if (!(normalLength > kEpsilon) || !IsFinite(areaVector))
            {
                return glm::dvec3(0.0);
            }

            const glm::dvec3 faceNormal = areaVector / normalLength;
            const glm::dvec3 curveNormal = glm::cross(faceNormal, tangent / tangentLength);
            const double curveNormalLength = glm::length(curveNormal);
            if (!(curveNormalLength > kEpsilon) || !IsFinite(curveNormal))
            {
                return glm::dvec3(0.0);
            }
            return curveNormal / curveNormalLength;
        }

        [[nodiscard]] BoundarySource BuildBoundarySource(
            const HalfedgeMesh::Mesh& mesh,
            std::span<const HalfedgeHandle> boundary)
        {
            const std::size_t nV = mesh.VerticesSize();
            BoundarySource source;
            source.Vectors.assign(nV, glm::dvec3(0.0));
            source.Weights.assign(nV, 0.0);
            source.IsSource.assign(nV, false);
            std::vector<std::size_t> endpointCounts(nV, 0);

            for (const HalfedgeHandle h : boundary)
            {
                if (!mesh.IsValid(h) || mesh.IsDeleted(h))
                {
                    ++source.InvalidHalfedgeCount;
                    continue;
                }

                const VertexHandle from = mesh.FromVertex(h);
                const VertexHandle to = mesh.ToVertex(h);
                if (!mesh.IsValid(from) || !mesh.IsValid(to) || mesh.IsDeleted(from) || mesh.IsDeleted(to))
                {
                    ++source.InvalidHalfedgeCount;
                    continue;
                }

                const glm::dvec3 pFrom = ToDVec3(mesh.Position(from));
                const glm::dvec3 pTo = ToDVec3(mesh.Position(to));
                const double edgeLength = glm::length(pTo - pFrom);
                const glm::dvec3 normal = BoundaryNormalForHalfedge(mesh, h);
                if (!(edgeLength > kEpsilon) || !(glm::length(normal) > kEpsilon) || !IsFinite(normal))
                {
                    ++source.InvalidHalfedgeCount;
                    continue;
                }

                const double endpointWeight = 0.5 * edgeLength;
                source.Vectors[from.Index] += endpointWeight * normal;
                source.Vectors[to.Index] += endpointWeight * normal;
                source.Weights[from.Index] += endpointWeight;
                source.Weights[to.Index] += endpointWeight;
                source.IsSource[from.Index] = true;
                source.IsSource[to.Index] = true;
                ++endpointCounts[from.Index];
                ++endpointCounts[to.Index];
            }

            for (std::size_t vi = 0; vi < nV; ++vi)
            {
                if (!source.IsSource[vi])
                {
                    continue;
                }
                ++source.SourceVertexCount;
                if (endpointCounts[vi] != 2)
                {
                    ++source.DegenerateVertexCount;
                }
            }
            return source;
        }

        [[nodiscard]] bool FactorAndSolve(
            const Geometry::Sparse::SparseMatrix& matrix,
            std::span<const double> rhs,
            std::span<double> x,
            Geometry::Sparse::SparseLDLT& solver,
            Geometry::Sparse::SparseFactorizationDiagnostics& factorDiagnostics,
            Geometry::Sparse::SparseFactorizationDiagnostics& solveDiagnostics)
        {
            factorDiagnostics = solver.factor(matrix);
            if (!factorDiagnostics.Succeeded())
            {
                solveDiagnostics = factorDiagnostics;
                return false;
            }

            solveDiagnostics = solver.solve(rhs, x);
            return solveDiagnostics.Succeeded();
        }

        [[nodiscard]] std::vector<FaceVector> ComputeNormalizedFaceField(
            const HalfedgeMesh::Mesh& mesh,
            std::span<const double> x,
            std::span<const double> y,
            std::span<const double> z)
        {
            std::vector<FaceVector> field(mesh.FacesSize());
            for (std::size_t fi = 0; fi < mesh.FacesSize(); ++fi)
            {
                MeshUtils::TriangleFaceView tri{};
                if (!MeshUtils::TryGetTriangleFaceView(mesh, FaceHandle{static_cast<PropertyIndex>(fi)}, tri))
                {
                    continue;
                }

                glm::dvec3 v{
                    x[tri.V0.Index] + x[tri.V1.Index] + x[tri.V2.Index],
                    y[tri.V0.Index] + y[tri.V1.Index] + y[tri.V2.Index],
                    z[tri.V0.Index] + z[tri.V1.Index] + z[tri.V2.Index]};
                v /= 3.0;

                const glm::dvec3 areaVector = MeshUtils::FaceAreaVector(mesh, tri.Face);
                const double normalLength = glm::length(areaVector);
                if (!(normalLength > kEpsilon) || !IsFinite(areaVector) || !IsFinite(v))
                {
                    continue;
                }
                const glm::dvec3 normal = areaVector / normalLength;
                v -= glm::dot(v, normal) * normal;

                const double length = glm::length(v);
                if (!(length > kEpsilon) || !IsFinite(v))
                {
                    continue;
                }
                field[fi].Direction = v / length;
                field[fi].Valid = true;
            }
            return field;
        }

        [[nodiscard]] std::vector<double> ComputeDivergence(
            const HalfedgeMesh::Mesh& mesh,
            const std::vector<FaceVector>& field)
        {
            std::vector<double> div(mesh.VerticesSize(), 0.0);

            for (std::size_t fi = 0; fi < mesh.FacesSize(); ++fi)
            {
                if (!field[fi].Valid)
                {
                    continue;
                }

                MeshUtils::TriangleFaceView tri{};
                if (!MeshUtils::TryGetTriangleFaceView(mesh, FaceHandle{static_cast<PropertyIndex>(fi)}, tri))
                {
                    continue;
                }

                const glm::dvec3 p0 = ToDVec3(tri.P0);
                const glm::dvec3 p1 = ToDVec3(tri.P1);
                const glm::dvec3 p2 = ToDVec3(tri.P2);
                const glm::vec3 e01 = tri.P1 - tri.P0;
                const glm::vec3 e02 = tri.P2 - tri.P0;
                const glm::vec3 e12 = tri.P2 - tri.P1;

                const double cot0 = MeshUtils::Cotan(e01, e02);
                const double cot1 = MeshUtils::Cotan(-e01, e12);
                const double cot2 = MeshUtils::Cotan(-e02, -e12);
                const glm::dvec3 xf = field[fi].Direction;

                div[tri.V0.Index] += 0.5 * (cot1 * glm::dot(p0 - p1, xf) + cot2 * glm::dot(p0 - p2, xf));
                div[tri.V1.Index] += 0.5 * (cot0 * glm::dot(p1 - p0, xf) + cot2 * glm::dot(p1 - p2, xf));
                div[tri.V2.Index] += 0.5 * (cot0 * glm::dot(p2 - p0, xf) + cot1 * glm::dot(p2 - p1, xf));
            }

            return div;
        }

        [[nodiscard]] double GradientAlignment(
            const HalfedgeMesh::Mesh& mesh,
            std::span<const double> phi,
            const std::vector<FaceVector>& field)
        {
            double alignment = 0.0;
            for (std::size_t fi = 0; fi < mesh.FacesSize(); ++fi)
            {
                if (!field[fi].Valid)
                {
                    continue;
                }
                const glm::dvec3 grad = MeshUtils::FaceScalarGradient(
                    mesh,
                    FaceHandle{static_cast<PropertyIndex>(fi)},
                    phi);
                if (IsFinite(grad))
                {
                    alignment += glm::dot(grad, field[fi].Direction);
                }
            }
            return alignment;
        }

        [[nodiscard]] bool AllFinite(std::span<const double> values)
        {
            return std::all_of(values.begin(), values.end(), [](double value) { return std::isfinite(value); });
        }

        [[nodiscard]] double BoundaryMean(
            std::span<const double> values,
            std::span<const double> weights)
        {
            double weightedSum = 0.0;
            double weightSum = 0.0;
            for (std::size_t i = 0; i < values.size(); ++i)
            {
                if (weights[i] > 0.0)
                {
                    weightedSum += weights[i] * values[i];
                    weightSum += weights[i];
                }
            }
            return (weightSum > 0.0) ? (weightedSum / weightSum) : 0.0;
        }

        void PublishResult(
            HalfedgeMesh::Mesh& mesh,
            SignedHeatResult& result,
            std::span<const double> phi,
            const BoundarySource& source)
        {
            for (std::size_t vi = 0; vi < mesh.VerticesSize(); ++vi)
            {
                VertexHandle v{static_cast<PropertyIndex>(vi)};
                if (mesh.IsDeleted(v) || mesh.IsIsolated(v))
                {
                    result.SignedDistanceProperty[v] = 0.0;
                    result.IsBoundarySourceProperty[v] = false;
                    continue;
                }

                result.SignedDistanceProperty[v] = phi[vi];
                result.IsBoundarySourceProperty[v] = source.IsSource[vi];
                result.Diagnostics.MaxAbsDistance = std::max(
                    result.Diagnostics.MaxAbsDistance,
                    std::abs(phi[vi]));
            }
        }
    }

    SignedHeatResult ComputeSignedDistance(
        HalfedgeMesh::Mesh& mesh,
        std::span<const HalfedgeHandle> boundary,
        const SignedHeatParams& params)
    {
        SignedHeatResult result;
        result.Diagnostics.BoundaryHalfedgeCount = boundary.size();
        InitializeProperties(mesh, result);

        if (mesh.IsEmpty() || mesh.FaceCount() == 0 || boundary.empty()
            || !std::isfinite(params.TimeStep)
            || !std::isfinite(params.PoissonRegularization)
            || params.PoissonRegularization <= 0.0
            || !HasValidTriangleFace(mesh))
        {
            result.Diagnostics.Status = SignedHeatStatus::InvalidInput;
            return result;
        }

        const BoundarySource source = BuildBoundarySource(mesh, boundary);
        result.Diagnostics.SourceVertexCount = source.SourceVertexCount;
        result.Diagnostics.InvalidBoundaryHalfedgeCount = source.InvalidHalfedgeCount;
        result.Diagnostics.DegenerateBoundaryVertexCount = source.DegenerateVertexCount;
        if (source.SourceVertexCount == 0)
        {
            result.Diagnostics.Status = SignedHeatStatus::InvalidInput;
            return result;
        }

        DEC::DECOperators ops = DEC::BuildOperators(mesh);
        if (!ops.IsValid())
        {
            result.Diagnostics.Status = SignedHeatStatus::OperatorAssemblyFailed;
            return result;
        }

        const double meanEdgeLength = MeshUtils::MeanEdgeLength(mesh);
        const double timeStep = params.TimeStep > 0.0 ? params.TimeStep : meanEdgeLength * meanEdgeLength;
        result.Diagnostics.MeanEdgeLength = meanEdgeLength;
        result.Diagnostics.TimeStep = timeStep;
        if (!(timeStep > 0.0) || !std::isfinite(timeStep))
        {
            result.Diagnostics.Status = SignedHeatStatus::InvalidInput;
            return result;
        }

        const Geometry::Sparse::SparseMatrix heatMatrix = CombineMassAndLaplacian(
            ops.Hodge0,
            1.0,
            ops.Laplacian,
            timeStep);

        std::vector<double> rhsX(mesh.VerticesSize(), 0.0);
        std::vector<double> rhsY(mesh.VerticesSize(), 0.0);
        std::vector<double> rhsZ(mesh.VerticesSize(), 0.0);
        for (std::size_t vi = 0; vi < mesh.VerticesSize(); ++vi)
        {
            rhsX[vi] = source.Vectors[vi].x;
            rhsY[vi] = source.Vectors[vi].y;
            rhsZ[vi] = source.Vectors[vi].z;
        }

        Geometry::Sparse::SparseLDLT heatSolver;
        Geometry::Sparse::SparseFactorizationDiagnostics heatSolveDiagnostics;
        std::vector<double> diffusedX(mesh.VerticesSize(), 0.0);
        if (!FactorAndSolve(
                heatMatrix,
                rhsX,
                diffusedX,
                heatSolver,
                result.Diagnostics.HeatFactorization,
                heatSolveDiagnostics))
        {
            result.Diagnostics.Status = result.Diagnostics.HeatFactorization.Succeeded()
                ? SignedHeatStatus::HeatSolveFailed
                : SignedHeatStatus::HeatFactorizationFailed;
            return result;
        }

        std::vector<double> diffusedY(mesh.VerticesSize(), 0.0);
        heatSolveDiagnostics = heatSolver.solve(rhsY, diffusedY);
        if (!heatSolveDiagnostics.Succeeded())
        {
            result.Diagnostics.Status = SignedHeatStatus::HeatSolveFailed;
            return result;
        }

        std::vector<double> diffusedZ(mesh.VerticesSize(), 0.0);
        heatSolveDiagnostics = heatSolver.solve(rhsZ, diffusedZ);
        if (!heatSolveDiagnostics.Succeeded())
        {
            result.Diagnostics.Status = SignedHeatStatus::HeatSolveFailed;
            return result;
        }

        if (!AllFinite(diffusedX) || !AllFinite(diffusedY) || !AllFinite(diffusedZ))
        {
            result.Diagnostics.Status = SignedHeatStatus::NonFiniteResult;
            return result;
        }

        const std::vector<FaceVector> field = ComputeNormalizedFaceField(
            mesh,
            diffusedX,
            diffusedY,
            diffusedZ);
        std::vector<double> divergence = ComputeDivergence(mesh, field);
        if (!AllFinite(divergence))
        {
            result.Diagnostics.Status = SignedHeatStatus::NonFiniteResult;
            return result;
        }

        const Geometry::Sparse::SparseMatrix poissonMatrix = CombineMassAndLaplacian(
            ops.Hodge0,
            params.PoissonRegularization,
            ops.Laplacian,
            1.0);

        Geometry::Sparse::SparseLDLT poissonSolver;
        Geometry::Sparse::SparseFactorizationDiagnostics poissonSolveDiagnostics;
        std::vector<double> phi(mesh.VerticesSize(), 0.0);
        if (!FactorAndSolve(
                poissonMatrix,
                divergence,
                phi,
                poissonSolver,
                result.Diagnostics.PoissonFactorization,
                poissonSolveDiagnostics))
        {
            result.Diagnostics.Status = result.Diagnostics.PoissonFactorization.Succeeded()
                ? SignedHeatStatus::PoissonSolveFailed
                : SignedHeatStatus::PoissonFactorizationFailed;
            return result;
        }

        if (!AllFinite(phi))
        {
            result.Diagnostics.Status = SignedHeatStatus::NonFiniteResult;
            return result;
        }

        if (GradientAlignment(mesh, phi, field) < 0.0)
        {
            for (double& value : phi)
            {
                value = -value;
            }
        }

        const double boundaryMean = BoundaryMean(phi, source.Weights);
        for (double& value : phi)
        {
            value -= boundaryMean;
        }
        result.Diagnostics.MeanBoundaryOffset = BoundaryMean(phi, source.Weights);

        if (!AllFinite(phi))
        {
            result.Diagnostics.Status = SignedHeatStatus::NonFiniteResult;
            return result;
        }

        result.Diagnostics.Status = result.Diagnostics.BoundaryDegenerate()
            ? SignedHeatStatus::DegenerateBoundaryInput
            : SignedHeatStatus::Success;
        PublishResult(mesh, result, phi, source);
        return result;
    }
}

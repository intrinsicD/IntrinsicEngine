module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

module Geometry.Curvature;

import Geometry.Properties;
import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.Utils;

namespace Geometry::Curvature
{
    using MeshUtils::ComputeMixedVoronoiAreas;
    using MeshUtils::ComputeCotanLaplacian;
    using MeshUtils::ComputeVertexAngleDefect;
    using MeshUtils::ComputeVertexAngleSums;
    using MeshUtils::EdgeCotanWeight;
    using MeshUtils::FaceAreaVector;
    using MeshUtils::VertexNormal;

    namespace
    {
        struct TensorVertex
        {
            glm::dvec3 Dir1{0.0};
            glm::dvec3 Dir2{0.0};
            double MaxPrincipal{0.0};
            double MinPrincipal{0.0};
            bool Valid{false};
        };

        struct EdgeTensorSample
        {
            glm::dvec3 WeightedDirection{0.0};
            double SignedDihedral{0.0};
            bool Valid{false};
        };

        struct SymmetricTensor
        {
            double M00{0.0};
            double M01{0.0};
            double M02{0.0};
            double M11{0.0};
            double M12{0.0};
            double M22{0.0};

            void AddOuterProduct(const glm::dvec3& vector, double scale) noexcept
            {
                M00 += scale * vector.x * vector.x;
                M01 += scale * vector.x * vector.y;
                M02 += scale * vector.x * vector.z;
                M11 += scale * vector.y * vector.y;
                M12 += scale * vector.y * vector.z;
                M22 += scale * vector.z * vector.z;
            }

            void Divide(double value) noexcept
            {
                M00 /= value;
                M01 /= value;
                M02 /= value;
                M11 /= value;
                M12 /= value;
                M22 /= value;
            }

            [[nodiscard]] glm::dvec3 Multiply(const glm::dvec3& vector) const noexcept
            {
                return {
                    M00 * vector.x + M01 * vector.y + M02 * vector.z,
                    M01 * vector.x + M11 * vector.y + M12 * vector.z,
                    M02 * vector.x + M12 * vector.y + M22 * vector.z};
            }

            [[nodiscard]] double MaxAbsCoefficient() const noexcept
            {
                return std::max({
                    std::abs(M00), std::abs(M01), std::abs(M02),
                    std::abs(M11), std::abs(M12), std::abs(M22)});
            }
        };

        [[nodiscard]] bool IsFinite(const glm::dvec3& value) noexcept
        {
            return std::isfinite(value.x)
                && std::isfinite(value.y)
                && std::isfinite(value.z);
        }

        // The framework24 edge-dihedral estimator accumulates signed interior
        // hinges over the vertex and its one-ring neighbours. Each contribution
        // uses half the edge length to match mixed vertex-area normalization.
        [[nodiscard]] std::vector<TensorVertex> ComputeTaubinTensor(HalfedgeMesh::Mesh& mesh)
        {
            constexpr double kTiny = 1e-18;
            const std::size_t nV = mesh.VerticesSize();
            const std::size_t nE = mesh.EdgesSize();
            const std::size_t nF = mesh.FacesSize();
            std::vector<TensorVertex> out(nV);
            const std::vector<double> mixedAreas = ComputeMixedVoronoiAreas(mesh);

            std::vector<glm::dvec3> faceNormals(nF, glm::dvec3(0.0));
            for (std::size_t fi = 0; fi < nF; ++fi)
            {
                const FaceHandle face{static_cast<PropertyIndex>(fi)};
                if (mesh.IsDeleted(face))
                    continue;

                const glm::dvec3 areaVector = FaceAreaVector(mesh, face);
                const double length = glm::length(areaVector);
                if (IsFinite(areaVector) && std::isfinite(length) && length > kTiny)
                    faceNormals[fi] = areaVector / length;
            }

            std::vector<EdgeTensorSample> edgeSamples(nE);
            for (std::size_t ei = 0; ei < nE; ++ei)
            {
                const EdgeHandle edge{static_cast<PropertyIndex>(ei)};
                if (mesh.IsDeleted(edge) || mesh.IsBoundary(edge))
                    continue;

                const HalfedgeHandle h0 = mesh.Halfedge(edge, 0u);
                const HalfedgeHandle h1 = mesh.OppositeHalfedge(h0);
                const FaceHandle f0 = mesh.Face(h0);
                const FaceHandle f1 = mesh.Face(h1);
                if (!f0.IsValid() || !f1.IsValid())
                    continue;

                const glm::dvec3 n0 = faceNormals[f0.Index];
                const glm::dvec3 n1 = faceNormals[f1.Index];
                if (glm::dot(n0, n0) <= kTiny || glm::dot(n1, n1) <= kTiny)
                    continue;

                // The direction is opposite h0, matching the face-order/sign
                // convention in atan2. Choosing h1 gives the same signed angle.
                glm::dvec3 direction = glm::dvec3(mesh.Position(mesh.FromVertex(h0)))
                                     - glm::dvec3(mesh.Position(mesh.ToVertex(h0)));
                const double length = glm::length(direction);
                if (!IsFinite(direction) || !std::isfinite(length) || length <= kTiny)
                    continue;
                direction /= length;

                // Negate the differential-geometry sign so an outward-oriented
                // convex surface follows this module's established positive-
                // curvature convention.
                const double signedDihedral = -std::atan2(
                    glm::dot(glm::cross(n0, n1), direction),
                    glm::dot(n0, n1));
                if (!std::isfinite(signedDihedral))
                    continue;

                edgeSamples[ei] = {
                    .WeightedDirection = std::sqrt(0.5 * length) * direction,
                    .SignedDihedral = signedDihedral,
                    .Valid = true};
            }

            for (std::size_t i = 0; i < nV; ++i)
            {
                const VertexHandle vertex{static_cast<PropertyIndex>(i)};
                if (mesh.IsDeleted(vertex) || mesh.IsIsolated(vertex))
                    continue;

                SymmetricTensor tensor;
                double supportArea = 0.0;
                bool supportValid = true;
                const auto accumulateSupportVertex = [&](const VertexHandle support)
                {
                    const double area = mixedAreas[support.Index];
                    if (!std::isfinite(area) || area < 0.0)
                    {
                        supportValid = false;
                        return;
                    }
                    supportArea += area;
                    for (const HalfedgeHandle halfedge : mesh.HalfedgesAroundVertex(support))
                    {
                        const EdgeTensorSample& sample = edgeSamples[mesh.Edge(halfedge).Index];
                        if (sample.Valid)
                            tensor.AddOuterProduct(sample.WeightedDirection, sample.SignedDihedral);
                    }
                };

                accumulateSupportVertex(vertex);
                for (const HalfedgeHandle halfedge : mesh.HalfedgesAroundVertex(vertex))
                    accumulateSupportVertex(mesh.ToVertex(halfedge));

                if (!supportValid || !std::isfinite(supportArea) || supportArea <= kTiny)
                    continue;
                tensor.Divide(supportArea);

                const double scale = tensor.MaxAbsCoefficient();
                if (!std::isfinite(scale) || scale <= 0.0)
                    continue;

                glm::dvec3 normal{0.0};
                for (const FaceHandle face : mesh.FacesAroundVertex(vertex))
                    normal += FaceAreaVector(mesh, face);
                const double normalLength = glm::length(normal);
                if (!IsFinite(normal) || !std::isfinite(normalLength) || normalLength <= kTiny)
                    continue;
                normal /= normalLength;

                const glm::dvec3 seed = std::abs(normal.x) < 0.9
                    ? glm::dvec3{1.0, 0.0, 0.0}
                    : glm::dvec3{0.0, 1.0, 0.0};
                glm::dvec3 tangent0 = glm::cross(normal, seed);
                tangent0 /= glm::length(tangent0);
                const glm::dvec3 tangent1 = glm::cross(normal, tangent0);

                // Restrict to the estimated tangent plane before diagonalizing.
                // A full 3x3 smallest-absolute-eigenvalue selection is ambiguous
                // on cylinders, where both the normal and one principal value are
                // zero. The 2x2 restriction keeps the published basis tangent.
                const double m00 = glm::dot(tangent0, tensor.Multiply(tangent0));
                const double m01 = glm::dot(tangent0, tensor.Multiply(tangent1));
                const double m11 = glm::dot(tangent1, tensor.Multiply(tangent1));
                const double meanEigenvalue = 0.5 * (m00 + m11);
                const double radius = std::hypot(0.5 * (m00 - m11), m01);
                const double rawMax = meanEigenvalue + radius;
                const double rawMin = meanEigenvalue - radius;
                const double angle = 0.5 * std::atan2(2.0 * m01, m00 - m11);
                const glm::dvec3 rawMaxDirection = std::cos(angle) * tangent0
                                                 + std::sin(angle) * tangent1;
                const glm::dvec3 rawMinDirection = -std::sin(angle) * tangent0
                                                 + std::cos(angle) * tangent1;

                // A hinge angle measures normal curvature across its edge while
                // the reference integral stores the contribution along that edge.
                // The two tangent eigenvalues are therefore paired with the
                // complementary eigenvectors when published as principal fields.
                const glm::dvec3 maxDirection = rawMinDirection;
                const glm::dvec3 minDirection = rawMaxDirection;
                const double maxPrincipal = rawMax;
                const double minPrincipal = rawMin;
                if (!IsFinite(maxDirection) || !IsFinite(minDirection)
                    || !std::isfinite(maxPrincipal) || !std::isfinite(minPrincipal))
                {
                    continue;
                }

                out[i].Dir1 = maxDirection;
                out[i].Dir2 = minDirection;
                out[i].MaxPrincipal = maxPrincipal;
                out[i].MinPrincipal = minPrincipal;
                out[i].Valid = true;
            }

            // Match framework24's three cotan post-smoothing passes,
            // but use separate buffers so traversal order and parallel scheduling
            // cannot change the result. Directions remain the tensor eigenvectors.
            std::vector<double> minPrincipal(nV, 0.0);
            std::vector<double> maxPrincipal(nV, 0.0);
            for (std::size_t i = 0; i < nV; ++i)
            {
                minPrincipal[i] = out[i].MinPrincipal;
                maxPrincipal[i] = out[i].MaxPrincipal;
            }

            for (int pass = 0; pass < 3; ++pass)
            {
                std::vector<double> nextMin = minPrincipal;
                std::vector<double> nextMax = maxPrincipal;
                for (std::size_t i = 0; i < nV; ++i)
                {
                    const VertexHandle vertex{static_cast<PropertyIndex>(i)};
                    if (!out[i].Valid || mesh.IsDeleted(vertex) || mesh.IsIsolated(vertex))
                        continue;

                    double weightSum = 0.0;
                    double minSum = 0.0;
                    double maxSum = 0.0;
                    for (const HalfedgeHandle halfedge : mesh.HalfedgesAroundVertex(vertex))
                    {
                        const VertexHandle neighbour = mesh.ToVertex(halfedge);
                        if (!out[neighbour.Index].Valid)
                            continue;
                        const double weight = std::max(0.0, EdgeCotanWeight(mesh, mesh.Edge(halfedge)));
                        if (!std::isfinite(weight) || weight <= 0.0)
                            continue;
                        weightSum += weight;
                        minSum += weight * minPrincipal[neighbour.Index];
                        maxSum += weight * maxPrincipal[neighbour.Index];
                    }
                    if (std::isfinite(weightSum) && weightSum > 0.0
                        && std::isfinite(minSum) && std::isfinite(maxSum))
                    {
                        nextMin[i] = minSum / weightSum;
                        nextMax[i] = maxSum / weightSum;
                    }
                }
                minPrincipal.swap(nextMin);
                maxPrincipal.swap(nextMax);
            }

            for (std::size_t i = 0; i < nV; ++i)
            {
                if (!out[i].Valid)
                    continue;
                out[i].MinPrincipal = minPrincipal[i];
                out[i].MaxPrincipal = maxPrincipal[i];
            }
            return out;
        }

        [[nodiscard]] double ComputeSignedMeanCurvatureFromLaplaceBeltrami(
            const glm::dvec3& laplaceB,
            const glm::dvec3& normal) noexcept
        {
            const double laplaceLen = glm::length(laplaceB);
            if (laplaceLen <= 1e-18)
                return 0.0;

            const double normalLen = glm::length(normal);
            if (normalLen <= 1e-18)
                return laplaceLen / 2.0;

            // This implementation uses the cotan Laplace-Beltrami operator
            //   Δx = (1/A) Σ_j w_ij (x_j - x_i)
            // which points approximately toward -n on outward-oriented convex
            // surfaces. Therefore the signed scalar mean curvature satisfies
            //   Δx = -2 H n.
            const double orientation = glm::dot(laplaceB, normal) / (laplaceLen * normalLen);
            const double magnitude = laplaceLen / 2.0;
            return (orientation > 0.0) ? -magnitude : magnitude;
        }
    }

    // Mean curvature normal at vertex i:
    //   Hn_i = (1 / 2A_i) * Σ_j (cot α_ij + cot β_ij) * (x_j - x_i)
    // The discrete Laplace-Beltrami satisfies ΔS x = -2H n.

    std::optional<MeanCurvatureResult> ComputeMeanCurvature(HalfedgeMesh::Mesh& mesh)
    {
        if (mesh.IsEmpty() || mesh.FaceCount() == 0)
            return std::nullopt;

        const std::size_t nV = mesh.VerticesSize();

        MeanCurvatureResult result;
        result.Property = VertexProperty<double>(mesh.VertexProperties().GetOrAdd<double>("v:mean_curvature", 0.0));

        auto areas = ComputeMixedVoronoiAreas(mesh);
        auto laplacian = ComputeCotanLaplacian(mesh);

        for (std::size_t i = 0; i < nV; ++i)
        {
            VertexHandle vh{static_cast<PropertyIndex>(i)};
            if (mesh.IsDeleted(vh) || mesh.IsIsolated(vh)) continue;

            if (areas[i] > 1e-12)
            {
                glm::dvec3 laplaceB = laplacian[i] / areas[i];
                const glm::dvec3 normal = glm::dvec3(VertexNormal(mesh, vh));
                result.Property[vh] = ComputeSignedMeanCurvatureFromLaplaceBeltrami(laplaceB, normal);
            }
        }

        return result;
    }

    // Discrete Gaussian curvature via angle defect (Descartes' theorem):
    //   K(v_i) = (2π - Σ_j θ_j) / A_i     for interior vertices
    //   K(v_i) = (π  - Σ_j θ_j) / A_i     for boundary vertices
    //
    // where θ_j is the angle at v_i in each incident triangle.

    std::optional<GaussianCurvatureResult> ComputeGaussianCurvature(HalfedgeMesh::Mesh& mesh)
    {
        if (mesh.IsEmpty() || mesh.FaceCount() == 0)
            return std::nullopt;

        const std::size_t nV = mesh.VerticesSize();

        GaussianCurvatureResult result;
        result.Property = VertexProperty<double>(mesh.VertexProperties().GetOrAdd<double>("v:gaussian_curvature", 0.0));

        auto areas = ComputeMixedVoronoiAreas(mesh);

        const std::vector<double> vertexAngleSums = ComputeVertexAngleSums(mesh);

        for (std::size_t i = 0; i < nV; ++i)
        {
            VertexHandle vh{static_cast<PropertyIndex>(i)};
            if (mesh.IsDeleted(vh) || mesh.IsIsolated(vh)) continue;

            if (areas[i] > 1e-12)
            {
                const double defect = ComputeVertexAngleDefect(mesh, vh, vertexAngleSums[i]);
                result.Property[vh] = defect / areas[i];
            }
        }

        return result;
    }

    CurvatureField ComputeCurvature(HalfedgeMesh::Mesh& mesh)
    {
        const std::size_t nV = mesh.VerticesSize();

        CurvatureField result;
        result.MeanCurvatureProperty = VertexProperty<double>(mesh.VertexProperties().GetOrAdd<double>("v:mean_curvature", 0.0));
        result.GaussianCurvatureProperty = VertexProperty<double>(mesh.VertexProperties().GetOrAdd<double>("v:gaussian_curvature", 0.0));
        result.MinPrincipalCurvatureProperty = VertexProperty<double>(mesh.VertexProperties().GetOrAdd<double>("v:min_principal_curvature", 0.0));
        result.MaxPrincipalCurvatureProperty = VertexProperty<double>(mesh.VertexProperties().GetOrAdd<double>("v:max_principal_curvature", 0.0));
        result.MeanCurvatureNormalProperty = VertexProperty<glm::vec3>(mesh.VertexProperties().GetOrAdd<glm::vec3>("v:mean_curvature_normal", glm::vec3(0.0f)));
        result.PrincipalDir1Property = VertexProperty<glm::vec3>(mesh.VertexProperties().GetOrAdd<glm::vec3>("v:principal_dir1", glm::vec3(0.0f)));
        result.PrincipalDir2Property = VertexProperty<glm::vec3>(mesh.VertexProperties().GetOrAdd<glm::vec3>("v:principal_dir2", glm::vec3(0.0f)));

        const std::vector<TensorVertex> tensor = ComputeTaubinTensor(mesh);
        for (std::size_t i = 0; i < nV; ++i)
        {
            const VertexHandle vertex{static_cast<PropertyIndex>(i)};
            result.MeanCurvatureProperty[vertex] = 0.0;
            result.GaussianCurvatureProperty[vertex] = 0.0;
            result.MaxPrincipalCurvatureProperty[vertex] = 0.0;
            result.MinPrincipalCurvatureProperty[vertex] = 0.0;
            result.MeanCurvatureNormalProperty[vertex] = glm::vec3(0.0f);
            result.PrincipalDir1Property[vertex] = glm::vec3(0.0f);
            result.PrincipalDir2Property[vertex] = glm::vec3(0.0f);
            if (mesh.IsDeleted(vertex) || !tensor[i].Valid)
                continue;

            const double maxPrincipal = tensor[i].MaxPrincipal;
            const double minPrincipal = tensor[i].MinPrincipal;
            const double mean = 0.5 * (maxPrincipal + minPrincipal);
            result.MeanCurvatureProperty[vertex] = mean;
            result.GaussianCurvatureProperty[vertex] = maxPrincipal * minPrincipal;
            result.MaxPrincipalCurvatureProperty[vertex] = maxPrincipal;
            result.MinPrincipalCurvatureProperty[vertex] = minPrincipal;
            result.PrincipalDir1Property[vertex] = glm::vec3(tensor[i].Dir1);
            result.PrincipalDir2Property[vertex] = glm::vec3(tensor[i].Dir2);

            const glm::dvec3 normal(VertexNormal(mesh, vertex));
            const double normalLength = glm::length(normal);
            if (IsFinite(normal) && std::isfinite(normalLength) && normalLength > 1e-18)
                result.MeanCurvatureNormalProperty[vertex] = glm::vec3(-mean * normal / normalLength);
        }

        return result;
    }

    std::optional<CurvatureTensorResult> ComputeCurvatureTensor(HalfedgeMesh::Mesh& mesh)
    {
        if (mesh.IsEmpty() || mesh.FaceCount() == 0)
            return std::nullopt;

        const std::size_t nV = mesh.VerticesSize();

        CurvatureTensorResult result;
        result.PrincipalDir1Property = VertexProperty<glm::vec3>(mesh.VertexProperties().GetOrAdd<glm::vec3>("v:principal_dir1", glm::vec3(0.0f)));
        result.PrincipalDir2Property = VertexProperty<glm::vec3>(mesh.VertexProperties().GetOrAdd<glm::vec3>("v:principal_dir2", glm::vec3(0.0f)));
        result.MaxPrincipalCurvatureProperty = VertexProperty<double>(mesh.VertexProperties().GetOrAdd<double>("v:max_principal_curvature", 0.0));
        result.MinPrincipalCurvatureProperty = VertexProperty<double>(mesh.VertexProperties().GetOrAdd<double>("v:min_principal_curvature", 0.0));

        const std::vector<TensorVertex> tensor = ComputeTaubinTensor(mesh);

        for (std::size_t i = 0; i < nV; ++i)
        {
            const VertexHandle vertex{static_cast<PropertyIndex>(i)};
            result.PrincipalDir1Property[vertex] = glm::vec3(0.0f);
            result.PrincipalDir2Property[vertex] = glm::vec3(0.0f);
            result.MaxPrincipalCurvatureProperty[vertex] = 0.0;
            result.MinPrincipalCurvatureProperty[vertex] = 0.0;
            if (mesh.IsDeleted(vertex) || !tensor[i].Valid)
                continue;

            result.PrincipalDir1Property[vertex] = glm::vec3(tensor[i].Dir1);
            result.PrincipalDir2Property[vertex] = glm::vec3(tensor[i].Dir2);
            result.MaxPrincipalCurvatureProperty[vertex] = tensor[i].MaxPrincipal;
            result.MinPrincipalCurvatureProperty[vertex] = tensor[i].MinPrincipal;
        }

        return result;
    }
} // namespace Geometry::Curvature

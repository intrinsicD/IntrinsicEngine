// Implements discrete scalar operators and the corrected Framework24
// one-ring edge-dihedral tensor published through canonical vertex fields.
module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

#include <Eigen/Eigenvalues>
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
    using MeshUtils::FaceAreaVector;
    using MeshUtils::TryGetTriangleFaceView;
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
            bool DirectionValid{false};
        };

        struct EdgeTensorSample
        {
            glm::dvec3 WeightedDirection{0.0};
            double SignedDihedral{0.0};
            bool Valid{false};
        };

        struct TensorComputation
        {
            std::vector<TensorVertex> Vertices{};
            std::size_t DegenerateFaceCount{0u};
            std::size_t IllConditionedFaceCount{0u};
            std::size_t UnsupportedFaceCount{0u};
            double MinimumTriangleQuality{0.0};
        };

        [[nodiscard]] bool IsFinite(const glm::dvec3& value) noexcept
        {
            return std::isfinite(value.x)
                && std::isfinite(value.y)
                && std::isfinite(value.z);
        }

        [[nodiscard]] glm::dvec3 GeometricVertexNormal(
            const HalfedgeMesh::Mesh& mesh,
            const VertexHandle vertex) noexcept
        {
            glm::dvec3 normal{0.0};
            for (const FaceHandle face : mesh.FacesAroundVertex(vertex))
                normal += FaceAreaVector(mesh, face);
            const double length = glm::length(normal);
            if (!IsFinite(normal) || !std::isfinite(length)
                || length <= std::numeric_limits<double>::min())
                return glm::dvec3{0.0};
            return normal / length;
        }

        constexpr double kFramework24CotanBound = 19.1;
        [[nodiscard]] double Framework24ClampCotan(
            const double value) noexcept
        {
            return std::clamp(
                value, -kFramework24CotanBound, kFramework24CotanBound);
        }

        [[nodiscard]] double Framework24TriangleArea(
            double a, double b, double c) noexcept
        {
            if (a < b)
                std::swap(a, b);
            if (a < c)
                std::swap(a, c);
            if (b < c)
                std::swap(b, c);
            const double radicand =
                (a + (b + c)) * (c - (a - b))
                * (c + (a - b)) * (a + (b - c));
            if (!std::isfinite(radicand))
                return 0.0;
            return 0.25 * std::sqrt(std::abs(radicand));
        }

        [[nodiscard]] double Framework24TriangleArea(
            const glm::dvec3& p,
            const glm::dvec3& q,
            const glm::dvec3& r) noexcept
        {
            return Framework24TriangleArea(
                glm::length(q - p),
                glm::length(r - q),
                glm::length(r - p));
        }

        // Framework24 revision 6dd50a82 uses the Meyer mixed-area allocation
        // and a clamped cotangent evaluated against twice the triangle area.
        [[nodiscard]] std::vector<double> ComputeFramework24VertexAreas(
            const HalfedgeMesh::Mesh& mesh)
        {
            std::vector<double> areas(mesh.VerticesSize(), 0.0);
            for (std::size_t i = 0u; i < mesh.VerticesSize(); ++i)
            {
                const VertexHandle vertex{static_cast<PropertyIndex>(i)};
                if (mesh.IsDeleted(vertex) || mesh.IsIsolated(vertex))
                    continue;

                double areaSum = 0.0;
                for (const HalfedgeHandle halfedge :
                     mesh.HalfedgesAroundVertex(vertex))
                {
                    if (mesh.IsBoundary(halfedge))
                        continue;
                    const HalfedgeHandle next = mesh.NextHalfedge(halfedge);
                    const glm::dvec3 p(mesh.Position(
                        mesh.FromVertex(halfedge)));
                    const glm::dvec3 q(mesh.Position(
                        mesh.ToVertex(halfedge)));
                    const glm::dvec3 r(mesh.Position(mesh.ToVertex(next)));
                    if (!IsFinite(p) || !IsFinite(q) || !IsFinite(r))
                        continue;

                    const glm::dvec3 pq = q - p;
                    const glm::dvec3 qr = r - q;
                    const glm::dvec3 pr = r - p;
                    const double triangleArea =
                        Framework24TriangleArea(p, q, r);
                    if (!std::isfinite(triangleArea)
                        || triangleArea
                            <= std::numeric_limits<double>::epsilon())
                    {
                        continue;
                    }

                    const double dotP = glm::dot(pq, pr);
                    const double dotQ = glm::dot(-qr, pq);
                    const double dotR = glm::dot(qr, pr);
                    if (dotP < 0.0)
                    {
                        areaSum += 0.5 * triangleArea;
                    }
                    else if (dotQ < 0.0 || dotR < 0.0)
                    {
                        areaSum += 0.25 * triangleArea;
                    }
                    else
                    {
                        const double twiceArea = 2.0 * triangleArea;
                        const double cotQ = Framework24ClampCotan(
                            dotQ / twiceArea);
                        const double cotR = Framework24ClampCotan(
                            dotR / twiceArea);
                        areaSum += 0.125
                            * (glm::dot(pr, pr) * cotQ
                               + glm::dot(pq, pq) * cotR);
                    }
                }
                areas[i] = areaSum;
            }
            return areas;
        }

        // Direct deterministic port of the corrected Framework24 default:
        // one-ring support with no implicit principal-value smoothing.
        [[nodiscard]] TensorComputation ComputeEdgeDihedralTensor(
            HalfedgeMesh::Mesh& mesh)
        {
            constexpr double kDirectionTiny =
                64.0 * std::numeric_limits<double>::epsilon();
            const std::size_t vertexCount = mesh.VerticesSize();
            const std::size_t edgeCount = mesh.EdgesSize();
            const std::size_t faceCount = mesh.FacesSize();
            TensorComputation computation{};
            computation.Vertices.resize(vertexCount);
            std::vector<TensorVertex>& out = computation.Vertices;
            const std::vector<double> mixedAreas =
                ComputeFramework24VertexAreas(mesh);

            std::vector<glm::dvec3> faceNormals(
                faceCount, glm::dvec3(0.0));
            std::vector<std::uint8_t> reliableSupport(vertexCount, 1u);
            bool hasTriangleQuality = false;
            for (std::size_t i = 0u; i < faceCount; ++i)
            {
                const FaceHandle face{static_cast<PropertyIndex>(i)};
                if (mesh.IsDeleted(face))
                    continue;

                MeshUtils::TriangleFaceView triangle{};
                if (!TryGetTriangleFaceView(mesh, face, triangle))
                {
                    ++computation.UnsupportedFaceCount;
                    for (const VertexHandle vertex :
                         mesh.VerticesAroundFace(face))
                    {
                        reliableSupport[vertex.Index] = 0u;
                    }
                    continue;
                }

                const glm::dvec3 p0(triangle.P0);
                const glm::dvec3 p1(triangle.P1);
                const glm::dvec3 p2(triangle.P2);
                const glm::dvec3 e01 = p1 - p0;
                const glm::dvec3 e02 = p2 - p0;
                const glm::dvec3 e12 = p2 - p1;
                const glm::dvec3 twiceAreaVector = glm::cross(e01, e02);
                const double twiceArea = glm::length(twiceAreaVector);
                const double maximumEdgeSquared = std::max({
                    glm::dot(e01, e01),
                    glm::dot(e02, e02),
                    glm::dot(e12, e12)});
                bool reliable = IsFinite(p0) && IsFinite(p1) && IsFinite(p2)
                    && IsFinite(twiceAreaVector)
                    && std::isfinite(twiceArea)
                    && std::isfinite(maximumEdgeSquared)
                    && twiceArea > std::numeric_limits<double>::min()
                    && maximumEdgeSquared > std::numeric_limits<double>::min();
                if (!reliable)
                {
                    ++computation.DegenerateFaceCount;
                }
                else
                {
                    const double quality = twiceArea / maximumEdgeSquared;
                    if (!hasTriangleQuality)
                    {
                        computation.MinimumTriangleQuality = quality;
                        hasTriangleQuality = true;
                    }
                    else
                    {
                        computation.MinimumTriangleQuality = std::min(
                            computation.MinimumTriangleQuality, quality);
                    }
                    if (!std::isfinite(quality)
                        || quality <= kMinimumReliableTriangleQuality)
                    {
                        ++computation.IllConditionedFaceCount;
                        reliable = false;
                    }
                }

                if (!reliable)
                {
                    reliableSupport[triangle.V0.Index] = 0u;
                    reliableSupport[triangle.V1.Index] = 0u;
                    reliableSupport[triangle.V2.Index] = 0u;
                    continue;
                }
                faceNormals[i] = twiceAreaVector / twiceArea;
            }

            std::vector<EdgeTensorSample> edgeSamples(edgeCount);
            for (std::size_t i = 0u; i < edgeCount; ++i)
            {
                const EdgeHandle edge{static_cast<PropertyIndex>(i)};
                if (mesh.IsDeleted(edge) || mesh.IsBoundary(edge))
                    continue;
                const HalfedgeHandle halfedge0 = mesh.Halfedge(edge, 0u);
                const HalfedgeHandle halfedge1 =
                    mesh.OppositeHalfedge(halfedge0);
                const FaceHandle face0 = mesh.Face(halfedge0);
                const FaceHandle face1 = mesh.Face(halfedge1);
                if (!face0.IsValid() || !face1.IsValid())
                    continue;
                const glm::dvec3 normal0 = faceNormals[face0.Index];
                const glm::dvec3 normal1 = faceNormals[face1.Index];
                if (glm::dot(normal0, normal0) <= kDirectionTiny
                    || glm::dot(normal1, normal1) <= kDirectionTiny)
                {
                    continue;
                }

                glm::dvec3 direction =
                    glm::dvec3(mesh.Position(mesh.FromVertex(halfedge0)))
                    - glm::dvec3(mesh.Position(mesh.ToVertex(halfedge0)));
                const double length = glm::length(direction);
                if (!IsFinite(direction) || !std::isfinite(length)
                    || length <= std::numeric_limits<double>::min())
                {
                    continue;
                }
                direction /= length;
                const double signedDihedral = -std::atan2(
                    glm::dot(glm::cross(normal0, normal1), direction),
                    glm::dot(normal0, normal1));
                if (!std::isfinite(signedDihedral))
                    continue;
                edgeSamples[i] = EdgeTensorSample{
                    .WeightedDirection =
                        std::sqrt(0.5 * length) * direction,
                    .SignedDihedral = signedDihedral,
                    .Valid = true,
                };
            }

            for (std::size_t i = 0u; i < vertexCount; ++i)
            {
                const VertexHandle vertex{static_cast<PropertyIndex>(i)};
                if (mesh.IsDeleted(vertex) || mesh.IsIsolated(vertex))
                    continue;

                const double supportArea = mixedAreas[i];
                Eigen::Matrix3d tensor = Eigen::Matrix3d::Zero();
                if (reliableSupport[i] == 0u
                    || !std::isfinite(supportArea)
                    || supportArea <= std::numeric_limits<double>::min())
                {
                    continue;
                }
                for (const HalfedgeHandle halfedge :
                     mesh.HalfedgesAroundVertex(vertex))
                {
                    const EdgeTensorSample& sample =
                        edgeSamples[mesh.Edge(halfedge).Index];
                    if (sample.Valid)
                    {
                        for (int row = 0; row < 3; ++row)
                        {
                            for (int column = 0; column < 3; ++column)
                            {
                                tensor(row, column) +=
                                    sample.SignedDihedral
                                    * sample.WeightedDirection[row]
                                    * sample.WeightedDirection[column];
                            }
                        }
                    }
                }
                tensor /= supportArea;
                Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> solver(
                    tensor);
                if (solver.info() != Eigen::Success)
                    continue;

                const Eigen::Vector3d eigenvalues = solver.eigenvalues();
                const double absolute0 = std::abs(eigenvalues[0]);
                const double absolute1 = std::abs(eigenvalues[1]);
                const double absolute2 = std::abs(eigenvalues[2]);
                int minimumIndex = 0;
                int maximumIndex = 1;
                if (absolute0 < absolute1)
                {
                    if (absolute0 < absolute2)
                    {
                        minimumIndex = 1;
                        maximumIndex = 2;
                    }
                }
                else
                {
                    if (absolute1 < absolute2)
                    {
                        minimumIndex = 0;
                        maximumIndex = 2;
                    }
                }
                out[i].MinPrincipal = eigenvalues[minimumIndex];
                out[i].MaxPrincipal = eigenvalues[maximumIndex];
                out[i].Valid =
                    std::isfinite(out[i].MaxPrincipal)
                    && std::isfinite(out[i].MinPrincipal);
                if (!out[i].Valid)
                    continue;
                const Eigen::Vector3d maximumDirection =
                    solver.eigenvectors().col(maximumIndex);
                const Eigen::Vector3d minimumDirection =
                    solver.eigenvectors().col(minimumIndex);
                out[i].Dir1 = {
                    maximumDirection[0],
                    maximumDirection[1],
                    maximumDirection[2]};
                out[i].Dir2 = {
                    minimumDirection[0],
                    minimumDirection[1],
                    minimumDirection[2]};
                out[i].DirectionValid = IsFinite(out[i].Dir1)
                    && IsFinite(out[i].Dir2);
            }

            if (computation.DegenerateFaceCount > 0u
                || computation.UnsupportedFaceCount > 0u)
            {
                computation.MinimumTriangleQuality = 0.0;
            }
            return computation;
        }

        // Publishes the corrected deterministic Framework24 principal field.
        [[nodiscard]] CurvatureDiagnostics PublishTensorFields(
            HalfedgeMesh::Mesh& mesh,
            const TensorComputation& computation,
            VertexProperty<glm::vec3> direction1Property,
            VertexProperty<glm::vec3> direction2Property,
            VertexProperty<double> maxPrincipalProperty,
            VertexProperty<double> minPrincipalProperty)
        {
            const std::vector<TensorVertex>& tensor = computation.Vertices;
            const std::size_t vertexCount = mesh.VerticesSize();
            for (std::size_t i = 0u; i < vertexCount; ++i)
            {
                const VertexHandle vertex{static_cast<PropertyIndex>(i)};
                direction1Property[vertex] = glm::vec3(0.0f);
                direction2Property[vertex] = glm::vec3(0.0f);
                maxPrincipalProperty[vertex] = 0.0;
                minPrincipalProperty[vertex] = 0.0;
                if (mesh.IsDeleted(vertex) || !tensor[i].Valid)
                    continue;
                maxPrincipalProperty[vertex] = tensor[i].MaxPrincipal;
                minPrincipalProperty[vertex] = tensor[i].MinPrincipal;
                if (tensor[i].DirectionValid)
                {
                    direction1Property[vertex] = glm::vec3(tensor[i].Dir1);
                    direction2Property[vertex] = glm::vec3(tensor[i].Dir2);
                }
            }

            CurvatureDiagnostics diagnostics{};
            diagnostics.DegenerateFaceCount =
                computation.DegenerateFaceCount;
            diagnostics.IllConditionedFaceCount =
                computation.IllConditionedFaceCount;
            diagnostics.UnsupportedFaceCount =
                computation.UnsupportedFaceCount;
            diagnostics.MinimumTriangleQuality =
                computation.MinimumTriangleQuality;
            bool hasRange = false;
            const VertexProperty<double>& finalMin = minPrincipalProperty;
            const VertexProperty<double>& finalMax = maxPrincipalProperty;
            for (std::size_t i = 0u; i < vertexCount; ++i)
            {
                const VertexHandle vertex{static_cast<PropertyIndex>(i)};
                if (mesh.IsDeleted(vertex) || !tensor[i].Valid)
                    continue;
                ++diagnostics.SupportedVertexCount;
                const double minimum = finalMin[vertex];
                const double maximum = finalMax[vertex];
                if (minimum != 0.0 || maximum != 0.0)
                {
                    ++diagnostics.NonZeroPrincipalVertexCount;
                }
                if (!hasRange)
                {
                    diagnostics.MinimumPrincipalValue = minimum;
                    diagnostics.MaximumPrincipalValue = maximum;
                    hasRange = true;
                }
                else
                {
                    diagnostics.MinimumPrincipalValue = std::min(
                        diagnostics.MinimumPrincipalValue, minimum);
                    diagnostics.MaximumPrincipalValue = std::max(
                        diagnostics.MaximumPrincipalValue, maximum);
                }
            }
            return diagnostics;
        }

        [[nodiscard]] double ComputeSignedMeanCurvatureFromLaplaceBeltrami(
            const glm::dvec3& laplaceB,
            const glm::dvec3& normal) noexcept
        {
            const double laplaceLen = glm::length(laplaceB);
            if (!std::isfinite(laplaceLen)
                || laplaceLen <= std::numeric_limits<double>::min())
                return 0.0;

            const double normalLen = glm::length(normal);
            if (!std::isfinite(normalLen)
                || normalLen <= std::numeric_limits<double>::min())
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

            result.Property[vh] = 0.0;
            if (std::isfinite(areas[i])
                && areas[i] > std::numeric_limits<double>::min())
            {
                glm::dvec3 laplaceB = laplacian[i] / areas[i];
                const glm::dvec3 normal = GeometricVertexNormal(mesh, vh);
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

            result.Property[vh] = 0.0;
            if (std::isfinite(areas[i])
                && areas[i] > std::numeric_limits<double>::min())
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

        const TensorComputation tensor = ComputeEdgeDihedralTensor(mesh);
        result.Diagnostics = PublishTensorFields(
            mesh,
            tensor,
            result.PrincipalDir1Property,
            result.PrincipalDir2Property,
            result.MaxPrincipalCurvatureProperty,
            result.MinPrincipalCurvatureProperty);
        const VertexProperty<double>& maxPrincipalProperty =
            result.MaxPrincipalCurvatureProperty;
        const VertexProperty<double>& minPrincipalProperty =
            result.MinPrincipalCurvatureProperty;
        for (std::size_t i = 0; i < nV; ++i)
        {
            const VertexHandle vertex{static_cast<PropertyIndex>(i)};
            result.MeanCurvatureProperty[vertex] = 0.0;
            result.GaussianCurvatureProperty[vertex] = 0.0;
            result.MeanCurvatureNormalProperty[vertex] = glm::vec3(0.0f);
            if (mesh.IsDeleted(vertex) || !tensor.Vertices[i].Valid)
                continue;

            const double maxPrincipal =
                maxPrincipalProperty[vertex];
            const double minPrincipal =
                minPrincipalProperty[vertex];
            const double mean = 0.5 * (maxPrincipal + minPrincipal);
            result.MeanCurvatureProperty[vertex] = mean;
            result.GaussianCurvatureProperty[vertex] = maxPrincipal * minPrincipal;

            const glm::dvec3 normal = GeometricVertexNormal(mesh, vertex);
            const double normalLength = glm::length(normal);
            if (IsFinite(normal) && std::isfinite(normalLength)
                && normalLength > std::numeric_limits<double>::min())
                result.MeanCurvatureNormalProperty[vertex] = glm::vec3(-mean * normal / normalLength);
        }

        return result;
    }

    std::optional<CurvatureTensorResult> ComputeCurvatureTensor(HalfedgeMesh::Mesh& mesh)
    {
        if (mesh.IsEmpty() || mesh.FaceCount() == 0)
            return std::nullopt;

        CurvatureTensorResult result;
        result.PrincipalDir1Property = VertexProperty<glm::vec3>(mesh.VertexProperties().GetOrAdd<glm::vec3>("v:principal_dir1", glm::vec3(0.0f)));
        result.PrincipalDir2Property = VertexProperty<glm::vec3>(mesh.VertexProperties().GetOrAdd<glm::vec3>("v:principal_dir2", glm::vec3(0.0f)));
        result.MaxPrincipalCurvatureProperty = VertexProperty<double>(mesh.VertexProperties().GetOrAdd<double>("v:max_principal_curvature", 0.0));
        result.MinPrincipalCurvatureProperty = VertexProperty<double>(mesh.VertexProperties().GetOrAdd<double>("v:min_principal_curvature", 0.0));

        const TensorComputation tensor = ComputeEdgeDihedralTensor(mesh);
        result.Diagnostics = PublishTensorFields(
            mesh,
            tensor,
            result.PrincipalDir1Property,
            result.PrincipalDir2Property,
            result.MaxPrincipalCurvatureProperty,
            result.MinPrincipalCurvatureProperty);

        return result;
    }
} // namespace Geometry::Curvature

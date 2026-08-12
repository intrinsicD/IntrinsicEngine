// Implements discrete scalar operators and the deterministic Framework24
// edge-dihedral tensor published through canonical vertex fields.
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
        constexpr std::size_t kFramework24SmoothingSteps = 3u;

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

        // Framework24 divides corner dot products by triangle area rather than
        // twice area. Preserve that legacy normalization because it is part of
        // the reference field, including the distinct obtuse coefficients.
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
                        areaSum += 0.25 * triangleArea;
                    }
                    else if (dotQ < 0.0 || dotR < 0.0)
                    {
                        areaSum += 0.125 * triangleArea;
                    }
                    else
                    {
                        const double cotQ = Framework24ClampCotan(
                            dotQ / triangleArea);
                        const double cotR = Framework24ClampCotan(
                            dotR / triangleArea);
                        areaSum += 0.125
                            * (glm::dot(pr, pr) * cotQ
                               + glm::dot(pq, pq) * cotR);
                    }
                }
                areas[i] = areaSum;
            }
            return areas;
        }

        [[nodiscard]] double Framework24TriangleCotan(
            const glm::dvec3& p0,
            const glm::dvec3& p1,
            const glm::dvec3& p2) noexcept
        {
            const glm::dvec3 d0 = p0 - p2;
            const glm::dvec3 d1 = p1 - p2;
            const double dot = glm::dot(d0, d1);
            const double triangleArea = Framework24TriangleArea(p0, p1, p2);
            double denominator = triangleArea;
            if (!(denominator > std::numeric_limits<double>::epsilon()))
                denominator = glm::length(glm::cross(d0, d1));
            if (!std::isfinite(dot) || !std::isfinite(denominator)
                || denominator <= std::numeric_limits<double>::min())
            {
                return 0.0;
            }
            return Framework24ClampCotan(dot / denominator);
        }

        [[nodiscard]] std::vector<double> ComputeFramework24EdgeCotans(
            const HalfedgeMesh::Mesh& mesh)
        {
            std::vector<double> cotans(mesh.EdgesSize(), 0.0);
            for (std::size_t i = 0u; i < mesh.EdgesSize(); ++i)
            {
                const EdgeHandle edge{static_cast<PropertyIndex>(i)};
                if (mesh.IsDeleted(edge))
                    continue;
                const HalfedgeHandle h0 = mesh.Halfedge(edge, 0u);
                const HalfedgeHandle h1 = mesh.OppositeHalfedge(h0);
                const glm::dvec3 p0(mesh.Position(mesh.ToVertex(h0)));
                const glm::dvec3 p1(mesh.Position(mesh.ToVertex(h1)));
                double weight = 0.0;
                if (!mesh.IsBoundary(h0))
                {
                    const glm::dvec3 p2(mesh.Position(
                        mesh.ToVertex(mesh.NextHalfedge(h0))));
                    weight += Framework24TriangleCotan(p0, p1, p2);
                }
                if (!mesh.IsBoundary(h1))
                {
                    const glm::dvec3 p2(mesh.Position(
                        mesh.ToVertex(mesh.NextHalfedge(h1))));
                    weight += Framework24TriangleCotan(p0, p1, p2);
                }
                cotans[i] = 0.5 * weight;
            }
            return cotans;
        }

        // Direct deterministic port of Framework24 CurvatureTaubin. The
        // reference's default parallel smoother has a read/write race, so the
        // same in-place updates run in stable vertex-index order here.
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
                const double signedDihedral = std::atan2(
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

                std::vector<VertexHandle> neighborhood{};
                neighborhood.reserve(16u);
                neighborhood.push_back(vertex);
                for (const HalfedgeHandle halfedge :
                     mesh.HalfedgesAroundVertex(vertex))
                {
                    const VertexHandle neighbor = mesh.ToVertex(halfedge);
                    if (neighbor.IsValid() && !mesh.IsDeleted(neighbor))
                        neighborhood.push_back(neighbor);
                }

                double supportArea = 0.0;
                Eigen::Matrix3d tensor = Eigen::Matrix3d::Zero();
                bool reliable = true;
                for (const VertexHandle supportVertex : neighborhood)
                {
                    if (reliableSupport[supportVertex.Index] == 0u)
                    {
                        reliable = false;
                        break;
                    }
                    supportArea += mixedAreas[supportVertex.Index];
                    for (const HalfedgeHandle halfedge :
                         mesh.HalfedgesAroundVertex(supportVertex))
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
                }
                if (!reliable || !std::isfinite(supportArea)
                    || supportArea <= std::numeric_limits<double>::min())
                    continue;
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

            const std::vector<double> edgeCotans =
                ComputeFramework24EdgeCotans(mesh);
            for (std::size_t step = 0u;
                 step < kFramework24SmoothingSteps; ++step)
            {
                for (std::size_t i = 0u; i < vertexCount; ++i)
                {
                    const VertexHandle vertex{static_cast<PropertyIndex>(i)};
                    if (mesh.IsDeleted(vertex) || !out[i].Valid)
                        continue;
                    double minimum = 0.0;
                    double maximum = 0.0;
                    double weightSum = 0.0;
                    for (const HalfedgeHandle halfedge :
                         mesh.HalfedgesAroundVertex(vertex))
                    {
                        const VertexHandle neighbor = mesh.ToVertex(halfedge);
                        if (!neighbor.IsValid() || mesh.IsDeleted(neighbor)
                            || !out[neighbor.Index].Valid)
                        {
                            continue;
                        }
                        const double weight = std::max(
                            0.0, edgeCotans[mesh.Edge(halfedge).Index]);
                        weightSum += weight;
                        minimum += weight
                            * out[neighbor.Index].MinPrincipal;
                        maximum += weight
                            * out[neighbor.Index].MaxPrincipal;
                    }
                    if (weightSum > 0.0)
                    {
                        minimum /= weightSum;
                        maximum /= weightSum;
                        if (std::isfinite(minimum)
                            && std::isfinite(maximum))
                        {
                            out[i].MinPrincipal = minimum;
                            out[i].MaxPrincipal = maximum;
                        }
                        else
                        {
                            out[i] = TensorVertex{};
                        }
                    }
                }
            }
            if (computation.DegenerateFaceCount > 0u
                || computation.UnsupportedFaceCount > 0u)
            {
                computation.MinimumTriangleQuality = 0.0;
            }
            return computation;
        }

        // Publishes the three-pass deterministic Framework24 principal field.
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

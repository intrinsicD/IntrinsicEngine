// Implements discrete scalar operators and the signed edge-dihedral curvature
// tensor published through canonical vertex fields. The tensor uses one-ring
// hinge support and publishes unsmoothed eigenvalues: the previous PMP-default
// two-ring support bled sharp-crease bending into flanking vertices, and the
// three damped eigenvalue-smoothing passes then cancelled genuine curvature
// into near-zero bands (BUG-156, tests/data/sculpt.obj).
module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
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
            // Unlike Vertices::Valid, this mask remains set for ordinary zero
            // sentinels (for example, boundary vertices with no interior
            // interpolation support). It is cleared only where unreliable face
            // geometry invalidates the local estimate, so boundary
            // interpolation cannot inherit values from unreliable neighbours.
            std::vector<std::uint8_t> ReliableField{};
            std::size_t DegenerateFaceCount{0u};
            std::size_t IllConditionedFaceCount{0u};
            std::size_t UnsupportedFaceCount{0u};
            double MinimumTriangleQuality{0.0};
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

            [[nodiscard]] double MaxAbsCoefficient() const noexcept
            {
                return std::max({
                    std::abs(M00), std::abs(M01), std::abs(M02),
                    std::abs(M11), std::abs(M12), std::abs(M22)});
            }
        };

        struct SymmetricEigenSystem
        {
            std::array<double, 3> Values{};
            std::array<glm::dvec3, 3> Vectors{};
            bool Valid{false};
        };

        [[nodiscard]] bool IsFinite(const glm::dvec3& value) noexcept
        {
            return std::isfinite(value.x)
                && std::isfinite(value.y)
                && std::isfinite(value.z);
        }

        // Allocation-free Jacobi decomposition of the local 3x3 tensor. The
        // stopping criterion is relative to the tensor magnitude so uniformly
        // scaling a mesh cannot change which rotations are performed. Values
        // are returned in decreasing algebraic order.
        [[nodiscard]] SymmetricEigenSystem DecomposeSymmetric(
            const SymmetricTensor& tensor) noexcept
        {
            const double matrixScale = tensor.MaxAbsCoefficient();
            if (!std::isfinite(matrixScale))
                return {};
            if (matrixScale == 0.0)
            {
                return SymmetricEigenSystem{
                    .Values = {0.0, 0.0, 0.0},
                    .Vectors = {
                        glm::dvec3{1.0, 0.0, 0.0},
                        glm::dvec3{0.0, 1.0, 0.0},
                        glm::dvec3{0.0, 0.0, 1.0}},
                    .Valid = true,
                };
            }
            const double convergenceTolerance =
                64.0 * std::numeric_limits<double>::epsilon() * matrixScale;
            double a[3][3]{
                {tensor.M00, tensor.M01, tensor.M02},
                {tensor.M01, tensor.M11, tensor.M12},
                {tensor.M02, tensor.M12, tensor.M22},
            };
            double vectors[3][3]{
                {1.0, 0.0, 0.0},
                {0.0, 1.0, 0.0},
                {0.0, 0.0, 1.0},
            };

            bool converged = false;
            for (int iteration = 0; iteration < 100; ++iteration)
            {
                int row = 0;
                int column = 1;
                if (std::abs(a[0][1]) < std::abs(a[0][2]))
                {
                    if (std::abs(a[0][2]) < std::abs(a[1][2]))
                    {
                        row = 1;
                        column = 2;
                    }
                    else
                    {
                        row = 0;
                        column = 2;
                    }
                }
                else if (std::abs(a[0][1]) < std::abs(a[1][2]))
                {
                    row = 1;
                    column = 2;
                }

                if (std::abs(a[row][column]) <= convergenceTolerance)
                {
                    converged = true;
                    break;
                }

                const double theta = 0.5
                    * (a[column][column] - a[row][row])
                    / a[row][column];
                double tangent = 1.0
                    / (std::abs(theta) + std::sqrt(1.0 + theta * theta));
                if (theta < 0.0)
                    tangent = -tangent;
                const double cosine = 1.0 / std::sqrt(1.0 + tangent * tangent);
                const double sine = tangent * cosine;

                double rotation[3][3]{
                    {1.0, 0.0, 0.0},
                    {0.0, 1.0, 0.0},
                    {0.0, 0.0, 1.0},
                };
                rotation[row][row] = cosine;
                rotation[column][column] = cosine;
                rotation[row][column] = sine;
                rotation[column][row] = -sine;

                double intermediate[3][3]{};
                double rotated[3][3]{};
                double rotatedVectors[3][3]{};
                for (int i = 0; i < 3; ++i)
                {
                    for (int j = 0; j < 3; ++j)
                    {
                        for (int k = 0; k < 3; ++k)
                        {
                            intermediate[i][j] += a[i][k] * rotation[k][j];
                            rotatedVectors[i][j] +=
                                vectors[i][k] * rotation[k][j];
                        }
                    }
                }
                for (int i = 0; i < 3; ++i)
                {
                    for (int j = 0; j < 3; ++j)
                    {
                        for (int k = 0; k < 3; ++k)
                            rotated[i][j] += rotation[k][i] * intermediate[k][j];
                        a[i][j] = rotated[i][j];
                        vectors[i][j] = rotatedVectors[i][j];
                    }
                }
            }

            SymmetricEigenSystem result{};
            if (!converged)
                return result;

            const std::array<double, 3> diagonal{a[0][0], a[1][1], a[2][2]};
            std::array<int, 3> sorted{};
            if (diagonal[0] > diagonal[1])
            {
                if (diagonal[1] > diagonal[2])
                    sorted = {0, 1, 2};
                else if (diagonal[0] > diagonal[2])
                    sorted = {0, 2, 1};
                else
                    sorted = {2, 0, 1};
            }
            else if (diagonal[0] > diagonal[2])
            {
                sorted = {1, 0, 2};
            }
            else if (diagonal[1] > diagonal[2])
            {
                sorted = {1, 2, 0};
            }
            else
            {
                sorted = {2, 1, 0};
            }

            result.Values = {
                diagonal[sorted[0]],
                diagonal[sorted[1]],
                diagonal[sorted[2]],
            };
            result.Vectors[0] = {
                vectors[0][sorted[0]],
                vectors[1][sorted[0]],
                vectors[2][sorted[0]],
            };
            result.Vectors[1] = {
                vectors[0][sorted[1]],
                vectors[1][sorted[1]],
                vectors[2][sorted[1]],
            };
            result.Vectors[2] = glm::cross(
                result.Vectors[0], result.Vectors[1]);
            const double thirdLength = glm::length(result.Vectors[2]);
            if (!std::isfinite(thirdLength)
                || thirdLength
                    <= 64.0 * std::numeric_limits<double>::epsilon())
                return SymmetricEigenSystem{};
            result.Vectors[2] /= thirdLength;
            for (std::size_t i = 0u; i < 3u; ++i)
            {
                if (!std::isfinite(result.Values[i])
                    || !IsFinite(result.Vectors[i]))
                {
                    return SymmetricEigenSystem{};
                }
            }
            result.Valid = true;
            return result;
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

        // Accumulates signed interior hinge samples over each non-boundary
        // center's own incident edges (the PMP formulation restricted to
        // one-ring support), then interpolates boundary scalar values from
        // interior neighbours. Two-ring support is deliberately not used: it
        // integrates sharp-crease bending into flanking vertices whose local
        // surface is smooth, overwhelming their own signal.
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
            computation.ReliableField.assign(vertexCount, 1u);
            std::vector<TensorVertex>& out = computation.Vertices;
            const std::vector<double> mixedAreas =
                ComputeMixedVoronoiAreas(mesh);

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

                // Algebraically equivalent to PMP's to(h0)-to(h1) direction:
                // both the direction and atan2 sign are reversed here to keep
                // the module's outward-convex-positive convention explicit.
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
                if (mesh.IsDeleted(vertex) || mesh.IsIsolated(vertex)
                    || mesh.IsBoundary(vertex))
                {
                    if (mesh.IsDeleted(vertex) || mesh.IsIsolated(vertex))
                        computation.ReliableField[i] = 0u;
                    continue;
                }

                const double supportArea = mixedAreas[i];
                if (reliableSupport[i] == 0u || !std::isfinite(supportArea)
                    || supportArea <= std::numeric_limits<double>::min())
                {
                    computation.ReliableField[i] = 0u;
                    continue;
                }
                SymmetricTensor tensor{};
                for (const HalfedgeHandle halfedge :
                     mesh.HalfedgesAroundVertex(vertex))
                {
                    const EdgeTensorSample& sample =
                        edgeSamples[mesh.Edge(halfedge).Index];
                    if (sample.Valid)
                    {
                        tensor.AddOuterProduct(
                            sample.WeightedDirection,
                            sample.SignedDihedral);
                    }
                }
                tensor.Divide(supportArea);

                const SymmetricEigenSystem eigen =
                    DecomposeSymmetric(tensor);
                if (!eigen.Valid)
                    continue;

                const double absolute0 = std::abs(eigen.Values[0]);
                const double absolute1 = std::abs(eigen.Values[1]);
                const double absolute2 = std::abs(eigen.Values[2]);
                int normalIndex = 2;
                if (absolute0 < absolute1)
                    normalIndex = absolute0 < absolute2 ? 0 : 2;
                else
                    normalIndex = absolute1 < absolute2 ? 1 : 2;

                std::array<int, 2> scalarTangentIndices{};
                std::size_t scalarTangentCount = 0u;
                for (int index = 0; index < 3; ++index)
                {
                    if (index != normalIndex)
                        scalarTangentIndices[scalarTangentCount++] = index;
                }
                const int scalarMaxIndex = scalarTangentIndices[0];
                const int scalarMinIndex = scalarTangentIndices[1];
                out[i].MaxPrincipal = eigen.Values[scalarMaxIndex];
                out[i].MinPrincipal = eigen.Values[scalarMinIndex];
                out[i].Valid =
                    std::isfinite(out[i].MaxPrincipal)
                    && std::isfinite(out[i].MinPrincipal);
                if (!out[i].Valid || tensor.MaxAbsCoefficient() == 0.0)
                    continue;

                // PMP publishes scalars only. Direction publication resolves
                // cylinder zero-eigenvalue ambiguity against the geometric
                // normal, then retains the hinge tensor's complementary pairing.
                const glm::dvec3 geometricNormal =
                    GeometricVertexNormal(mesh, vertex);
                if (glm::dot(geometricNormal, geometricNormal)
                    <= kDirectionTiny)
                    continue;
                int geometricNormalIndex = 0;
                double bestAlignment = -1.0;
                for (int index = 0; index < 3; ++index)
                {
                    const double alignment = std::abs(
                        glm::dot(eigen.Vectors[index], geometricNormal));
                    if (alignment > bestAlignment)
                    {
                        bestAlignment = alignment;
                        geometricNormalIndex = index;
                    }
                }
                std::array<int, 2> directionTangentIndices{};
                std::size_t directionTangentCount = 0u;
                for (int index = 0; index < 3; ++index)
                {
                    if (index != geometricNormalIndex)
                    {
                        directionTangentIndices[directionTangentCount++] =
                            index;
                    }
                }
                const int tensorMaxIndex = directionTangentIndices[0];
                const int tensorMinIndex = directionTangentIndices[1];
                glm::dvec3 maxDirection =
                    eigen.Vectors[tensorMinIndex]
                    - glm::dot(eigen.Vectors[tensorMinIndex], geometricNormal)
                        * geometricNormal;
                const double maxDirectionLength = glm::length(maxDirection);
                if (!IsFinite(maxDirection)
                    || !std::isfinite(maxDirectionLength)
                    || maxDirectionLength <= kDirectionTiny)
                {
                    continue;
                }
                maxDirection /= maxDirectionLength;
                glm::dvec3 minDirection =
                    eigen.Vectors[tensorMaxIndex]
                    - glm::dot(eigen.Vectors[tensorMaxIndex], geometricNormal)
                        * geometricNormal;
                minDirection -= glm::dot(minDirection, maxDirection)
                    * maxDirection;
                const double minDirectionLength = glm::length(minDirection);
                if (!IsFinite(minDirection)
                    || !std::isfinite(minDirectionLength)
                    || minDirectionLength <= kDirectionTiny)
                {
                    continue;
                }
                minDirection /= minDirectionLength;
                out[i].Dir1 = maxDirection;
                out[i].Dir2 = minDirection;
                out[i].DirectionValid = true;
            }

            // PMP boundary values are a uniform average over non-boundary
            // one-ring neighbours. Direction line fields use the same support,
            // with signs aligned before averaging and a tangent re-orthogonalization.
            for (std::size_t i = 0u; i < vertexCount; ++i)
            {
                const VertexHandle vertex{static_cast<PropertyIndex>(i)};
                if (mesh.IsDeleted(vertex) || !mesh.IsBoundary(vertex))
                    continue;
                if (reliableSupport[i] == 0u)
                {
                    computation.ReliableField[i] = 0u;
                    continue;
                }
                double minSum = 0.0;
                double maxSum = 0.0;
                std::size_t scalarCount = 0u;
                glm::dvec3 direction1Sum{0.0};
                glm::dvec3 direction2Sum{0.0};
                glm::dvec3 direction1Reference{0.0};
                glm::dvec3 direction2Reference{0.0};
                std::size_t directionCount = 0u;
                for (const HalfedgeHandle halfedge :
                     mesh.HalfedgesAroundVertex(vertex))
                {
                    const VertexHandle neighbor = mesh.ToVertex(halfedge);
                    if (!neighbor.IsValid() || mesh.IsDeleted(neighbor)
                        || mesh.IsBoundary(neighbor))
                    {
                        continue;
                    }
                    if (computation.ReliableField[neighbor.Index] == 0u)
                    {
                        computation.ReliableField[i] = 0u;
                        continue;
                    }
                    if (!out[neighbor.Index].Valid)
                        continue;
                    minSum += out[neighbor.Index].MinPrincipal;
                    maxSum += out[neighbor.Index].MaxPrincipal;
                    ++scalarCount;
                    if (!out[neighbor.Index].DirectionValid)
                        continue;
                    glm::dvec3 direction1 = out[neighbor.Index].Dir1;
                    glm::dvec3 direction2 = out[neighbor.Index].Dir2;
                    if (directionCount == 0u)
                    {
                        direction1Reference = direction1;
                        direction2Reference = direction2;
                    }
                    if (glm::dot(direction1, direction1Reference) < 0.0)
                        direction1 = -direction1;
                    if (glm::dot(direction2, direction2Reference) < 0.0)
                        direction2 = -direction2;
                    direction1Sum += direction1;
                    direction2Sum += direction2;
                    ++directionCount;
                }
                if (computation.ReliableField[i] == 0u)
                {
                    out[i] = TensorVertex{};
                    continue;
                }
                if (scalarCount == 0u)
                    continue;
                out[i].MinPrincipal = minSum
                    / static_cast<double>(scalarCount);
                out[i].MaxPrincipal = maxSum
                    / static_cast<double>(scalarCount);
                out[i].Valid = true;
                if (directionCount == 0u)
                    continue;

                const glm::dvec3 geometricNormal =
                    GeometricVertexNormal(mesh, vertex);
                if (glm::dot(geometricNormal, geometricNormal)
                    <= kDirectionTiny)
                    continue;
                glm::dvec3 direction1 = direction1Sum
                    - glm::dot(direction1Sum, geometricNormal)
                        * geometricNormal;
                const double direction1Length = glm::length(direction1);
                if (!IsFinite(direction1)
                    || !std::isfinite(direction1Length)
                    || direction1Length <= kDirectionTiny)
                {
                    continue;
                }
                direction1 /= direction1Length;
                glm::dvec3 direction2 = direction2Sum
                    - glm::dot(direction2Sum, geometricNormal)
                        * geometricNormal;
                direction2 -= glm::dot(direction2, direction1) * direction1;
                double direction2Length = glm::length(direction2);
                if (!IsFinite(direction2)
                    || !std::isfinite(direction2Length)
                    || direction2Length <= kDirectionTiny)
                {
                    direction2 = glm::cross(geometricNormal, direction1);
                    direction2Length = glm::length(direction2);
                }
                if (!IsFinite(direction2)
                    || !std::isfinite(direction2Length)
                    || direction2Length <= kDirectionTiny)
                {
                    continue;
                }
                direction2 /= direction2Length;
                if (glm::dot(direction2, direction2Reference) < 0.0)
                    direction2 = -direction2;
                out[i].Dir1 = direction1;
                out[i].Dir2 = direction2;
                out[i].DirectionValid = true;
            }
            if (computation.DegenerateFaceCount > 0u
                || computation.UnsupportedFaceCount > 0u)
            {
                computation.MinimumTriangleQuality = 0.0;
            }
            return computation;
        }

        // Publishes the raw per-vertex eigenvalues. No post-smoothing is
        // applied: damped eigenvalue smoothing averaged across crease
        // transitions and cancelled genuine curvature into near-zero bands.
        // Callers wanting stabilized fields can smooth the published
        // properties explicitly through Geometry.Smoothing.
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

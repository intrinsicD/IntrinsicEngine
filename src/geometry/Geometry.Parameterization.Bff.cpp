module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <span>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

module Geometry.Parameterization.Bff;

import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.Utils;
import Geometry.Parameterization.Diagnostics;
import Geometry.Properties;
import Geometry.Sparse;

namespace Geometry::Parameterization
{
    namespace
    {
        constexpr double kPi = 3.141592653589793238462643383279502884;
        constexpr std::size_t kInvalidIndex =
            std::numeric_limits<std::size_t>::max();

        struct PreparedDisk
        {
            std::vector<VertexHandle> Boundary{};
            std::vector<VertexHandle> Interior{};
            std::vector<VertexHandle> Active{};
            std::vector<std::size_t> BoundaryOf{};
            std::vector<std::size_t> InteriorOf{};
            std::vector<std::size_t> ActiveOf{};
            std::vector<double> EdgeWeights{};
            std::vector<double> GaussianCurvature{};
            std::vector<double> BoundaryCurvature{};
            std::vector<double> BoundaryLengths{};
            double CharacteristicLength{0.0};
        };

        [[nodiscard]] bool IsFinite(const glm::vec3 value) noexcept
        {
            return std::isfinite(value.x)
                && std::isfinite(value.y)
                && std::isfinite(value.z);
        }

        [[nodiscard]] bool IsFinite(const glm::vec2 value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y);
        }

        [[nodiscard]] double CornerAngle(
            const glm::dvec3 vertex,
            const glm::dvec3 previous,
            const glm::dvec3 next,
            const double lengthEpsilon,
            const double areaEpsilon) noexcept
        {
            const glm::dvec3 a = previous - vertex;
            const glm::dvec3 b = next - vertex;
            const double aLength = glm::length(a);
            const double bLength = glm::length(b);
            const double crossLength = glm::length(glm::cross(a, b));
            if (!std::isfinite(aLength) || !std::isfinite(bLength)
                || !std::isfinite(crossLength)
                || aLength <= lengthEpsilon || bLength <= lengthEpsilon
                || crossLength <= areaEpsilon)
            {
                return std::numeric_limits<double>::quiet_NaN();
            }
            return std::atan2(crossLength, glm::dot(a, b));
        }

        [[nodiscard]] bool HasDiskTopology(
            const HalfedgeMesh::Mesh& mesh,
            const std::vector<VertexHandle>& boundary)
        {
            if (boundary.empty())
                return false;

            std::vector<std::uint8_t> reached(mesh.VerticesSize(), 0u);
            std::vector<VertexHandle> pending{boundary.front()};
            reached[boundary.front().Index] = 1u;
            while (!pending.empty())
            {
                const VertexHandle vertex = pending.back();
                pending.pop_back();
                for (const HalfedgeHandle halfedge :
                     mesh.HalfedgesAroundVertex(vertex))
                {
                    const VertexHandle neighbor = mesh.ToVertex(halfedge);
                    if (mesh.IsDeleted(neighbor)
                        || reached[neighbor.Index] != 0u)
                    {
                        continue;
                    }
                    reached[neighbor.Index] = 1u;
                    pending.push_back(neighbor);
                }
            }

            for (std::size_t vi = 0u; vi < mesh.VerticesSize(); ++vi)
            {
                const VertexHandle vertex{static_cast<PropertyIndex>(vi)};
                if (mesh.IsDeleted(vertex))
                    continue;
                if (mesh.IsIsolated(vertex) || !mesh.IsManifold(vertex)
                    || reached[vi] == 0u)
                {
                    return false;
                }
            }

            const std::int64_t eulerCharacteristic =
                static_cast<std::int64_t>(mesh.VertexCount())
                - static_cast<std::int64_t>(mesh.EdgeCount())
                + static_cast<std::int64_t>(mesh.FaceCount());
            return eulerCharacteristic == 1;
        }

        [[nodiscard]] BffStatus PrepareDisk(
            const HalfedgeMesh::Mesh& mesh,
            const double epsilon,
            PreparedDisk& disk)
        {
            if (mesh.VertexCount() == 0u || mesh.FaceCount() == 0u)
                return BffStatus::EmptyMesh;
            if (mesh.VertexCount() < 3u)
                return BffStatus::InsufficientVertices;

            for (std::size_t vi = 0u; vi < mesh.VerticesSize(); ++vi)
            {
                const VertexHandle vertex{static_cast<PropertyIndex>(vi)};
                if (!mesh.IsDeleted(vertex) && !IsFinite(mesh.Position(vertex)))
                    return BffStatus::NonFiniteGeometry;
            }
            for (std::size_t fi = 0u; fi < mesh.FacesSize(); ++fi)
            {
                const FaceHandle face{static_cast<PropertyIndex>(fi)};
                if (!mesh.IsDeleted(face) && mesh.Valence(face) != 3u)
                    return BffStatus::NotTriangleMesh;
            }

            const std::vector<MeshUtils::BoundaryLoopData> loops =
                MeshUtils::CollectBoundaryLoops(mesh);
            if (loops.size() != 1u)
                return BffStatus::NotDiskTopology;
            disk.Boundary = loops.front().Vertices;
            if (disk.Boundary.size() < 3u)
                return BffStatus::DegenerateBoundary;

            std::size_t boundaryVertexCount = 0u;
            for (std::size_t vi = 0u; vi < mesh.VerticesSize(); ++vi)
            {
                const VertexHandle vertex{static_cast<PropertyIndex>(vi)};
                if (!mesh.IsDeleted(vertex) && !mesh.IsIsolated(vertex)
                    && mesh.IsBoundary(vertex))
                {
                    ++boundaryVertexCount;
                }
            }
            if (boundaryVertexCount != disk.Boundary.size()
                || !HasDiskTopology(mesh, disk.Boundary))
            {
                return BffStatus::NotDiskTopology;
            }

            disk.BoundaryOf.assign(mesh.VerticesSize(), kInvalidIndex);
            for (std::size_t i = 0u; i < disk.Boundary.size(); ++i)
                disk.BoundaryOf[disk.Boundary[i].Index] = i;

            disk.InteriorOf.assign(mesh.VerticesSize(), kInvalidIndex);
            disk.ActiveOf.assign(mesh.VerticesSize(), kInvalidIndex);
            for (std::size_t vi = 0u; vi < mesh.VerticesSize(); ++vi)
            {
                const VertexHandle vertex{static_cast<PropertyIndex>(vi)};
                if (mesh.IsDeleted(vertex) || mesh.IsIsolated(vertex))
                    continue;
                disk.ActiveOf[vi] = disk.Active.size();
                disk.Active.push_back(vertex);
                if (disk.BoundaryOf[vi] == kInvalidIndex)
                {
                    disk.InteriorOf[vi] = disk.Interior.size();
                    disk.Interior.push_back(vertex);
                }
            }

            for (std::size_t ei = 0u; ei < mesh.EdgesSize(); ++ei)
            {
                const EdgeHandle edge{static_cast<PropertyIndex>(ei)};
                if (mesh.IsDeleted(edge))
                    continue;
                const HalfedgeHandle halfedge{
                    static_cast<PropertyIndex>(2u * ei)};
                const glm::dvec3 a(mesh.Position(mesh.FromVertex(halfedge)));
                const glm::dvec3 b(mesh.Position(mesh.ToVertex(halfedge)));
                const double length = glm::length(b - a);
                if (!std::isfinite(length))
                    return BffStatus::NonFiniteGeometry;
                disk.CharacteristicLength =
                    std::max(disk.CharacteristicLength, length);
            }
            if (!(disk.CharacteristicLength > 0.0))
                return BffStatus::DegenerateGeometry;
            const double lengthEpsilon = epsilon * disk.CharacteristicLength;
            const double areaEpsilon = epsilon
                * disk.CharacteristicLength * disk.CharacteristicLength;

            std::vector<double> angleSum(mesh.VerticesSize(), 0.0);
            disk.EdgeWeights.assign(mesh.EdgesSize(), 0.0);
            for (std::size_t fi = 0u; fi < mesh.FacesSize(); ++fi)
            {
                const FaceHandle face{static_cast<PropertyIndex>(fi)};
                if (mesh.IsDeleted(face))
                    continue;

                MeshUtils::TriangleFaceView triangle{};
                if (!MeshUtils::TryGetTriangleFaceView(mesh, face, triangle))
                    return BffStatus::NotTriangleMesh;
                const glm::dvec3 p0(mesh.Position(triangle.V0));
                const glm::dvec3 p1(mesh.Position(triangle.V1));
                const glm::dvec3 p2(mesh.Position(triangle.V2));
                const double a0 = CornerAngle(
                    p0, p1, p2, lengthEpsilon, areaEpsilon);
                const double a1 = CornerAngle(
                    p1, p2, p0, lengthEpsilon, areaEpsilon);
                const double a2 = CornerAngle(
                    p2, p0, p1, lengthEpsilon, areaEpsilon);
                if (!std::isfinite(a0) || !std::isfinite(a1)
                    || !std::isfinite(a2))
                {
                    return BffStatus::DegenerateGeometry;
                }
                angleSum[triangle.V0.Index] += a0;
                angleSum[triangle.V1.Index] += a1;
                angleSum[triangle.V2.Index] += a2;

                const auto cotangent = [](const double angle)
                {
                    return std::cos(angle) / std::sin(angle);
                };
                disk.EdgeWeights[mesh.Edge(triangle.H0).Index] +=
                    0.5 * cotangent(a1);
                disk.EdgeWeights[mesh.Edge(triangle.H1).Index] +=
                    0.5 * cotangent(a2);
                disk.EdgeWeights[mesh.Edge(triangle.H2).Index] +=
                    0.5 * cotangent(a0);
            }

            disk.GaussianCurvature.assign(mesh.VerticesSize(), 0.0);
            disk.BoundaryCurvature.assign(mesh.VerticesSize(), 0.0);
            for (const VertexHandle vertex : disk.Active)
            {
                if (!std::isfinite(angleSum[vertex.Index]))
                    return BffStatus::NonFiniteGeometry;
                if (disk.BoundaryOf[vertex.Index] != kInvalidIndex)
                    disk.BoundaryCurvature[vertex.Index] =
                        kPi - angleSum[vertex.Index];
                else
                    disk.GaussianCurvature[vertex.Index] =
                        2.0 * kPi - angleSum[vertex.Index];
            }

            for (std::size_t ei = 0u; ei < mesh.EdgesSize(); ++ei)
            {
                const EdgeHandle edge{static_cast<PropertyIndex>(ei)};
                if (mesh.IsDeleted(edge))
                    continue;
                const double weight = disk.EdgeWeights[ei];
                if (!std::isfinite(weight))
                    return BffStatus::NonFiniteGeometry;
                disk.EdgeWeights[ei] = weight;
            }

            disk.BoundaryLengths.resize(disk.Boundary.size(), 0.0);
            for (std::size_t i = 0u; i < disk.Boundary.size(); ++i)
            {
                const glm::dvec3 a(mesh.Position(disk.Boundary[i]));
                const glm::dvec3 b(mesh.Position(
                    disk.Boundary[(i + 1u) % disk.Boundary.size()]));
                const double length = glm::length(b - a);
                if (!std::isfinite(length))
                    return BffStatus::NonFiniteGeometry;
                if (length <= lengthEpsilon)
                    return BffStatus::DegenerateBoundary;
                disk.BoundaryLengths[i] = length;
            }
            return BffStatus::Success;
        }

        [[nodiscard]] Sparse::SparseBuildResult BuildDirichletMatrix(
            const HalfedgeMesh::Mesh& mesh,
            const PreparedDisk& disk)
        {
            Sparse::SparseBuilder builder(
                disk.Interior.size(), disk.Interior.size());
            builder.Reserve(disk.Interior.size() * 7u);
            for (std::size_t row = 0u; row < disk.Interior.size(); ++row)
            {
                const VertexHandle vertex = disk.Interior[row];
                double diagonal = 0.0;
                for (const HalfedgeHandle halfedge :
                     mesh.HalfedgesAroundVertex(vertex))
                {
                    const double weight =
                        disk.EdgeWeights[mesh.Edge(halfedge).Index];
                    diagonal += weight;
                    const VertexHandle neighbor = mesh.ToVertex(halfedge);
                    const std::size_t column =
                        disk.InteriorOf[neighbor.Index];
                    if (column != kInvalidIndex)
                        builder.Add(row, column, -weight);
                }
                builder.Add(row, row, diagonal);
            }
            return builder.Build();
        }

        struct GroundedSystem
        {
            Sparse::SparseBuildResult Built{};
            std::vector<std::size_t> GroundedOf{};
            VertexHandle Root{};
        };

        [[nodiscard]] GroundedSystem BuildGroundedMatrix(
            const HalfedgeMesh::Mesh& mesh,
            const PreparedDisk& disk)
        {
            GroundedSystem system{};
            system.Root = disk.Boundary.front();
            system.GroundedOf.assign(mesh.VerticesSize(), kInvalidIndex);
            std::size_t count = 0u;
            for (const VertexHandle vertex : disk.Active)
            {
                if (vertex != system.Root)
                    system.GroundedOf[vertex.Index] = count++;
            }

            Sparse::SparseBuilder builder(count, count);
            builder.Reserve(count * 7u);
            for (const VertexHandle vertex : disk.Active)
            {
                if (vertex == system.Root)
                    continue;
                const std::size_t row = system.GroundedOf[vertex.Index];
                double diagonal = 0.0;
                for (const HalfedgeHandle halfedge :
                     mesh.HalfedgesAroundVertex(vertex))
                {
                    const double weight =
                        disk.EdgeWeights[mesh.Edge(halfedge).Index];
                    diagonal += weight;
                    const VertexHandle neighbor = mesh.ToVertex(halfedge);
                    if (neighbor != system.Root)
                    {
                        const std::size_t column =
                            system.GroundedOf[neighbor.Index];
                        if (column != kInvalidIndex)
                            builder.Add(row, column, -weight);
                    }
                }
                builder.Add(row, row, diagonal);
            }
            system.Built = builder.Build();
            return system;
        }

        [[nodiscard]] bool SolveDirichlet(
            const HalfedgeMesh::Mesh& mesh,
            const PreparedDisk& disk,
            const Sparse::SparseLDLT* solver,
            const std::vector<double>& boundaryValues,
            const std::vector<double>& source,
            std::vector<double>& values)
        {
            values.assign(mesh.VerticesSize(), 0.0);
            for (std::size_t i = 0u; i < disk.Boundary.size(); ++i)
                values[disk.Boundary[i].Index] = boundaryValues[i];
            if (disk.Interior.empty())
                return true;
            if (solver == nullptr)
                return false;

            std::vector<double> rhs(disk.Interior.size(), 0.0);
            for (std::size_t row = 0u; row < disk.Interior.size(); ++row)
            {
                const VertexHandle vertex = disk.Interior[row];
                rhs[row] = source[vertex.Index];
                for (const HalfedgeHandle halfedge :
                     mesh.HalfedgesAroundVertex(vertex))
                {
                    const VertexHandle neighbor = mesh.ToVertex(halfedge);
                    const std::size_t boundaryIndex =
                        disk.BoundaryOf[neighbor.Index];
                    if (boundaryIndex == kInvalidIndex)
                        continue;
                    const double weight =
                        disk.EdgeWeights[mesh.Edge(halfedge).Index];
                    rhs[row] += weight * boundaryValues[boundaryIndex];
                }
            }
            std::vector<double> solution(disk.Interior.size(), 0.0);
            if (!solver->solve(rhs, solution).Succeeded())
                return false;
            for (std::size_t i = 0u; i < disk.Interior.size(); ++i)
                values[disk.Interior[i].Index] = solution[i];
            return std::all_of(values.begin(), values.end(), [](double value)
            {
                return std::isfinite(value);
            });
        }

        [[nodiscard]] bool SolveGrounded(
            const HalfedgeMesh::Mesh& mesh,
            const PreparedDisk& disk,
            const GroundedSystem& system,
            const Sparse::SparseLDLT& solver,
            const std::vector<double>& source,
            std::vector<double>& values)
        {
            const std::size_t count = disk.Active.size() - 1u;
            std::vector<double> rhs(count, 0.0);
            for (const VertexHandle vertex : disk.Active)
            {
                if (vertex == system.Root)
                    continue;
                rhs[system.GroundedOf[vertex.Index]] = source[vertex.Index];
            }
            std::vector<double> solution(count, 0.0);
            if (!solver.solve(rhs, solution).Succeeded())
                return false;

            values.assign(mesh.VerticesSize(), 0.0);
            for (const VertexHandle vertex : disk.Active)
            {
                if (vertex != system.Root)
                    values[vertex.Index] =
                        solution[system.GroundedOf[vertex.Index]];
            }
            return std::all_of(values.begin(), values.end(), [](double value)
            {
                return std::isfinite(value);
            });
        }

        [[nodiscard]] double ApplyLaplacianAtVertex(
            const HalfedgeMesh::Mesh& mesh,
            const PreparedDisk& disk,
            const VertexHandle vertex,
            const std::vector<double>& values)
        {
            double sum = 0.0;
            for (const HalfedgeHandle halfedge :
                 mesh.HalfedgesAroundVertex(vertex))
            {
                const VertexHandle neighbor = mesh.ToVertex(halfedge);
                const double weight =
                    disk.EdgeWeights[mesh.Edge(halfedge).Index];
                sum += weight
                    * (values[vertex.Index] - values[neighbor.Index]);
            }
            return sum;
        }

        enum class CurveClosureStatus : std::uint8_t
        {
            Success,
            SingularProjection,
            NonPositiveAdjustedLength,
            ResidualTooLarge,
        };

        [[nodiscard]] CurveClosureStatus CloseBoundaryCurve(
            const PreparedDisk& disk,
            const std::vector<double>& scaledLengths,
            const std::vector<double>& targetAngles,
            const double epsilon,
            std::vector<double>& adjustedLengths,
            std::vector<glm::dvec2>& boundary,
            BffDiagnostics& diagnostics)
        {
            const std::size_t count = disk.Boundary.size();
            std::vector<glm::dvec2> tangents(count, glm::dvec2(0.0));
            double direction = 0.0;
            tangents[0] = glm::dvec2(1.0, 0.0);
            for (std::size_t i = 1u; i < count; ++i)
            {
                direction += targetAngles[i];
                tangents[i] = glm::dvec2(
                    std::cos(direction), std::sin(direction));
            }

            double m00 = 0.0;
            double m01 = 0.0;
            double m11 = 0.0;
            glm::dvec2 residual(0.0);
            std::vector<double> closureWeights(count, 0.0);
            for (std::size_t i = 0u; i < count; ++i)
            {
                // Eq. 19/20 uses N^-1_ii = l_ij for the boundary edge
                // whose adjusted length is degree of freedom i.
                closureWeights[i] = disk.BoundaryLengths[i];
                const glm::dvec2 tangent = tangents[i];
                m00 += closureWeights[i] * tangent.x * tangent.x;
                m01 += closureWeights[i] * tangent.x * tangent.y;
                m11 += closureWeights[i] * tangent.y * tangent.y;
                residual += scaledLengths[i] * tangent;
            }

            const double determinant = m00 * m11 - m01 * m01;
            const double matrixScale = std::max({std::abs(m00),
                                                  std::abs(m01), std::abs(m11)});
            if (!std::isfinite(determinant)
                || !(matrixScale > 0.0)
                || std::abs(determinant) <= epsilon * matrixScale * matrixScale)
            {
                return CurveClosureStatus::SingularProjection;
            }
            const glm::dvec2 multiplier(
                (m11 * residual.x - m01 * residual.y) / determinant,
                (-m01 * residual.x + m00 * residual.y) / determinant);

            adjustedLengths.resize(count, 0.0);
            const double lengthScale = *std::max_element(
                scaledLengths.begin(), scaledLengths.end());
            double sumRelativeSquared = 0.0;
            double maxRelative = 0.0;
            for (std::size_t i = 0u; i < count; ++i)
            {
                const double correction =
                    closureWeights[i] * glm::dot(tangents[i], multiplier);
                adjustedLengths[i] = scaledLengths[i] - correction;
                if (!std::isfinite(adjustedLengths[i])
                    || adjustedLengths[i] <= epsilon * lengthScale)
                {
                    return CurveClosureStatus::NonPositiveAdjustedLength;
                }
                const double relative =
                    std::abs(correction) / scaledLengths[i];
                sumRelativeSquared += relative * relative;
                maxRelative = std::max(maxRelative, relative);
            }
            diagnostics.ClosureAdjustmentRmsRelative =
                std::sqrt(sumRelativeSquared / static_cast<double>(count));
            diagnostics.ClosureAdjustmentMaxRelative = maxRelative;

            boundary.assign(count, glm::dvec2(0.0));
            for (std::size_t i = 1u; i < count; ++i)
            {
                boundary[i] = boundary[i - 1u]
                    + adjustedLengths[i - 1u] * tangents[i - 1u];
            }
            const glm::dvec2 closure = boundary.back()
                + adjustedLengths.back() * tangents.back();
            const double perimeter = std::accumulate(
                adjustedLengths.begin(), adjustedLengths.end(), 0.0);
            if (!std::isfinite(closure.x) || !std::isfinite(closure.y)
                || glm::length(closure) > 128.0 * epsilon * perimeter)
            {
                return CurveClosureStatus::ResidualTooLarge;
            }
            return CurveClosureStatus::Success;
        }

        [[nodiscard]] double SignedUvArea(
            const HalfedgeMesh::Mesh& mesh,
            const std::vector<glm::vec2>& uvs)
        {
            double area = 0.0;
            for (std::size_t fi = 0u; fi < mesh.FacesSize(); ++fi)
            {
                const FaceHandle face{static_cast<PropertyIndex>(fi)};
                if (mesh.IsDeleted(face))
                    continue;
                MeshUtils::TriangleFaceView triangle{};
                if (!MeshUtils::TryGetTriangleFaceView(mesh, face, triangle))
                    continue;
                const glm::dvec2 a(uvs[triangle.V0.Index]);
                const glm::dvec2 b(uvs[triangle.V1.Index]);
                const glm::dvec2 c(uvs[triangle.V2.Index]);
                area += 0.5 * ((b.x - a.x) * (c.y - a.y)
                             - (c.x - a.x) * (b.y - a.y));
            }
            return area;
        }

        void FixOrientation(
            const HalfedgeMesh::Mesh& mesh,
            std::vector<glm::vec2>& uvs)
        {
            if (SignedUvArea(mesh, uvs) >= 0.0)
                return;
            for (glm::vec2& uv : uvs)
                uv.y = -uv.y;
        }

        [[nodiscard]] double ExteriorTurn(
            const glm::dvec2 previous,
            const glm::dvec2 vertex,
            const glm::dvec2 next) noexcept
        {
            const glm::dvec2 incoming = vertex - previous;
            const glm::dvec2 outgoing = next - vertex;
            return std::atan2(
                incoming.x * outgoing.y - incoming.y * outgoing.x,
                glm::dot(incoming, outgoing));
        }

        void PopulateBoundaryErrors(
            const PreparedDisk& disk,
            const BffParams& params,
            const std::vector<glm::vec2>& uvs,
            BffDiagnostics& diagnostics)
        {
            const std::size_t count = disk.Boundary.size();
            if (params.Mode == BffBoundaryMode::TargetLengths)
            {
                double sumSquared = 0.0;
                double maximum = 0.0;
                for (std::size_t i = 0u; i < count; ++i)
                {
                    const glm::dvec2 a(uvs[disk.Boundary[i].Index]);
                    const glm::dvec2 b(uvs[
                        disk.Boundary[(i + 1u) % count].Index]);
                    const double actual = glm::length(b - a);
                    const double relative = std::abs(
                        actual - params.BoundaryData[i])
                        / params.BoundaryData[i];
                    sumSquared += relative * relative;
                    maximum = std::max(maximum, relative);
                }
                diagnostics.RequestedLengthRmsRelativeError =
                    std::sqrt(sumSquared / static_cast<double>(count));
                diagnostics.RequestedLengthMaxRelativeError = maximum;
            }
            else if (params.Mode == BffBoundaryMode::TargetAngles)
            {
                double totalTurn = 0.0;
                for (std::size_t i = 0u; i < count; ++i)
                {
                    const glm::dvec2 previous(uvs[
                        disk.Boundary[(i + count - 1u) % count].Index]);
                    const glm::dvec2 vertex(uvs[disk.Boundary[i].Index]);
                    const glm::dvec2 next(uvs[
                        disk.Boundary[(i + 1u) % count].Index]);
                    totalTurn += ExteriorTurn(previous, vertex, next);
                }
                // Boundary halfedges may traverse the positive-area UV domain
                // clockwise. BFF boundary curvature is orientation-normalized
                // to a total turn of +2*pi, so compare in that convention.
                const double orientation = totalTurn < 0.0 ? -1.0 : 1.0;
                double sumSquared = 0.0;
                double maximum = 0.0;
                for (std::size_t i = 0u; i < count; ++i)
                {
                    const glm::dvec2 previous(uvs[
                        disk.Boundary[(i + count - 1u) % count].Index]);
                    const glm::dvec2 vertex(uvs[disk.Boundary[i].Index]);
                    const glm::dvec2 next(uvs[
                        disk.Boundary[(i + 1u) % count].Index]);
                    const double actual = orientation
                        * ExteriorTurn(previous, vertex, next);
                    const double error = std::abs(std::remainder(
                        actual - params.BoundaryData[i], 2.0 * kPi));
                    sumSquared += error * error;
                    maximum = std::max(maximum, error);
                }
                diagnostics.TargetAngleRmsError =
                    std::sqrt(sumSquared / static_cast<double>(count));
                diagnostics.TargetAngleMaxError = maximum;
            }
        }
    } // namespace

    const char* ToString(const BffStatus status) noexcept
    {
        switch (status)
        {
        case BffStatus::Success: return "Success";
        case BffStatus::EmptyMesh: return "EmptyMesh";
        case BffStatus::InsufficientVertices: return "InsufficientVertices";
        case BffStatus::NotTriangleMesh: return "NotTriangleMesh";
        case BffStatus::NotDiskTopology: return "NotDiskTopology";
        case BffStatus::DegenerateBoundary: return "DegenerateBoundary";
        case BffStatus::NonFiniteGeometry: return "NonFiniteGeometry";
        case BffStatus::DegenerateGeometry: return "DegenerateGeometry";
        case BffStatus::InvalidBoundaryMode: return "InvalidBoundaryMode";
        case BffStatus::InvalidTolerance: return "InvalidTolerance";
        case BffStatus::MismatchedBoundaryArray: return "MismatchedBoundaryArray";
        case BffStatus::NonFiniteBoundaryData: return "NonFiniteBoundaryData";
        case BffStatus::NonPositiveTargetLength: return "NonPositiveTargetLength";
        case BffStatus::InconsistentAngleSum: return "InconsistentAngleSum";
        case BffStatus::CurveClosureFailed: return "CurveClosureFailed";
        case BffStatus::NonPositiveScaledLength: return "NonPositiveScaledLength";
        case BffStatus::NonPositiveAdjustedLength: return "NonPositiveAdjustedLength";
        case BffStatus::SingularSystem: return "SingularSystem";
        case BffStatus::SolverFailed: return "SolverFailed";
        case BffStatus::NonFiniteResult: return "NonFiniteResult";
        case BffStatus::UnusableDiagnostics: return "UnusableDiagnostics";
        }
        return "Unknown";
    }

    BffResult ComputeBFF(
        const HalfedgeMesh::Mesh& mesh,
        const BffParams& params)
    {
        BffResult result{};
        const auto fail = [&result](const BffStatus status)
        {
            result.Status = status;
            result.UVs.clear();
            return result;
        };

        if (!std::isfinite(params.AngleSumTolerance)
            || !std::isfinite(params.DegeneracyTolerance))
        {
            return fail(BffStatus::InvalidTolerance);
        }
        if (params.AngleSumTolerance <= 0.0
            || params.DegeneracyTolerance <= 0.0)
        {
            return fail(BffStatus::InvalidTolerance);
        }

        switch (params.Mode)
        {
        case BffBoundaryMode::AutomaticConformal:
        case BffBoundaryMode::TargetLengths:
        case BffBoundaryMode::TargetAngles:
            break;
        default:
            return fail(BffStatus::InvalidBoundaryMode);
        }

        PreparedDisk disk{};
        const BffStatus prepared = PrepareDisk(
            mesh, params.DegeneracyTolerance, disk);
        if (prepared != BffStatus::Success)
            return fail(prepared);

        result.Diagnostics.BoundaryVertexCount = disk.Boundary.size();
        result.Diagnostics.InteriorVertexCount = disk.Interior.size();
        const std::size_t boundaryCount = disk.Boundary.size();
        if (params.Mode == BffBoundaryMode::AutomaticConformal)
        {
            if (!params.BoundaryData.empty())
                return fail(BffStatus::MismatchedBoundaryArray);
        }
        else if (params.BoundaryData.size() != boundaryCount)
        {
            return fail(BffStatus::MismatchedBoundaryArray);
        }

        if (params.Mode != BffBoundaryMode::AutomaticConformal)
        {
            if (!std::all_of(
                    params.BoundaryData.begin(), params.BoundaryData.end(),
                    [](const double value) { return std::isfinite(value); }))
            {
                return fail(BffStatus::NonFiniteBoundaryData);
            }
        }
        if (params.Mode == BffBoundaryMode::TargetLengths
            && !std::all_of(
                params.BoundaryData.begin(), params.BoundaryData.end(),
                [](const double value) { return value > 0.0; }))
        {
            return fail(BffStatus::NonPositiveTargetLength);
        }
        if (params.Mode == BffBoundaryMode::TargetAngles)
        {
            result.Diagnostics.TargetAngleSum = std::accumulate(
                params.BoundaryData.begin(), params.BoundaryData.end(), 0.0);
            if (!std::isfinite(result.Diagnostics.TargetAngleSum)
                || std::abs(result.Diagnostics.TargetAngleSum - 2.0 * kPi)
                    > params.AngleSumTolerance)
            {
                return fail(BffStatus::InconsistentAngleSum);
            }
        }

        Sparse::SparseLDLT dirichletSolver{};
        const Sparse::SparseLDLT* dirichlet = nullptr;
        if (!disk.Interior.empty())
        {
            const Sparse::SparseBuildResult built =
                BuildDirichletMatrix(mesh, disk);
            if (!built.Valid)
                return fail(BffStatus::SingularSystem);
            const Sparse::SparseFactorizationDiagnostics factor =
                dirichletSolver.factor(built.Matrix);
            if (!factor.Succeeded())
                return fail(BffStatus::SingularSystem);
            dirichlet = &dirichletSolver;
        }

        const GroundedSystem grounded = BuildGroundedMatrix(mesh, disk);
        if (!grounded.Built.Valid)
            return fail(BffStatus::SingularSystem);
        Sparse::SparseLDLT neumannSolver{};
        const Sparse::SparseFactorizationDiagnostics neumannFactor =
            neumannSolver.factor(grounded.Built.Matrix);
        if (!neumannFactor.Succeeded())
            return fail(BffStatus::SingularSystem);
        std::vector<double> scaleFactors(mesh.VerticesSize(), 0.0);
        std::vector<double> targetAngles(boundaryCount, 0.0);
        if (params.Mode == BffBoundaryMode::TargetAngles)
        {
            targetAngles = params.BoundaryData;
            std::vector<double> source(mesh.VerticesSize(), 0.0);
            for (const VertexHandle vertex : disk.Interior)
                source[vertex.Index] = -disk.GaussianCurvature[vertex.Index];
            for (std::size_t i = 0u; i < boundaryCount; ++i)
            {
                const VertexHandle vertex = disk.Boundary[i];
                source[vertex.Index] = targetAngles[i]
                    - disk.BoundaryCurvature[vertex.Index];
            }
            if (!SolveGrounded(
                    mesh, disk, grounded, neumannSolver, source, scaleFactors))
            {
                return fail(BffStatus::SolverFailed);
            }
            ++result.Diagnostics.NeumannSolveCount;
        }
        else
        {
            std::vector<double> boundaryScale(boundaryCount, 0.0);
            if (params.Mode == BffBoundaryMode::TargetLengths)
            {
                for (std::size_t i = 0u; i < boundaryCount; ++i)
                {
                    const std::size_t previous =
                        (i + boundaryCount - 1u) % boundaryCount;
                    const double previousTarget = params.BoundaryData[previous];
                    const double nextTarget = params.BoundaryData[i];
                    const double previousLog = std::log(
                        previousTarget / disk.BoundaryLengths[previous]);
                    const double nextLog = std::log(
                        nextTarget / disk.BoundaryLengths[i]);
                    boundaryScale[i] =
                        (previousTarget * previousLog + nextTarget * nextLog)
                        / (previousTarget + nextTarget);
                    if (!std::isfinite(boundaryScale[i]))
                        return fail(BffStatus::NonFiniteBoundaryData);
                }
            }

            std::vector<double> source(mesh.VerticesSize(), 0.0);
            for (const VertexHandle vertex : disk.Interior)
                source[vertex.Index] = -disk.GaussianCurvature[vertex.Index];
            if (!SolveDirichlet(
                    mesh, disk, dirichlet, boundaryScale, source, scaleFactors))
            {
                return fail(BffStatus::SolverFailed);
            }
            if (!disk.Interior.empty())
                ++result.Diagnostics.DirichletSolveCount;
            for (std::size_t i = 0u; i < boundaryCount; ++i)
            {
                const VertexHandle vertex = disk.Boundary[i];
                targetAngles[i] = disk.BoundaryCurvature[vertex.Index]
                    + ApplyLaplacianAtVertex(
                        mesh, disk, vertex, scaleFactors);
            }
        }

        std::vector<double> scaledLengths(boundaryCount, 0.0);
        double scaledLengthScale = 0.0;
        for (std::size_t i = 0u; i < boundaryCount; ++i)
        {
            const VertexHandle a = disk.Boundary[i];
            const VertexHandle b =
                disk.Boundary[(i + 1u) % boundaryCount];
            const double scale = std::exp(
                0.5 * (scaleFactors[a.Index] + scaleFactors[b.Index]));
            scaledLengths[i] = scale * disk.BoundaryLengths[i];
            if (!std::isfinite(scaledLengths[i])
                || scaledLengths[i] <= 0.0)
            {
                return fail(BffStatus::NonPositiveScaledLength);
            }
            scaledLengthScale = std::max(
                scaledLengthScale, scaledLengths[i]);
        }
        for (const double length : scaledLengths)
        {
            if (length <= params.DegeneracyTolerance * scaledLengthScale)
                return fail(BffStatus::NonPositiveScaledLength);
        }

        std::vector<double> adjustedLengths{};
        std::vector<glm::dvec2> boundaryCurve{};
        const CurveClosureStatus closureStatus = CloseBoundaryCurve(
            disk, scaledLengths, targetAngles,
            params.DegeneracyTolerance, adjustedLengths,
            boundaryCurve, result.Diagnostics);
        if (closureStatus == CurveClosureStatus::NonPositiveAdjustedLength)
            return fail(BffStatus::NonPositiveAdjustedLength);
        if (closureStatus != CurveClosureStatus::Success)
        {
            return fail(BffStatus::CurveClosureFailed);
        }

        std::vector<double> boundaryX(boundaryCount, 0.0);
        std::vector<double> boundaryY(boundaryCount, 0.0);
        for (std::size_t i = 0u; i < boundaryCount; ++i)
        {
            boundaryX[i] = boundaryCurve[i].x;
            boundaryY[i] = boundaryCurve[i].y;
        }
        const std::vector<double> zeroSource(mesh.VerticesSize(), 0.0);
        std::vector<double> x{};
        std::vector<double> y{};
        if (!SolveDirichlet(
                mesh, disk, dirichlet, boundaryX, zeroSource, x))
        {
            return fail(BffStatus::SolverFailed);
        }
        if (!disk.Interior.empty())
            ++result.Diagnostics.DirichletSolveCount;

        if (params.Mode == BffBoundaryMode::TargetAngles)
        {
            if (!SolveDirichlet(
                    mesh, disk, dirichlet, boundaryY, zeroSource, y))
            {
                return fail(BffStatus::SolverFailed);
            }
            if (!disk.Interior.empty())
                ++result.Diagnostics.DirichletSolveCount;
        }
        else
        {
            std::vector<double> hilbertSource(mesh.VerticesSize(), 0.0);
            for (std::size_t i = 0u; i < boundaryCount; ++i)
            {
                const std::size_t previous =
                    (i + boundaryCount - 1u) % boundaryCount;
                const std::size_t next = (i + 1u) % boundaryCount;
                hilbertSource[disk.Boundary[i].Index] =
                    0.5 * (x[disk.Boundary[next].Index]
                           - x[disk.Boundary[previous].Index]);
            }
            if (!SolveGrounded(
                    mesh, disk, grounded, neumannSolver, hilbertSource, y))
            {
                return fail(BffStatus::SolverFailed);
            }
            ++result.Diagnostics.NeumannSolveCount;
        }

        result.UVs.assign(mesh.VerticesSize(), glm::vec2(0.0f));
        for (const VertexHandle vertex : disk.Active)
        {
            const glm::vec2 uv(
                static_cast<float>(x[vertex.Index]),
                static_cast<float>(y[vertex.Index]));
            if (!IsFinite(uv))
                return fail(BffStatus::NonFiniteResult);
            result.UVs[vertex.Index] = uv;
        }
        FixOrientation(mesh, result.UVs);
        PopulateBoundaryErrors(disk, params, result.UVs, result.Diagnostics);

        double uvScale = 0.0;
        for (std::size_t ei = 0u; ei < mesh.EdgesSize(); ++ei)
        {
            const EdgeHandle edge{static_cast<PropertyIndex>(ei)};
            if (mesh.IsDeleted(edge))
                continue;
            const HalfedgeHandle halfedge{
                static_cast<PropertyIndex>(2u * ei)};
            uvScale = std::max(
                uvScale,
                static_cast<double>(glm::distance(
                    result.UVs[mesh.FromVertex(halfedge).Index],
                    result.UVs[mesh.ToVertex(halfedge).Index])));
        }
        if (!(uvScale > 0.0) || !std::isfinite(uvScale))
            return fail(BffStatus::NonFiniteResult);

        const ParameterizationDiagnosticsOptions diagnosticOptions{
            .DegeneratePositionAreaEpsilon = params.DegeneracyTolerance
                * disk.CharacteristicLength * disk.CharacteristicLength,
            .DegenerateUvAreaEpsilon = params.DegeneracyTolerance
                * uvScale * uvScale,
            .SingularValueEpsilon = params.DegeneracyTolerance
                * uvScale / disk.CharacteristicLength,
            .BoundaryPositionLengthEpsilon = params.DegeneracyTolerance
                * disk.CharacteristicLength,
            .BoundaryUvLengthEpsilon = params.DegeneracyTolerance * uvScale,
        };
        result.Diagnostics.Quality = EvaluateParameterizationDiagnostics(
            mesh, std::span<const glm::vec2>(result.UVs), diagnosticOptions);
        if (result.Diagnostics.Quality.Status
                != ParameterizationDiagnosticsStatus::Success
            || !result.Diagnostics.Quality.HasUsableFaces())
        {
            return fail(BffStatus::UnusableDiagnostics);
        }

        result.Status = BffStatus::Success;
        return result;
    }
} // namespace Geometry::Parameterization

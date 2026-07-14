// GEOM-014 — manifest-backed FA-QEM adaptation quality smoke.

#include "Bench.SimplificationQualitySmoke.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <map>
#include <vector>

#include <glm/glm.hpp>

import Geometry.HalfedgeMesh;
import Geometry.MeshClosestFace;
import Geometry.Properties;
import Geometry.Simplification;

namespace Intrinsic::Bench::Geometry
{
    namespace
    {
        constexpr int kWarmupIterations = 1;
        constexpr int kMeasuredIterations = 2;
        constexpr int kBarycentricResolution = 4;
        constexpr std::size_t kTargetFaceCount = 24u;
        constexpr double kQualityRegressionTolerance = 1.0e-6;

        [[nodiscard]] ::Geometry::HalfedgeMesh::Mesh MakeTessellatedCube(
            const int subdivisions,
            const float halfExtent = 1.0f)
        {
            using ::Geometry::VertexHandle;
            ::Geometry::HalfedgeMesh::Mesh mesh;
            std::map<std::array<int, 3>, VertexHandle> vertices;

            auto vertexAt = [&](const int i, const int j, const int k) -> VertexHandle
            {
                const std::array<int, 3> key{i, j, k};
                if (const auto found = vertices.find(key); found != vertices.end())
                    return found->second;
                const glm::vec3 position(
                    -halfExtent + 2.0f * halfExtent * static_cast<float>(i) / static_cast<float>(subdivisions),
                    -halfExtent + 2.0f * halfExtent * static_cast<float>(j) / static_cast<float>(subdivisions),
                    -halfExtent + 2.0f * halfExtent * static_cast<float>(k) / static_cast<float>(subdivisions));
                const VertexHandle vertex = mesh.AddVertex(position);
                vertices.emplace(key, vertex);
                return vertex;
            };

            const auto addOrientedTriangle =
                [&](const VertexHandle a, const VertexHandle b, const VertexHandle c, const glm::vec3 outward)
            {
                const glm::vec3 normal = glm::cross(
                    mesh.Position(b) - mesh.Position(a),
                    mesh.Position(c) - mesh.Position(a));
                if (glm::dot(normal, outward) < 0.0f)
                    (void)mesh.AddTriangle(a, c, b);
                else
                    (void)mesh.AddTriangle(a, b, c);
            };

            for (int axis = 0; axis < 3; ++axis)
            {
                for (int side = 0; side < 2; ++side)
                {
                    const int level = side == 0 ? 0 : subdivisions;
                    glm::vec3 outward{0.0f};
                    outward[axis] = side == 0 ? -1.0f : 1.0f;
                    const int axis1 = (axis + 1) % 3;
                    const int axis2 = (axis + 2) % 3;
                    for (int i = 0; i < subdivisions; ++i)
                    {
                        for (int j = 0; j < subdivisions; ++j)
                        {
                            const auto corner = [&](const int u, const int v)
                            {
                                std::array<int, 3> coordinates{};
                                coordinates[static_cast<std::size_t>(axis)] = level;
                                coordinates[static_cast<std::size_t>(axis1)] = u;
                                coordinates[static_cast<std::size_t>(axis2)] = v;
                                return vertexAt(coordinates[0], coordinates[1], coordinates[2]);
                            };
                            const VertexHandle v00 = corner(i, j);
                            const VertexHandle v10 = corner(i + 1, j);
                            const VertexHandle v11 = corner(i + 1, j + 1);
                            const VertexHandle v01 = corner(i, j + 1);
                            addOrientedTriangle(v00, v10, v11, outward);
                            addOrientedTriangle(v00, v11, v01, outward);
                        }
                    }
                }
            }
            return mesh;
        }

        struct SurfaceDistanceSummary
        {
            double RmsDistance{0.0};
            double MaxDistance{0.0};
            std::size_t SampleCount{0u};
            bool Succeeded{false};
        };

        [[nodiscard]] SurfaceDistanceSummary SampleReferenceToResultSurfaceDistance(
            const ::Geometry::HalfedgeMesh::Mesh& reference,
            const ::Geometry::HalfedgeMesh::Mesh& result)
        {
            ::Geometry::MeshClosestFaceIndex resultIndex;
            if (!resultIndex.Build(result))
                return {};

            SurfaceDistanceSummary summary{};
            double sumSquaredDistance = 0.0;
            for (std::size_t faceIndex = 0; faceIndex < reference.FacesSize(); ++faceIndex)
            {
                const ::Geometry::FaceHandle face{
                    static_cast<::Geometry::PropertyIndex>(faceIndex)};
                if (!reference.IsValid(face) || reference.IsDeleted(face))
                    continue;

                std::vector<glm::vec3> polygon;
                for (const ::Geometry::VertexHandle vertex : reference.VerticesAroundFace(face))
                {
                    if (reference.IsValid(vertex) && !reference.IsDeleted(vertex))
                        polygon.push_back(reference.Position(vertex));
                }

                for (std::size_t triangle = 1u; triangle + 1u < polygon.size(); ++triangle)
                {
                    const glm::vec3 p0 = polygon[0u];
                    const glm::vec3 p1 = polygon[triangle];
                    const glm::vec3 p2 = polygon[triangle + 1u];
                    for (int i = 0; i <= kBarycentricResolution; ++i)
                    {
                        for (int j = 0; j <= kBarycentricResolution - i; ++j)
                        {
                            const int k = kBarycentricResolution - i - j;
                            const glm::vec3 sample =
                                (static_cast<float>(i) * p0
                                 + static_cast<float>(j) * p1
                                 + static_cast<float>(k) * p2)
                                / static_cast<float>(kBarycentricResolution);
                            const ::Geometry::MeshClosestFaceResult nearest = resultIndex.Query(sample);
                            if (!nearest.Found
                                || nearest.Status != ::Geometry::MeshClosestFaceStatus::Success
                                || !std::isfinite(nearest.SquaredDistance)
                                || nearest.SquaredDistance < 0.0f)
                            {
                                return {};
                            }
                            const double squaredDistance =
                                static_cast<double>(nearest.SquaredDistance);
                            sumSquaredDistance += squaredDistance;
                            summary.MaxDistance = std::max(
                                summary.MaxDistance,
                                std::sqrt(squaredDistance));
                            ++summary.SampleCount;
                        }
                    }
                }
            }

            if (summary.SampleCount == 0u)
                return {};
            summary.RmsDistance = std::sqrt(
                sumSquaredDistance / static_cast<double>(summary.SampleCount));
            summary.Succeeded = std::isfinite(summary.RmsDistance)
                && std::isfinite(summary.MaxDistance);
            return summary;
        }

        struct TickResult
        {
            SurfaceDistanceSummary FeatureAwareDistance{};
            SurfaceDistanceSummary ClassicalDistance{};
            SurfaceDistanceSummary SensitivityControlDistance{};
            std::size_t FeatureAwareFinalFaceCount{0u};
            std::size_t ClassicalFinalFaceCount{0u};
            std::size_t FeatureAwarePinnedVertexCount{0u};
            std::size_t FeatureAwareQualityRejectionCount{0u};
            double QualityErrorL2{0.0};
            double QualityErrorLinf{0.0};
            bool Succeeded{false};
        };

        [[nodiscard]] TickResult Tick()
        {
            const auto reference = MakeTessellatedCube(4);

            auto featureAwareMesh = MakeTessellatedCube(4);
            ::Geometry::Simplification::Params featureAwareParams;
            featureAwareParams.TargetFaces = kTargetFaceCount;
            const auto featureAwareResult =
                ::Geometry::Simplification::Simplify(featureAwareMesh, featureAwareParams);

            auto classicalMesh = MakeTessellatedCube(4);
            ::Geometry::Simplification::Params classicalParams;
            classicalParams.Metric = ::Geometry::Simplification::Metric::ClassicalQEM;
            classicalParams.TargetFaces = kTargetFaceCount;
            const auto classicalResult =
                ::Geometry::Simplification::Simplify(classicalMesh, classicalParams);

            TickResult tick{};
            if (!featureAwareResult.has_value() || !classicalResult.has_value())
                return tick;

            featureAwareMesh.GarbageCollection();
            classicalMesh.GarbageCollection();
            tick.FeatureAwareDistance =
                SampleReferenceToResultSurfaceDistance(reference, featureAwareMesh);
            tick.ClassicalDistance =
                SampleReferenceToResultSurfaceDistance(reference, classicalMesh);

            auto translatedControl = MakeTessellatedCube(4);
            for (std::size_t vertexIndex = 0u;
                 vertexIndex < translatedControl.VerticesSize();
                 ++vertexIndex)
            {
                const ::Geometry::VertexHandle vertex{
                    static_cast<::Geometry::PropertyIndex>(vertexIndex)};
                if (!translatedControl.IsDeleted(vertex))
                    translatedControl.Position(vertex) += glm::vec3(0.25f, 0.0f, 0.0f);
            }
            tick.SensitivityControlDistance =
                SampleReferenceToResultSurfaceDistance(reference, translatedControl);

            tick.FeatureAwareFinalFaceCount = featureAwareResult->FinalFaceCount;
            tick.ClassicalFinalFaceCount = classicalResult->FinalFaceCount;
            tick.FeatureAwarePinnedVertexCount =
                featureAwareResult->SharpFeatureVerticesPinned;
            tick.FeatureAwareQualityRejectionCount =
                featureAwareResult->CollapsesRejectedQuality;
            tick.QualityErrorL2 = std::max(
                0.0,
                tick.FeatureAwareDistance.RmsDistance
                    - tick.ClassicalDistance.RmsDistance);
            tick.QualityErrorLinf = std::max(
                0.0,
                tick.FeatureAwareDistance.MaxDistance
                    - tick.ClassicalDistance.MaxDistance);

            tick.Succeeded = tick.FeatureAwareDistance.Succeeded
                && tick.ClassicalDistance.Succeeded
                && tick.SensitivityControlDistance.Succeeded
                && tick.FeatureAwareDistance.SampleCount
                    == tick.ClassicalDistance.SampleCount
                && tick.FeatureAwareFinalFaceCount == kTargetFaceCount
                && tick.ClassicalFinalFaceCount == kTargetFaceCount
                && featureAwareMesh.FaceCount() == kTargetFaceCount
                && classicalMesh.FaceCount() == kTargetFaceCount
                && tick.FeatureAwarePinnedVertexCount >= 8u
                && tick.FeatureAwareQualityRejectionCount > 0u
                && tick.SensitivityControlDistance.MaxDistance > 0.20
                && tick.QualityErrorL2 <= kQualityRegressionTolerance
                && tick.QualityErrorLinf <= kQualityRegressionTolerance;
            return tick;
        }
    }

    SimplificationQualitySmokeMetrics RunSimplificationQualitySmoke()
    {
        for (int iteration = 0; iteration < kWarmupIterations; ++iteration)
            (void)Tick();

        TickResult last{};
        const auto start = std::chrono::steady_clock::now();
        for (int iteration = 0; iteration < kMeasuredIterations; ++iteration)
            last = Tick();
        const auto end = std::chrono::steady_clock::now();

        const auto totalNanoseconds =
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        const double meanMilliseconds =
            (static_cast<double>(totalNanoseconds)
             / static_cast<double>(kMeasuredIterations))
            * 1.0e-6;

        SimplificationQualitySmokeMetrics metrics{};
        metrics.RuntimeMilliseconds = meanMilliseconds;
        metrics.QualityErrorL2 = last.QualityErrorL2;
        metrics.QualityErrorLinf = last.QualityErrorLinf;
        metrics.FeatureAwareRmsDistance = last.FeatureAwareDistance.RmsDistance;
        metrics.ClassicalRmsDistance = last.ClassicalDistance.RmsDistance;
        metrics.FeatureAwareMaxDistance = last.FeatureAwareDistance.MaxDistance;
        metrics.ClassicalMaxDistance = last.ClassicalDistance.MaxDistance;
        metrics.SensitivityControlMaxDistance =
            last.SensitivityControlDistance.MaxDistance;
        metrics.SampleCount = last.FeatureAwareDistance.SampleCount;
        metrics.TargetFaceCount = kTargetFaceCount;
        metrics.FeatureAwareFinalFaceCount = last.FeatureAwareFinalFaceCount;
        metrics.ClassicalFinalFaceCount = last.ClassicalFinalFaceCount;
        metrics.FeatureAwarePinnedVertexCount =
            last.FeatureAwarePinnedVertexCount;
        metrics.FeatureAwareQualityRejectionCount =
            last.FeatureAwareQualityRejectionCount;
        metrics.Succeeded = last.Succeeded;
        return metrics;
    }
}

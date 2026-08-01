// METHOD-018 — deterministic EAR/anisotropic-WLOP correctness smoke.

#include "Bench.EdgeAwareResamplingReferenceSmoke.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <vector>

#include <glm/glm.hpp>

import Geometry.PointCloud.Consolidation;

namespace Intrinsic::Bench::Geometry
{
    namespace
    {
        namespace Consolidation = ::Geometry::PointCloud::Consolidation;

        constexpr int kWarmupIterations = 1;
        constexpr int kMeasuredIterations = 4;
        constexpr double kDihedralAngle = 1.0471975511965976;

        struct OrientedPoints
        {
            std::vector<glm::vec3> Positions{};
            std::vector<glm::vec3> Normals{};
            std::size_t PointsPerSide{0u};
        };

        // Confirmation dimensions, spacing, and noise sequence deliberately
        // differ from the unit-test screening fixture.
        [[nodiscard]] OrientedPoints ConfirmationDihedral(
            const int radialCount = 6,
            const int creaseCount = 6)
        {
            const std::array<glm::vec3, 2u> tangents{
                glm::vec3{-1.0f, 0.0f, 0.0f},
                glm::vec3{
                    static_cast<float>(std::cos(kDihedralAngle)), 0.0f,
                    static_cast<float>(std::sin(kDihedralAngle))},
            };
            const std::array<glm::vec3, 2u> normals{
                glm::vec3{0.0f, 0.0f, 1.0f},
                glm::vec3{
                    static_cast<float>(-std::sin(kDihedralAngle)), 0.0f,
                    static_cast<float>(std::cos(kDihedralAngle))},
            };
            OrientedPoints fixture{};
            fixture.PointsPerSide = static_cast<std::size_t>(
                radialCount * creaseCount);
            fixture.Positions.reserve(2u * fixture.PointsPerSide);
            fixture.Normals.reserve(2u * fixture.PointsPerSide);
            for (std::size_t side = 0u; side < 2u; ++side)
            {
                for (int y = 0; y < creaseCount; ++y)
                {
                    for (int radial = 0; radial < radialCount; ++radial)
                    {
                        const float distance = 0.07f + 0.115f * radial;
                        const float alongCrease =
                            (static_cast<float>(y) -
                             0.5f * static_cast<float>(creaseCount - 1)) *
                            0.145f;
                        const int noiseCode =
                            (radial * 19 + y * 13 +
                             static_cast<int>(side) * 5 + 2) % 7 -
                            3;
                        const float noise = 0.0055f * noiseCode;
                        fixture.Positions.push_back(
                            distance * tangents[side] +
                            glm::vec3{0.0f, alongCrease, 0.0f} +
                            noise * normals[side]);
                        fixture.Normals.push_back(normals[side]);
                    }
                }
            }
            return fixture;
        }

        [[nodiscard]] double MeanExpectedPlaneError(
            const std::span<const glm::vec3> points,
            const std::span<const glm::vec3> normals)
        {
            if (points.size() != normals.size() || points.empty())
                return std::numeric_limits<double>::infinity();
            double sum = 0.0;
            for (std::size_t i = 0u; i < points.size(); ++i)
            {
                sum += std::abs(glm::dot(
                    glm::dvec3(points[i]), glm::dvec3(normals[i])));
            }
            return sum / static_cast<double>(points.size());
        }

        [[nodiscard]] double MinimumPairwiseDistance(
            const std::span<const glm::vec3> points)
        {
            double minimum = std::numeric_limits<double>::infinity();
            for (std::size_t i = 0u; i < points.size(); ++i)
            {
                for (std::size_t j = i + 1u; j < points.size(); ++j)
                {
                    minimum = std::min(
                        minimum,
                        static_cast<double>(
                            glm::distance(points[i], points[j])));
                }
            }
            return minimum;
        }

        [[nodiscard]] bool Finite(
            const std::span<const glm::vec3> values)
        {
            return std::ranges::all_of(values, [](const glm::vec3 value)
            {
                return std::isfinite(value.x) && std::isfinite(value.y) &&
                       std::isfinite(value.z);
            });
        }

        [[nodiscard]] EdgeAwareResamplingReferenceSmokeMetrics Tick()
        {
            EdgeAwareResamplingReferenceSmokeMetrics metrics{};
            const OrientedPoints fixture = ConfirmationDihedral();

            Consolidation::Params isotropicParams{
                .Method = Consolidation::WlopStrategy{},
                .SupportRadius = 0.20,
                .RepulsionWeight = 0.0,
                .MaxIterations = 3u,
                .ConvergenceTolerance = 0.0,
                .TargetPointCount = 0u,
                .Seed = 53u,
            };
            const auto isotropic = Consolidation::Consolidate(
                fixture.Positions, fixture.Normals, isotropicParams);

            auto anisotropicParams = isotropicParams;
            anisotropicParams.Method = Consolidation::WlopStrategy{
                .Weighting = Consolidation::WeightingMode::Anisotropic,
                .NormalSource =
                    Consolidation::NormalSourcePolicy::RequireAuthored,
                .NormalAngleRadians =
                    3.14159265358979323846 / 12.0,
                .NormalRefinementRounds = 3u,
            };
            anisotropicParams.ConvergenceTolerance = 1.0;
            const auto anisotropic = Consolidation::Consolidate(
                fixture.Positions, fixture.Normals, anisotropicParams);

            auto earParams = anisotropicParams;
            earParams.Method = Consolidation::EarStrategy{
                .NormalSource =
                    Consolidation::NormalSourcePolicy::RequireAuthored,
                .NormalAngleRadians =
                    3.14159265358979323846 / 12.0,
                .EdgeSensitivity = 5.0,
                .NormalRefinementRounds = 3u,
            };
            earParams.SupportRadius = 0.62;
            earParams.RepulsionWeight = 0.10;
            earParams.TargetPointCount = fixture.Positions.size() + 8u;
            earParams.MaxOutputPointCount = earParams.TargetPointCount;
            const auto ear = Consolidation::Consolidate(
                fixture.Positions, fixture.Normals, earParams);
            const auto repeated = Consolidation::Consolidate(
                fixture.Positions, fixture.Normals, earParams);

            auto requireNormalsParams = anisotropicParams;
            const auto missingNormals = Consolidation::Consolidate(
                fixture.Positions, requireNormalsParams);

            if (isotropic.Positions.size() != fixture.Positions.size() ||
                anisotropic.Positions.size() != fixture.Positions.size() ||
                anisotropic.Normals.size() != fixture.Normals.size() ||
                ear.Positions.size() != earParams.TargetPointCount ||
                ear.Normals.size() != ear.Positions.size())
            {
                return metrics;
            }

            metrics.RawExpectedPlaneError = MeanExpectedPlaneError(
                fixture.Positions, fixture.Normals);
            metrics.IsotropicExpectedPlaneError = MeanExpectedPlaneError(
                isotropic.Positions, fixture.Normals);
            metrics.AnisotropicExpectedPlaneError = MeanExpectedPlaneError(
                anisotropic.Positions, fixture.Normals);
            metrics.QualityErrorL2 = metrics.AnisotropicExpectedPlaneError;
            metrics.EdgeSharpnessPreservation =
                1.0 - metrics.AnisotropicExpectedPlaneError /
                    metrics.IsotropicExpectedPlaneError;

            glm::dvec3 firstNormal{0.0};
            glm::dvec3 secondNormal{0.0};
            for (std::size_t i = 0u; i < fixture.PointsPerSide; ++i)
            {
                firstNormal += glm::dvec3(anisotropic.Normals[i]);
                secondNormal += glm::dvec3(
                    anisotropic.Normals[i + fixture.PointsPerSide]);
            }
            firstNormal = glm::normalize(firstNormal);
            secondNormal = glm::normalize(secondNormal);
            metrics.NormalAngularErrorRadians = std::abs(
                std::acos(std::clamp(
                    glm::dot(firstNormal, secondNormal), -1.0, 1.0)) -
                kDihedralAngle);
            metrics.UniformityMinimumPairwiseDistance =
                MinimumPairwiseDistance(ear.Positions);
            metrics.InputPointCount = fixture.Positions.size();
            metrics.OutputPointCount = ear.Positions.size();
            metrics.InsertedPointCount = ear.Diagnostics.InsertedPointCount;
            metrics.EdgePriorityEvaluations =
                ear.Diagnostics.EdgePriorityEvaluations;
            for (std::size_t i = fixture.Positions.size();
                 i < ear.Positions.size(); ++i)
            {
                if (std::hypot(
                        static_cast<double>(ear.Positions[i].x),
                        static_cast<double>(ear.Positions[i].z)) < 0.30)
                {
                    ++metrics.InsertedNearFeatureCount;
                }
            }
            metrics.AnisotropicIterations =
                anisotropic.Diagnostics.Iterations;
            metrics.NormalRefinementIterations =
                anisotropic.Diagnostics.NormalRefinementIterations;
            metrics.UsedAuthoredNormals =
                anisotropic.Diagnostics.UsedAuthoredNormals &&
                ear.Diagnostics.UsedAuthoredNormals;
            metrics.NormalsRequiredFailedClosed =
                missingNormals.State ==
                    Consolidation::Status::NormalsRequired &&
                missingNormals.Positions.empty() &&
                missingNormals.Normals.empty();
            metrics.Deterministic = repeated.State == ear.State &&
                repeated.Positions == ear.Positions &&
                repeated.Normals == ear.Normals;
            metrics.Succeeded = anisotropic.Succeeded() && ear.Succeeded() &&
                Finite(anisotropic.Positions) && Finite(anisotropic.Normals) &&
                Finite(ear.Positions) && Finite(ear.Normals) &&
                metrics.AnisotropicExpectedPlaneError <
                    metrics.RawExpectedPlaneError &&
                metrics.EdgeSharpnessPreservation > 0.15 &&
                metrics.NormalAngularErrorRadians < 1.0e-4 &&
                metrics.InsertedPointCount == 8u &&
                metrics.InsertedNearFeatureCount >= 6u &&
                metrics.UniformityMinimumPairwiseDistance > 1.0e-5 &&
                metrics.UsedAuthoredNormals &&
                metrics.NormalsRequiredFailedClosed &&
                metrics.Deterministic;
            return metrics;
        }
    }

    EdgeAwareResamplingReferenceSmokeMetrics
    RunEdgeAwareResamplingReferenceSmoke()
    {
        for (int i = 0; i < kWarmupIterations; ++i)
            static_cast<void>(Tick());

        EdgeAwareResamplingReferenceSmokeMetrics last{};
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < kMeasuredIterations; ++i)
            last = Tick();
        const auto finish = std::chrono::steady_clock::now();
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                finish - start).count();
        last.RuntimeMilliseconds =
            static_cast<double>(elapsed) * 1.0e-6 /
            static_cast<double>(kMeasuredIterations);
        return last;
    }
}

// METHOD-019 — frozen paired LOP-family optimized CPU comparison.

#include "Bench.LopFamilyComparisonSmoke.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

import Geometry.PointCloud.Consolidation;

namespace Intrinsic::Bench::Geometry
{
    namespace
    {
        namespace Consolidation = ::Geometry::PointCloud::Consolidation;

        struct Fixture
        {
            const char* Name{"invalid"};
            std::vector<glm::vec3> Positions{};
            std::vector<glm::vec3> Normals{};
            Consolidation::Params Params{};
        };

        struct TimedResult
        {
            Consolidation::Result Value{};
            double Milliseconds{0.0};
        };

        [[nodiscard]] float Height(const float x, const float y) noexcept
        {
            return 0.018f * std::sin(13.0f * x + 7.0f * y) +
                   0.006f * std::cos(5.0f * x - 11.0f * y);
        }

        [[nodiscard]] std::vector<glm::vec3> Plane(
            const int countX,
            const int countY,
            const bool densityWarp)
        {
            std::vector<glm::vec3> points{};
            points.reserve(static_cast<std::size_t>(countX * countY));
            for (int y = 0; y < countY; ++y)
            {
                const float v = static_cast<float>(y) /
                    static_cast<float>(countY - 1);
                const float py = 2.0f * v - 1.0f;
                for (int x = 0; x < countX; ++x)
                {
                    const float u = static_cast<float>(x) /
                        static_cast<float>(countX - 1);
                    const float warped = densityWarp
                        ? std::pow(u, 1.7f)
                        : u;
                    const float px = 2.0f * warped - 1.0f;
                    points.emplace_back(px, py, Height(px, py));
                }
            }
            return points;
        }

        [[nodiscard]] Fixture LopFixture()
        {
            Fixture fixture{};
            fixture.Name = "lop";
            fixture.Positions = Plane(32, 32, false);
            fixture.Params.Method = Consolidation::LopStrategy{};
            fixture.Params.SupportRadius = 0.18;
            fixture.Params.RepulsionWeight = 0.35;
            fixture.Params.MaxIterations = 8u;
            fixture.Params.ConvergenceTolerance = 0.0;
            fixture.Params.TargetPointCount = 256u;
            fixture.Params.Seed = 1901u;
            fixture.Params.MaxInputPointCount = fixture.Positions.size();
            fixture.Params.MaxOutputPointCount = 256u;
            return fixture;
        }

        [[nodiscard]] Fixture WlopFixture()
        {
            Fixture fixture{};
            fixture.Name = "wlop";
            fixture.Positions = Plane(40, 24, true);
            fixture.Params.Method = Consolidation::WlopStrategy{};
            fixture.Params.SupportRadius = 0.20;
            fixture.Params.RepulsionWeight = 0.35;
            fixture.Params.MaxIterations = 8u;
            fixture.Params.ConvergenceTolerance = 0.0;
            fixture.Params.TargetPointCount = 240u;
            fixture.Params.Seed = 1902u;
            fixture.Params.MaxInputPointCount = fixture.Positions.size();
            fixture.Params.MaxOutputPointCount = 240u;
            return fixture;
        }

        [[nodiscard]] Fixture ClopFixture()
        {
            Fixture fixture{};
            fixture.Name = "clop";
            fixture.Positions = Plane(30, 24, false);
            fixture.Params.Method = Consolidation::ClopStrategy{
                .MixtureComponentCount = 24u,
                .MixtureMaxIterations = 40u,
                .MixtureRelativeTolerance = 1.0e-5,
                .CovarianceFloor = 1.0e-6,
            };
            fixture.Params.SupportRadius = 0.22;
            fixture.Params.RepulsionWeight = 0.35;
            fixture.Params.MaxIterations = 6u;
            fixture.Params.ConvergenceTolerance = 0.0;
            fixture.Params.TargetPointCount = 240u;
            fixture.Params.Seed = 1903u;
            fixture.Params.MaxInputPointCount = fixture.Positions.size();
            fixture.Params.MaxOutputPointCount = 240u;
            return fixture;
        }

        [[nodiscard]] Fixture EarFixture()
        {
            constexpr double angle = 1.0471975511965976;
            const std::array<glm::vec3, 2u> tangents{
                glm::vec3{-1.0f, 0.0f, 0.0f},
                glm::vec3{
                    static_cast<float>(std::cos(angle)), 0.0f,
                    static_cast<float>(std::sin(angle))},
            };
            const std::array<glm::vec3, 2u> normals{
                glm::vec3{0.0f, 0.0f, 1.0f},
                glm::vec3{
                    static_cast<float>(-std::sin(angle)), 0.0f,
                    static_cast<float>(std::cos(angle))},
            };

            Fixture fixture{};
            fixture.Name = "ear";
            fixture.Positions.reserve(112u);
            fixture.Normals.reserve(112u);
            for (std::size_t side = 0u; side < 2u; ++side)
            {
                for (int crease = 0; crease < 7; ++crease)
                {
                    for (int radial = 0; radial < 8; ++radial)
                    {
                        const float distance = 0.065f + 0.095f * radial;
                        const float alongCrease =
                            (static_cast<float>(crease) - 3.0f) * 0.125f;
                        const int noiseCode =
                            (radial * 17 + crease * 11 +
                             static_cast<int>(side) * 3 + 1) % 7 - 3;
                        const float noise = 0.0045f * noiseCode;
                        fixture.Positions.push_back(
                            distance * tangents[side] +
                            glm::vec3{0.0f, alongCrease, 0.0f} +
                            noise * normals[side]);
                        fixture.Normals.push_back(normals[side]);
                    }
                }
            }
            fixture.Params.Method = Consolidation::EarStrategy{
                .NormalSource =
                    Consolidation::NormalSourcePolicy::RequireAuthored,
                .NormalAngleRadians = 3.14159265358979323846 / 12.0,
                .EdgeSensitivity = 5.0,
                .NormalRefinementRounds = 3u,
            };
            fixture.Params.SupportRadius = 0.52;
            fixture.Params.RepulsionWeight = 0.10;
            fixture.Params.MaxIterations = 3u;
            fixture.Params.ConvergenceTolerance = 1.0;
            fixture.Params.TargetPointCount = 120u;
            fixture.Params.Seed = 1904u;
            fixture.Params.MaxInputPointCount = fixture.Positions.size();
            fixture.Params.MaxOutputPointCount = 120u;
            return fixture;
        }

        [[nodiscard]] Consolidation::Result RunReference(
            const Fixture& fixture)
        {
            return fixture.Normals.empty()
                ? Consolidation::Consolidate(
                    fixture.Positions, fixture.Params)
                : Consolidation::Consolidate(
                    fixture.Positions, fixture.Normals, fixture.Params);
        }

        [[nodiscard]] Consolidation::Result RunOptimized(
            const Fixture& fixture)
        {
            return fixture.Normals.empty()
                ? Consolidation::Validation::ConsolidateCpuOptimizedCandidate(
                    fixture.Positions, fixture.Params)
                : Consolidation::Validation::ConsolidateCpuOptimizedCandidate(
                    fixture.Positions, fixture.Normals, fixture.Params);
        }

        template <typename Run>
        [[nodiscard]] TimedResult Measure(Run&& run)
        {
            const auto begin = std::chrono::steady_clock::now();
            Consolidation::Result value = run();
            const auto end = std::chrono::steady_clock::now();
            const auto nanoseconds =
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    end - begin).count();
            return TimedResult{
                .Value = std::move(value),
                .Milliseconds = static_cast<double>(nanoseconds) * 1.0e-6,
            };
        }

        template <std::size_t N>
        [[nodiscard]] double Median(std::array<double, N> values)
        {
            std::ranges::sort(values);
            return values[N / 2u];
        }

        [[nodiscard]] bool SameResult(
            const Consolidation::Result& lhs,
            const Consolidation::Result& rhs) noexcept
        {
            return lhs.State == rhs.State &&
                   lhs.Positions == rhs.Positions &&
                   lhs.Normals == rhs.Normals;
        }

        struct Delta
        {
            double Rms{0.0};
            double Linf{0.0};
        };

        [[nodiscard]] Delta VectorDelta(
            const std::span<const glm::vec3> reference,
            const std::span<const glm::vec3> candidate) noexcept
        {
            if (reference.size() != candidate.size())
                return Delta{1.0e9, 1.0e9};
            if (reference.empty())
                return {};
            double squaredSum = 0.0;
            double maximum = 0.0;
            for (std::size_t i = 0u; i < reference.size(); ++i)
            {
                const glm::dvec3 difference =
                    glm::dvec3(reference[i]) - glm::dvec3(candidate[i]);
                const double magnitudeSquared = glm::dot(
                    difference, difference);
                if (!std::isfinite(magnitudeSquared) ||
                    magnitudeSquared < 0.0)
                {
                    return Delta{1.0e9, 1.0e9};
                }
                squaredSum += magnitudeSquared;
                maximum = std::max(maximum, std::sqrt(magnitudeSquared));
            }
            return Delta{
                std::sqrt(squaredSum /
                    static_cast<double>(reference.size())),
                maximum,
            };
        }

        [[nodiscard]] LopFamilyStrategyComparison Compare(
            const Fixture& fixture)
        {
            LopFamilyStrategyComparison comparison{};
            comparison.Strategy = fixture.Name;
            comparison.InputPointCount = fixture.Positions.size();

            static_cast<void>(RunReference(fixture));
            static_cast<void>(RunOptimized(fixture));

            Consolidation::Result firstReference{};
            Consolidation::Result firstOptimized{};
            bool haveFirst = false;
            comparison.ReferenceDeterministic = true;
            comparison.OptimizedDeterministic = true;
            comparison.ReferenceIdentityMatched = true;
            comparison.OptimizedIdentityMatched = true;
            for (std::size_t pair = 0u;
                 pair < kLopFamilyComparisonMeasuredPairs; ++pair)
            {
                TimedResult reference{};
                TimedResult optimized{};
                if ((pair % 2u) == 0u)
                {
                    reference = Measure([&] { return RunReference(fixture); });
                    optimized = Measure([&] { return RunOptimized(fixture); });
                }
                else
                {
                    optimized = Measure([&] { return RunOptimized(fixture); });
                    reference = Measure([&] { return RunReference(fixture); });
                }
                comparison.ReferenceRuntimeSamplesMilliseconds[pair] =
                    reference.Milliseconds;
                comparison.OptimizedRuntimeSamplesMilliseconds[pair] =
                    optimized.Milliseconds;
                comparison.PairedRuntimeRatios[pair] =
                    reference.Milliseconds > 0.0
                    ? optimized.Milliseconds / reference.Milliseconds
                    : 1.0e9;

                comparison.ReferenceIdentityMatched =
                    comparison.ReferenceIdentityMatched &&
                    reference.Value.Diagnostics.Implementation ==
                        Consolidation::kCpuReferenceImplementation;
                comparison.OptimizedIdentityMatched =
                    comparison.OptimizedIdentityMatched &&
                    optimized.Value.Diagnostics.Implementation ==
                        Consolidation::Validation::
                            kOptimizedCandidateImplementation;

                if (!haveFirst)
                {
                    firstReference = reference.Value;
                    firstOptimized = optimized.Value;
                    haveFirst = true;
                }
                else
                {
                    comparison.ReferenceDeterministic =
                        comparison.ReferenceDeterministic &&
                        SameResult(firstReference, reference.Value);
                    comparison.OptimizedDeterministic =
                        comparison.OptimizedDeterministic &&
                        SameResult(firstOptimized, optimized.Value);
                }
            }

            comparison.ReferenceMedianRuntimeMilliseconds = Median(
                comparison.ReferenceRuntimeSamplesMilliseconds);
            comparison.OptimizedMedianRuntimeMilliseconds = Median(
                comparison.OptimizedRuntimeSamplesMilliseconds);
            comparison.MedianRuntimeRatio = Median(
                comparison.PairedRuntimeRatios);
            comparison.ReferenceStatus = Consolidation::DebugName(
                firstReference.State);
            comparison.OptimizedStatus = Consolidation::DebugName(
                firstOptimized.State);
            comparison.StateMatched =
                firstReference.State == firstOptimized.State;
            comparison.OutputShapeMatched =
                firstReference.Positions.size() ==
                    firstOptimized.Positions.size() &&
                firstReference.Normals.size() ==
                    firstOptimized.Normals.size();
            comparison.OutputPointCount = firstOptimized.Positions.size();
            const Delta position = VectorDelta(
                firstReference.Positions, firstOptimized.Positions);
            const Delta normal = VectorDelta(
                firstReference.Normals, firstOptimized.Normals);
            comparison.PositionRmsDelta = position.Rms;
            comparison.PositionLinfDelta = position.Linf;
            comparison.NormalRmsDelta = normal.Rms;
            comparison.NormalLinfDelta = normal.Linf;
            const bool exactEmptyFailure = comparison.StateMatched &&
                firstReference.State != Consolidation::Status::Success &&
                firstReference.State != Consolidation::Status::NotConverged &&
                firstReference.Positions.empty() &&
                firstReference.Normals.empty() &&
                firstOptimized.Positions.empty() &&
                firstOptimized.Normals.empty();
            comparison.ParityPassed = comparison.StateMatched &&
                comparison.OutputShapeMatched &&
                (comparison.OutputPointCount > 0u || exactEmptyFailure) &&
                comparison.ReferenceIdentityMatched &&
                comparison.OptimizedIdentityMatched &&
                !comparison.OptimizedUsedFallback &&
                comparison.ReferenceDeterministic &&
                comparison.OptimizedDeterministic &&
                comparison.PositionRmsDelta <= kLopFamilyRmsDeltaMax &&
                comparison.PositionLinfDelta <= kLopFamilyLinfDeltaMax &&
                comparison.NormalRmsDelta <= kLopFamilyRmsDeltaMax &&
                comparison.NormalLinfDelta <= kLopFamilyLinfDeltaMax;
            comparison.AccelerationPassed = comparison.ParityPassed &&
                comparison.MedianRuntimeRatio <=
                    kLopFamilyUsefulRuntimeRatioMax;
            comparison.Adopted = comparison.AccelerationPassed;
            return comparison;
        }
    }

    LopFamilyComparisonMetrics RunLopFamilyComparisonSmoke()
    {
        const std::array<Fixture, 4u> fixtures{
            LopFixture(), WlopFixture(), ClopFixture(), EarFixture()};
        LopFamilyComparisonMetrics metrics{};
        metrics.Succeeded = true;
        for (std::size_t i = 0u; i < fixtures.size(); ++i)
        {
            metrics.Strategies[i] = Compare(fixtures[i]);
            const auto& comparison = metrics.Strategies[i];
            ++metrics.EvaluatedStrategyCount;
            metrics.AdoptedStrategyCount += comparison.Adopted ? 1u : 0u;
            metrics.RuntimeMilliseconds +=
                comparison.OptimizedMedianRuntimeMilliseconds;
            metrics.QualityErrorL2 = std::max(
                metrics.QualityErrorL2,
                std::max(
                    comparison.PositionRmsDelta,
                    comparison.NormalRmsDelta));
            metrics.Succeeded = metrics.Succeeded &&
                comparison.ParityPassed;
        }
        return metrics;
    }
}

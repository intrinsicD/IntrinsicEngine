// METHOD-023 — manifest-backed Boundary First Flattening reference smoke.

#include "Bench.BoundaryFirstFlatteningReferenceSmoke.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string_view>

#include <glm/glm.hpp>

import Geometry.HalfedgeMesh;
import Geometry.Parameterization.Bff;
import Geometry.Properties;

namespace Intrinsic::Bench::Geometry
{
    namespace
    {
        constexpr std::size_t kGridSideVertexCount = 3u;
        constexpr std::size_t kExpectedVertexCount =
            kGridSideVertexCount * kGridSideVertexCount;
        constexpr std::size_t kExpectedFaceCount =
            2u * (kGridSideVertexCount - 1u) * (kGridSideVertexCount - 1u);

        [[nodiscard]] ::Geometry::HalfedgeMesh::Mesh MakeCurvedGridDisk()
        {
            using ::Geometry::VertexHandle;

            ::Geometry::HalfedgeMesh::Mesh mesh;
            std::array<VertexHandle, kExpectedVertexCount> vertices{};
            for (std::size_t row = 0u; row < kGridSideVertexCount; ++row)
            {
                for (std::size_t column = 0u;
                     column < kGridSideVertexCount;
                     ++column)
                {
                    const float x = -1.0f + static_cast<float>(column);
                    const float y = -1.0f + static_cast<float>(row);
                    const float z = 0.35f * (1.0f - x * x) * (1.0f - y * y);
                    vertices[row * kGridSideVertexCount + column] =
                        mesh.AddVertex(glm::vec3{x, y, z});
                }
            }

            const auto vertexAt =
                [&vertices](const std::size_t row,
                            const std::size_t column) -> VertexHandle
                {
                    return vertices[row * kGridSideVertexCount + column];
                };
            for (std::size_t row = 0u;
                 row + 1u < kGridSideVertexCount;
                 ++row)
            {
                for (std::size_t column = 0u;
                     column + 1u < kGridSideVertexCount;
                     ++column)
                {
                    const VertexHandle v00 = vertexAt(row, column);
                    const VertexHandle v10 = vertexAt(row, column + 1u);
                    const VertexHandle v11 = vertexAt(row + 1u, column + 1u);
                    const VertexHandle v01 = vertexAt(row + 1u, column);
                    (void)mesh.AddTriangle(v00, v10, v11);
                    (void)mesh.AddTriangle(v00, v11, v01);
                }
            }
            return mesh;
        }

        [[nodiscard]] bool IsFiniteUv(const glm::vec2 uv) noexcept
        {
            return std::isfinite(uv.x) && std::isfinite(uv.y);
        }

        struct TickResult
        {
            double QualityErrorL2{
                kBoundaryFirstFlatteningReferenceSmokeQualityErrorL2Max + 1.0};
            double MeanConformalDistortion{
                1.0 + kBoundaryFirstFlatteningReferenceSmokeQualityErrorL2Max};
            double MaxConformalDistortion{
                1.0 + kBoundaryFirstFlatteningReferenceSmokeQualityErrorL2Max};
            double ClosureAdjustmentRmsRelative{0.0};
            double ClosureAdjustmentMaxRelative{0.0};
            std::size_t VertexCount{0u};
            std::size_t FaceCount{0u};
            std::size_t EvaluatedFaceCount{0u};
            std::size_t FlippedElementCount{0u};
            std::string_view FailureReason{"not_run"};
            bool Succeeded{false};
        };

        [[nodiscard]] TickResult Tick()
        {
            const ::Geometry::HalfedgeMesh::Mesh mesh = MakeCurvedGridDisk();
            const ::Geometry::Parameterization::BffParams params{
                .Mode = ::Geometry::Parameterization::BffBoundaryMode::AutomaticConformal,
            };
            const ::Geometry::Parameterization::BffResult result =
                ::Geometry::Parameterization::ComputeBFF(mesh, params);

            TickResult tick{};
            tick.VertexCount = mesh.VertexCount();
            tick.FaceCount = mesh.FaceCount();
            tick.FailureReason = ::Geometry::Parameterization::ToString(
                result.Status);
            if (!result.Succeeded())
                return tick;

            const auto& quality = result.Diagnostics.Quality;
            const bool finiteQuality =
                std::isfinite(quality.RootMeanSquareConformalError)
                && std::isfinite(quality.MeanConformalDistortion)
                && std::isfinite(quality.MaxConformalDistortion);
            if (finiteQuality)
            {
                tick.QualityErrorL2 =
                    quality.RootMeanSquareConformalError;
                tick.MeanConformalDistortion =
                    quality.MeanConformalDistortion;
                tick.MaxConformalDistortion =
                    quality.MaxConformalDistortion;
            }
            tick.EvaluatedFaceCount = quality.EvaluatedFaceCount;
            tick.FlippedElementCount = quality.FlippedElementCount;
            tick.ClosureAdjustmentRmsRelative =
                result.Diagnostics.ClosureAdjustmentRmsRelative;
            tick.ClosureAdjustmentMaxRelative =
                result.Diagnostics.ClosureAdjustmentMaxRelative;

            const bool finiteUvs = std::all_of(
                result.UVs.begin(),
                result.UVs.end(),
                IsFiniteUv);
            tick.Succeeded =
                result.Diagnostics.Backend == "cpu_reference"
                && tick.VertexCount == kExpectedVertexCount
                && tick.FaceCount == kExpectedFaceCount
                && result.UVs.size() == mesh.VerticesSize()
                && finiteUvs
                && quality.HasUsableFaces()
                && !quality.HasInvalidInput()
                && tick.EvaluatedFaceCount == tick.FaceCount
                && tick.FlippedElementCount == 0u
                && finiteQuality
                && tick.QualityErrorL2
                    <= kBoundaryFirstFlatteningReferenceSmokeQualityErrorL2Max;
            tick.FailureReason = tick.Succeeded
                ? std::string_view{"none"}
                : std::string_view{"quality_contract_failed"};
            return tick;
        }
    }

    BoundaryFirstFlatteningReferenceSmokeMetrics
    RunBoundaryFirstFlatteningReferenceSmoke()
    {
        for (std::size_t iteration = 0u;
             iteration < kBoundaryFirstFlatteningReferenceSmokeWarmupIterations;
             ++iteration)
        {
            (void)Tick();
        }

        TickResult last{};
        bool allMeasuredIterationsSucceeded = true;
        std::size_t failedMeasuredIterationCount = 0u;
        double worstQualityErrorL2 = 0.0;
        double worstMeanConformalDistortion = 0.0;
        double worstMaxConformalDistortion = 0.0;
        double worstClosureAdjustmentRmsRelative = 0.0;
        double worstClosureAdjustmentMaxRelative = 0.0;
        std::size_t minimumEvaluatedFaceCount =
            std::numeric_limits<std::size_t>::max();
        std::size_t maximumFlippedElementCount = 0u;
        std::string_view firstFailureReason{"none"};

        const auto start = std::chrono::steady_clock::now();
        for (std::size_t iteration = 0u;
             iteration < kBoundaryFirstFlatteningReferenceSmokeMeasuredIterations;
             ++iteration)
        {
            last = Tick();
            allMeasuredIterationsSucceeded =
                allMeasuredIterationsSucceeded && last.Succeeded;
            if (!last.Succeeded)
            {
                ++failedMeasuredIterationCount;
                if (firstFailureReason == "none")
                    firstFailureReason = last.FailureReason;
            }
            worstQualityErrorL2 =
                std::max(worstQualityErrorL2, last.QualityErrorL2);
            worstMeanConformalDistortion = std::max(
                worstMeanConformalDistortion,
                last.MeanConformalDistortion);
            worstMaxConformalDistortion = std::max(
                worstMaxConformalDistortion,
                last.MaxConformalDistortion);
            worstClosureAdjustmentRmsRelative = std::max(
                worstClosureAdjustmentRmsRelative,
                last.ClosureAdjustmentRmsRelative);
            worstClosureAdjustmentMaxRelative = std::max(
                worstClosureAdjustmentMaxRelative,
                last.ClosureAdjustmentMaxRelative);
            minimumEvaluatedFaceCount = std::min(
                minimumEvaluatedFaceCount,
                last.EvaluatedFaceCount);
            maximumFlippedElementCount = std::max(
                maximumFlippedElementCount,
                last.FlippedElementCount);
        }
        const auto end = std::chrono::steady_clock::now();

        const auto totalNanoseconds =
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
                .count();
        const double meanMilliseconds =
            (static_cast<double>(totalNanoseconds)
             / static_cast<double>(
                 kBoundaryFirstFlatteningReferenceSmokeMeasuredIterations))
            * 1.0e-6;

        if (firstFailureReason == "none")
        {
            if (!std::isfinite(meanMilliseconds))
                firstFailureReason = "runtime_non_finite";
            else if (meanMilliseconds
                     > kBoundaryFirstFlatteningReferenceSmokeRuntimeMillisecondsMax)
                firstFailureReason = "runtime_budget_exceeded";
            else if (!std::isfinite(worstQualityErrorL2))
                firstFailureReason = "aggregate_quality_non_finite";
        }

        BoundaryFirstFlatteningReferenceSmokeMetrics metrics{};
        metrics.RuntimeMilliseconds = meanMilliseconds;
        metrics.QualityErrorL2 = worstQualityErrorL2;
        metrics.MeanConformalDistortion = worstMeanConformalDistortion;
        metrics.MaxConformalDistortion = worstMaxConformalDistortion;
        metrics.ClosureAdjustmentRmsRelative =
            worstClosureAdjustmentRmsRelative;
        metrics.ClosureAdjustmentMaxRelative =
            worstClosureAdjustmentMaxRelative;
        metrics.VertexCount = last.VertexCount;
        metrics.FaceCount = last.FaceCount;
        metrics.EvaluatedFaceCount = minimumEvaluatedFaceCount;
        metrics.FlippedElementCount = maximumFlippedElementCount;
        metrics.FailedMeasuredIterationCount = failedMeasuredIterationCount;
        metrics.FailureReason = firstFailureReason;
        metrics.Succeeded = allMeasuredIterationsSucceeded
            && failedMeasuredIterationCount == 0u
            && std::isfinite(meanMilliseconds)
            && meanMilliseconds
                <= kBoundaryFirstFlatteningReferenceSmokeRuntimeMillisecondsMax
            && std::isfinite(worstQualityErrorL2)
            && worstQualityErrorL2
                <= kBoundaryFirstFlatteningReferenceSmokeQualityErrorL2Max;
        return metrics;
    }
}

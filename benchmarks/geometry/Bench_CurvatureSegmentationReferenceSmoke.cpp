// METHOD-037 — deterministic signed-curvature segmentation correctness smoke.

#include "Bench.CurvatureSegmentationReferenceSmoke.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.CurvatureSegmentation;
import Geometry.Properties;

namespace Intrinsic::Bench::Geometry
{
    namespace
    {
        namespace Segment = ::Geometry::CurvatureSegmentation;

        constexpr int kWarmupIterations = 1;
        constexpr int kMeasuredIterations = 8;
        constexpr std::uint32_t kSegments = 4u;

        struct FoldFixture
        {
            ::Geometry::HalfedgeMesh::Mesh Mesh{};
            std::vector<double> K1{};
            std::vector<double> K2{};
            std::vector<::Geometry::EdgeHandle> FoldEdges{};
        };

        [[nodiscard]] FoldFixture MakeFoldFixture()
        {
            FoldFixture fixture{};
            std::vector<::Geometry::VertexHandle> left{};
            std::vector<::Geometry::VertexHandle> fold{};
            std::vector<::Geometry::VertexHandle> right{};
            left.reserve(kSegments + 1u);
            fold.reserve(kSegments + 1u);
            right.reserve(kSegments + 1u);
            fixture.K1.reserve(3u * (kSegments + 1u));
            fixture.K2.reserve(3u * (kSegments + 1u));

            for (std::uint32_t row = 0u; row <= kSegments; ++row)
            {
                const float y = static_cast<float>(row);
                left.push_back(
                    fixture.Mesh.AddVertex({-1.0f, y, 0.0f}));
                fold.push_back(
                    fixture.Mesh.AddVertex({0.0f, y, 0.0f}));
                right.push_back(
                    fixture.Mesh.AddVertex({1.0f, y, 1.0f}));
                const double level = static_cast<double>(row);
                fixture.K1.insert(
                    fixture.K1.end(),
                    {-1.0 + 0.03 * level,
                     0.02 * level * level,
                     1.0 - 0.01 * level});
                fixture.K2.insert(
                    fixture.K2.end(),
                    {-2.0 + 0.015 * level * level,
                     -0.025 * level,
                     2.0 + 0.02 * level});
            }
            for (std::uint32_t row = 0u; row < kSegments; ++row)
            {
                (void)fixture.Mesh.AddTriangle(
                    left[row], fold[row], left[row + 1u]);
                (void)fixture.Mesh.AddTriangle(
                    fold[row], fold[row + 1u], left[row + 1u]);
                (void)fixture.Mesh.AddTriangle(
                    fold[row], right[row], fold[row + 1u]);
                (void)fixture.Mesh.AddTriangle(
                    right[row], right[row + 1u], fold[row + 1u]);
                const auto edge = fixture.Mesh.FindEdge(
                    fold[row], fold[row + 1u]);
                if (edge.has_value())
                    fixture.FoldEdges.push_back(*edge);
            }
            return fixture;
        }

        [[nodiscard]] double RegimeMisclassificationFraction(
            const std::vector<std::uint32_t>& labels)
        {
            std::size_t directCorrect = 0u;
            std::size_t swappedCorrect = 0u;
            for (std::size_t face = 0u; face < labels.size(); ++face)
            {
                const std::uint32_t expected =
                    face % 4u < 2u ? 0u : 1u;
                directCorrect += labels[face] == expected ? 1u : 0u;
                swappedCorrect += labels[face] == 1u - expected ? 1u : 0u;
            }
            const std::size_t correct =
                std::max(directCorrect, swappedCorrect);
            return labels.empty()
                ? 1.0
                : 1.0 - static_cast<double>(correct) /
                            static_cast<double>(labels.size());
        }

        [[nodiscard]] CurvatureSegmentationReferenceSmokeMetrics Tick()
        {
            FoldFixture fixture = MakeFoldFixture();
            Segment::CurvatureSegmentationParams params{};
            params.SelectionMode =
                Segment::ComponentSelectionMode::FixedCount;
            params.FixedComponentCount = 2u;
            params.SpatialWeight = 40.0;
            params.FeatureSensitivity = 20.0;
            params.MaxSpatialIterations = 20u;
            params.MinimumRegionFaces = 1u;
            params.Seed = 17u;

            const Segment::CurvatureSegmentationResult result =
                Segment::Segment(
                    fixture.Mesh, fixture.K1, fixture.K2, params);
            CurvatureSegmentationReferenceSmokeMetrics metrics{};
            if (!result.Succeeded())
                return metrics;

            metrics.RegimeMisclassificationFraction =
                RegimeMisclassificationFraction(
                    result.FaceComponents);
            const std::size_t recoveredFoldEdges = std::count_if(
                fixture.FoldEdges.begin(),
                fixture.FoldEdges.end(),
                [&result](const ::Geometry::EdgeHandle edge)
                {
                    return edge.Index < result.EdgeBoundaries.size() &&
                           result.EdgeBoundaries[edge.Index] != 0u;
                });
            metrics.FoldBoundaryRecall = fixture.FoldEdges.empty()
                ? 0.0
                : static_cast<double>(recoveredFoldEdges) /
                      static_cast<double>(fixture.FoldEdges.size());
            metrics.SelectedComponentCount =
                result.Diagnostics.SelectedComponentCount;
            metrics.ConnectedRegionCount =
                result.Diagnostics.ConnectedRegionCount;
            metrics.BoundaryEdgeCount = static_cast<std::uint32_t>(
                result.Diagnostics.BoundaryEdgeCount);
            metrics.GmmIterations = result.Diagnostics.GmmIterations;
            metrics.SpatialIterations =
                result.Diagnostics.SpatialIterations;
            const auto& timings = result.Diagnostics.Timings;
            metrics.FaceAggregationMilliseconds =
                timings.FaceAggregationAndNormalizationMilliseconds;
            metrics.GmmFittingMilliseconds =
                timings.GmmFittingMilliseconds;
            metrics.UnaryConstructionMilliseconds =
                timings.UnaryConstructionMilliseconds;
            metrics.DualGraphConstructionMilliseconds =
                timings.DualGraphConstructionMilliseconds;
            metrics.SpatialOptimizationMilliseconds =
                timings.SpatialOptimizationMilliseconds;
            metrics.ConnectivityPublicationMilliseconds =
                timings.ConnectivityCleanupAndPublicationMilliseconds;
            metrics.SegmentationTotalMilliseconds =
                timings.TotalMilliseconds;
            metrics.Succeeded =
                metrics.SelectedComponentCount == 2u &&
                metrics.ConnectedRegionCount == 2u &&
                metrics.RegimeMisclassificationFraction <= 0.05 &&
                metrics.FoldBoundaryRecall >= 0.99 &&
                result.Diagnostics.FinalEnergy <=
                    result.Diagnostics.InitialEnergy + 1.0e-10;
            return metrics;
        }
    }

    CurvatureSegmentationReferenceSmokeMetrics
    RunCurvatureSegmentationReferenceSmoke()
    {
        for (int i = 0; i < kWarmupIterations; ++i)
            static_cast<void>(Tick());

        CurvatureSegmentationReferenceSmokeMetrics last{};
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

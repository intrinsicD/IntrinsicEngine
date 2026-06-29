// METHOD-002 — signed heat CPU reference smoke benchmark workload.
//
// The smoke workload mirrors the flat-grid analytic case from the correctness
// tests and reports both runtime and signed-distance L2 error. No performance
// win is claimed; this is a PR-fast contract check for the reference backend.

#include "Bench.SignedHeatReferenceSmoke.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <vector>

#include <glm/glm.hpp>

import Geometry;

namespace Intrinsic::Bench::Geometry
{
    namespace
    {
        constexpr int kWarmupIterations = 1;
        constexpr int kMeasuredIterations = 8;
        constexpr int kGridColumns = 8;
        constexpr double kQualityErrorMax = 0.40;

        struct GridMesh
        {
            ::Geometry::HalfedgeMesh::Mesh Mesh;
            std::vector<::Geometry::VertexHandle> Vertices;
            int Columns{0};
            float Step{0.0f};

            [[nodiscard]] ::Geometry::VertexHandle At(int x, int y) const
            {
                return Vertices[static_cast<std::size_t>(y * (Columns + 1) + x)];
            }
        };

        [[nodiscard]] GridMesh MakeFlatGrid(int columns)
        {
            GridMesh grid;
            grid.Columns = columns;
            grid.Step = 2.0f / static_cast<float>(columns);
            grid.Vertices.reserve(static_cast<std::size_t>((columns + 1) * (columns + 1)));

            for (int y = 0; y <= columns; ++y)
            {
                for (int x = 0; x <= columns; ++x)
                {
                    const float px = -1.0f + grid.Step * static_cast<float>(x);
                    const float py = -1.0f + grid.Step * static_cast<float>(y);
                    grid.Vertices.push_back(grid.Mesh.AddVertex({px, py, 0.0f}));
                }
            }

            for (int y = 0; y < columns; ++y)
            {
                for (int x = 0; x < columns; ++x)
                {
                    const auto v00 = grid.At(x, y);
                    const auto v10 = grid.At(x + 1, y);
                    const auto v01 = grid.At(x, y + 1);
                    const auto v11 = grid.At(x + 1, y + 1);
                    (void)grid.Mesh.AddTriangle(v00, v10, v11);
                    (void)grid.Mesh.AddTriangle(v00, v11, v01);
                }
            }

            return grid;
        }

        [[nodiscard]] std::vector<::Geometry::HalfedgeHandle> MakeSquareBoundary(const GridMesh& grid)
        {
            constexpr int lo = 2;
            constexpr int hi = 6;
            std::vector<::Geometry::VertexHandle> loop;
            for (int x = lo; x < hi; ++x) loop.push_back(grid.At(x, lo));
            for (int y = lo; y < hi; ++y) loop.push_back(grid.At(hi, y));
            for (int x = hi; x > lo; --x) loop.push_back(grid.At(x, hi));
            for (int y = hi; y > lo; --y) loop.push_back(grid.At(lo, y));

            std::vector<::Geometry::HalfedgeHandle> boundary;
            boundary.reserve(loop.size());
            for (std::size_t i = 0; i < loop.size(); ++i)
            {
                const ::Geometry::VertexHandle from = loop[i];
                const ::Geometry::VertexHandle to = loop[(i + 1) % loop.size()];
                auto h = grid.Mesh.FindHalfedge(from, to);
                if (h.has_value())
                {
                    boundary.push_back(*h);
                }
            }
            return boundary;
        }

        [[nodiscard]] double ExpectedSquareSdfInsidePositive(const glm::vec3& p)
        {
            constexpr double halfExtent = 0.5;
            const double ax = std::abs(static_cast<double>(p.x));
            const double ay = std::abs(static_cast<double>(p.y));
            const double outsideX = std::max(ax - halfExtent, 0.0);
            const double outsideY = std::max(ay - halfExtent, 0.0);
            const double outsideDistance = std::sqrt(outsideX * outsideX + outsideY * outsideY);
            if (ax <= halfExtent && ay <= halfExtent)
            {
                return halfExtent - std::max(ax, ay);
            }
            return -outsideDistance;
        }

        [[nodiscard]] double RootMeanSquareError(
            const ::Geometry::HalfedgeMesh::Mesh& mesh,
            const ::Geometry::VertexProperty<double>& values)
        {
            double sumSq = 0.0;
            std::size_t count = 0;
            for (std::size_t vi = 0; vi < mesh.VerticesSize(); ++vi)
            {
                const ::Geometry::VertexHandle v{static_cast<::Geometry::PropertyIndex>(vi)};
                if (mesh.IsDeleted(v) || mesh.IsIsolated(v))
                {
                    continue;
                }
                const double expected = ExpectedSquareSdfInsidePositive(mesh.Position(v));
                const double actual = values[v];
                sumSq += (actual - expected) * (actual - expected);
                ++count;
            }
            return count > 0 ? std::sqrt(sumSq / static_cast<double>(count)) : 0.0;
        }

        [[nodiscard]] SignedHeatReferenceSmokeMetrics Tick()
        {
            GridMesh grid = MakeFlatGrid(kGridColumns);
            const std::vector<::Geometry::HalfedgeHandle> boundary = MakeSquareBoundary(grid);
            const ::Geometry::SignedHeatMethod::SignedHeatResult result =
                ::Geometry::SignedHeatMethod::ComputeSignedDistance(grid.Mesh, boundary);

            SignedHeatReferenceSmokeMetrics metrics{};
            metrics.QualityErrorL2 = RootMeanSquareError(grid.Mesh, result.SignedDistanceProperty);
            metrics.MaxAbsDistance = result.Diagnostics.MaxAbsDistance;
            metrics.MeanBoundaryOffset = result.Diagnostics.MeanBoundaryOffset;
            metrics.SourceVertexCount = static_cast<std::uint32_t>(result.Diagnostics.SourceVertexCount);
            metrics.DegenerateBoundaryVertexCount =
                static_cast<std::uint32_t>(result.Diagnostics.DegenerateBoundaryVertexCount);

            const ::Geometry::VertexHandle center = grid.At(4, 4);
            const ::Geometry::VertexHandle outside = grid.At(0, 0);
            metrics.Succeeded = boundary.size() == 16u
                && result.Diagnostics.Status == ::Geometry::SignedHeatMethod::SignedHeatStatus::Success
                && result.Diagnostics.SourceVertexCount == 16u
                && result.Diagnostics.DegenerateBoundaryVertexCount == 0u
                && result.SignedDistanceProperty[center] > 0.05
                && result.SignedDistanceProperty[outside] < -0.05
                && metrics.QualityErrorL2 < kQualityErrorMax;
            return metrics;
        }
    } // namespace

    SignedHeatReferenceSmokeMetrics RunSignedHeatReferenceSmoke()
    {
        for (int i = 0; i < kWarmupIterations; ++i)
        {
            (void)Tick();
        }

        SignedHeatReferenceSmokeMetrics last{};
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < kMeasuredIterations; ++i)
        {
            last = Tick();
        }
        const auto t1 = std::chrono::steady_clock::now();

        const auto totalNs = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        last.RuntimeMilliseconds =
            (static_cast<double>(totalNs) / static_cast<double>(kMeasuredIterations)) * 1.0e-6;
        return last;
    }
} // namespace Intrinsic::Bench::Geometry

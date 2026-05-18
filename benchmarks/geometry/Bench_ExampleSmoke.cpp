// GEOM-009 — geometry smoke benchmark workload definition.
//
// Replaces the original placeholder translation unit with a deterministic
// smoke benchmark over the public Geometry::HalfedgeMesh + Geometry::MeshQuality
// API surface. The companion manifest lives at
// benchmarks/geometry/manifests/geometry_halfedge_smoke.yaml; the runner that
// emits the result JSON lives at benchmarks/runners/BenchmarkSmokeRunner.cpp.

#include "Bench.GeometrySmoke.hpp"

#include <chrono>

import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.Builder;
import Geometry.HalfedgeMesh.Quality;

namespace Intrinsic::Bench::Geometry
{
    namespace
    {
        constexpr int kWarmupIterations   = 1;
        constexpr int kMeasuredIterations = 8;

        struct TickResult
        {
            double      TotalArea{0.0};
            std::size_t VertexCount{0};
            std::size_t FaceCount{0};
            bool        Succeeded{false};
        };

        [[nodiscard]] auto Tick() -> TickResult
        {
            const auto mesh    = ::Geometry::HalfedgeMesh::MakeMeshIcosahedron();
            const auto quality = ::Geometry::MeshQuality::ComputeQuality(mesh);

            TickResult result{};
            result.VertexCount = mesh.VertexCount();
            result.FaceCount   = mesh.FaceCount();
            if (quality.has_value())
            {
                result.TotalArea = quality->TotalArea;
                result.Succeeded = true;
            }
            return result;
        }
    } // namespace

    auto RunHalfedgeSmoke() -> HalfedgeSmokeMetrics
    {
        // Warmup keeps first-iteration allocator/JIT noise out of the reported
        // runtime without affecting the deterministic geometric outputs.
        for (int i = 0; i < kWarmupIterations; ++i)
        {
            (void)Tick();
        }

        TickResult last{};
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < kMeasuredIterations; ++i)
        {
            last = Tick();
        }
        const auto t1 = std::chrono::steady_clock::now();

        const auto   totalNs = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        const double meanMs  = (static_cast<double>(totalNs) / static_cast<double>(kMeasuredIterations)) * 1.0e-6;

        HalfedgeSmokeMetrics metrics{};
        metrics.RuntimeMilliseconds = meanMs;
        metrics.TotalArea           = last.TotalArea;
        metrics.VertexCount         = last.VertexCount;
        metrics.FaceCount           = last.FaceCount;
        metrics.Succeeded           = last.Succeeded;
        return metrics;
    }
} // namespace Intrinsic::Bench::Geometry

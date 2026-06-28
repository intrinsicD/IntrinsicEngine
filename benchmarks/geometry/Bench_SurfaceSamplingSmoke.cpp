// GEOM-035 — deterministic surface-sampling smoke benchmark.

#include "Bench.SurfaceSamplingSmoke.hpp"

#include <chrono>
#include <cmath>
#include <cstddef>

#include <glm/glm.hpp>

import Geometry.HalfedgeMesh;
import Geometry.PointCloud.SurfaceSampling;

namespace Intrinsic::Bench::Geometry
{
    namespace
    {
        constexpr int kWarmupIterations = 1;
        constexpr int kMeasuredIterations = 8;
        constexpr std::int64_t kSampleCount = 2048;
        constexpr double kExpectedSmallTriangleFraction = 0.2;

        [[nodiscard]] ::Geometry::HalfedgeMesh::Mesh MakeAreaRatioFixture()
        {
            ::Geometry::HalfedgeMesh::Mesh mesh;

            const auto s0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
            const auto s1 = mesh.AddVertex({1.0f, 0.0f, 0.0f});
            const auto s2 = mesh.AddVertex({0.0f, 1.0f, 0.0f});
            (void)mesh.AddTriangle(s0, s1, s2);

            const auto b0 = mesh.AddVertex({10.0f, 0.0f, 0.0f});
            const auto b1 = mesh.AddVertex({12.0f, 0.0f, 0.0f});
            const auto b2 = mesh.AddVertex({10.0f, 2.0f, 0.0f});
            (void)mesh.AddTriangle(b0, b1, b2);

            return mesh;
        }

        struct TickResult
        {
            double QualityErrorL2{0.0};
            double SmallTriangleFraction{0.0};
            std::size_t WrittenSampleCount{0};
            std::size_t AcceptedTriangleCount{0};
            bool Succeeded{false};
        };

        [[nodiscard]] TickResult Tick()
        {
            const auto mesh = MakeAreaRatioFixture();

            ::Geometry::PointCloud::SurfaceSampling::Params params;
            params.SampleCount = kSampleCount;
            params.Seed = 0x35035u;

            const auto result = ::Geometry::PointCloud::SurfaceSampling::SampleTriangleMeshSurface(mesh, params);

            std::size_t smallTriangleSamples = 0;
            for (const auto point : result.Cloud.LivePoints())
            {
                if (result.Cloud.Position(point).x < 5.0f)
                {
                    ++smallTriangleSamples;
                }
            }

            TickResult tick;
            tick.WrittenSampleCount = result.Info.WrittenSampleCount;
            tick.AcceptedTriangleCount = result.Info.AcceptedTriangleCount;
            if (result.Info.WrittenSampleCount > 0u)
            {
                tick.SmallTriangleFraction =
                    static_cast<double>(smallTriangleSamples) / static_cast<double>(result.Info.WrittenSampleCount);
            }
            tick.QualityErrorL2 = std::abs(tick.SmallTriangleFraction - kExpectedSmallTriangleFraction);
            tick.Succeeded = result.Succeeded()
                && result.Info.WrittenSampleCount == static_cast<std::size_t>(kSampleCount)
                && result.Info.AcceptedTriangleCount == 2u
                && tick.QualityErrorL2 <= 0.08;
            return tick;
        }
    }

    SurfaceSamplingSmokeMetrics RunSurfaceSamplingSmoke()
    {
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

        const auto totalNs = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        const double meanMs = (static_cast<double>(totalNs) / static_cast<double>(kMeasuredIterations)) * 1.0e-6;

        SurfaceSamplingSmokeMetrics metrics{};
        metrics.RuntimeMilliseconds = meanMs;
        metrics.QualityErrorL2 = last.QualityErrorL2;
        metrics.SmallTriangleFraction = last.SmallTriangleFraction;
        metrics.ExpectedSmallTriangleFraction = kExpectedSmallTriangleFraction;
        metrics.WrittenSampleCount = last.WrittenSampleCount;
        metrics.AcceptedTriangleCount = last.AcceptedTriangleCount;
        metrics.Succeeded = last.Succeeded;
        return metrics;
    }
}

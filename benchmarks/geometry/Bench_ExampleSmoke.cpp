// GEOM-009 — geometry smoke benchmark workload definition.
//
// Replaces the original placeholder translation unit with a deterministic
// smoke benchmark over the public Geometry::HalfedgeMesh + Geometry::MeshQuality
// API surface. The companion manifest lives at
// benchmarks/geometry/manifests/geometry_halfedge_smoke.yaml; the runner that
// emits the result JSON lives at benchmarks/runners/BenchmarkSmokeRunner.cpp.

#include "Bench.GeometrySmoke.hpp"

#include <chrono>
#include <vector>

#include <glm/glm.hpp>

import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.Builder;
import Geometry.HalfedgeMesh.Quality;
import Geometry.Parameterization.Diagnostics;

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

        struct DiagnosticsTickResult
        {
            double      MeanConformalDistortion{0.0};
            double      MaxAreaDistortion{0.0};
            double      MeanStretch{0.0};
            std::size_t EvaluatedFaceCount{0};
            std::size_t FlippedElementCount{0};
            bool        Succeeded{false};
        };

        [[nodiscard]] auto MakeSquareDisk() -> ::Geometry::HalfedgeMesh::Mesh
        {
            ::Geometry::HalfedgeMesh::Mesh mesh;
            const auto v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
            const auto v1 = mesh.AddVertex({1.0f, 0.0f, 0.0f});
            const auto v2 = mesh.AddVertex({1.0f, 1.0f, 0.0f});
            const auto v3 = mesh.AddVertex({0.0f, 1.0f, 0.0f});
            (void)mesh.AddTriangle(v0, v1, v2);
            (void)mesh.AddTriangle(v0, v2, v3);
            return mesh;
        }

        [[nodiscard]] auto DiagnosticsTick() -> DiagnosticsTickResult
        {
            const auto mesh = MakeSquareDisk();
            const std::vector<glm::vec2> uvs{
                glm::vec2{0.0f, 0.0f},
                glm::vec2{2.0f, 0.0f},
                glm::vec2{2.0f, 1.0f},
                glm::vec2{0.0f, 1.0f},
            };

            const auto diagnostics = ::Geometry::Parameterization::EvaluateParameterizationDiagnostics(mesh, uvs);

            DiagnosticsTickResult result{};
            result.MeanConformalDistortion = diagnostics.MeanConformalDistortion;
            result.MaxAreaDistortion = diagnostics.MaxAreaDistortion;
            result.MeanStretch = diagnostics.MeanStretch;
            result.EvaluatedFaceCount = diagnostics.EvaluatedFaceCount;
            result.FlippedElementCount = diagnostics.FlippedElementCount;
            result.Succeeded = diagnostics.Status == ::Geometry::Parameterization::ParameterizationDiagnosticsStatus::Success
                && diagnostics.EvaluatedFaceCount == 2u
                && diagnostics.FlippedElementCount == 0u;
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

    auto RunParameterizationDiagnosticsSmoke() -> ParameterizationDiagnosticsSmokeMetrics
    {
        for (int i = 0; i < kWarmupIterations; ++i)
        {
            (void)DiagnosticsTick();
        }

        DiagnosticsTickResult last{};
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < kMeasuredIterations; ++i)
        {
            last = DiagnosticsTick();
        }
        const auto t1 = std::chrono::steady_clock::now();

        const auto   totalNs = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        const double meanMs  = (static_cast<double>(totalNs) / static_cast<double>(kMeasuredIterations)) * 1.0e-6;

        ParameterizationDiagnosticsSmokeMetrics metrics{};
        metrics.RuntimeMilliseconds = meanMs;
        metrics.MeanConformalDistortion = last.MeanConformalDistortion;
        metrics.MaxAreaDistortion = last.MaxAreaDistortion;
        metrics.MeanStretch = last.MeanStretch;
        metrics.EvaluatedFaceCount = last.EvaluatedFaceCount;
        metrics.FlippedElementCount = last.FlippedElementCount;
        metrics.Succeeded = last.Succeeded;
        return metrics;
    }
} // namespace Intrinsic::Bench::Geometry

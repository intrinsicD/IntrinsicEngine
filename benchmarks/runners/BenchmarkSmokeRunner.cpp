// GEOM-009 — geometry smoke benchmark runner.
//
// Invokes the registered geometry smoke workloads and emits result JSON
// matching docs/benchmarking/result-json-schema.md (validated by
// tools/benchmark/validate_benchmark_results.py).
//
// Usage:
//   IntrinsicBenchmarkSmoke <output_path>
//
// `<output_path>` may be a directory (one JSON per benchmark is written
// inside it) or a file path (the first benchmark is written to that exact
// path; any additional benchmarks are written as siblings keyed by
// benchmark_id). The single-file form preserves backwards compatibility
// with the previous scaffold's CMake/CI wiring.

#include "../geometry/Bench.GeometrySmoke.hpp"
#include "../geometry/Bench.ProgressivePoissonReferenceSmoke.hpp"
#include "../geometry/Bench.QualityMetricsSmoke.hpp"
#include "../geometry/Bench.SignedHeatReferenceSmoke.hpp"
#include "../geometry/Bench.SurfaceSamplingSmoke.hpp"
#include "../physics/Bench.ParticleSpringReferenceSmoke.hpp"
#include "../physics/Bench.RigidBodyReferenceSmoke.hpp"
#include "../physics/Bench.SphFluidReferenceSmoke.hpp"
#include "../physics/Bench.XpbdClothReferenceSmoke.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    auto EscapeJson(std::string_view input) -> std::string
    {
        std::string out;
        out.reserve(input.size());
        for (const char ch : input)
        {
            switch (ch)
            {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n";  break;
                case '\r': out += "\\r";  break;
                case '\t': out += "\\t";  break;
                default:   out += ch;     break;
            }
        }
        return out;
    }

    auto ResolveCommit() -> std::string
    {
        const char* env = std::getenv("GIT_COMMIT");
        if (env != nullptr && env[0] != '\0')
        {
            return env;
        }
        return "local-dev";
    }

    struct EmittedBenchmark
    {
        std::string Id;
        std::string Payload;
        bool        Passed{false};
    };

    auto EmitHalfedgeSmoke(const std::string& commit) -> EmittedBenchmark
    {
        using namespace Intrinsic::Bench::Geometry;

        const auto metrics = RunHalfedgeSmoke();

        std::ostringstream out;
        out.setf(std::ios::fixed);
        out.precision(6);
        out << "{\n"
            << "  \"benchmark_id\": \"" << EscapeJson(kHalfedgeSmokeBenchmarkId) << "\",\n"
            << "  \"method\": \""       << EscapeJson(kHalfedgeSmokeMethod)      << "\",\n"
            << "  \"backend\": \"cpu_reference\",\n"
            << "  \"dataset\": \""      << EscapeJson(kHalfedgeSmokeDataset)     << "\",\n"
            << "  \"commit\": \""       << EscapeJson(commit)                    << "\",\n"
            << "  \"metrics\": {\n"
            << "    \"runtime_ms\": "      << metrics.RuntimeMilliseconds << ",\n"
            << "    \"quality_error_l2\": 0.0\n"
            << "  },\n"
            << "  \"diagnostics\": {\n"
            << "    \"runner\": \"IntrinsicBenchmarkSmoke\",\n"
            << "    \"mode\": \"smoke\",\n"
            << "    \"warmup_iterations\": 1,\n"
            << "    \"measured_iterations\": 8,\n"
            << "    \"total_area\": "    << metrics.TotalArea  << ",\n"
            << "    \"vertex_count\": "  << metrics.VertexCount << ",\n"
            << "    \"face_count\": "    << metrics.FaceCount   << "\n"
            << "  },\n"
            << "  \"status\": \"" << (metrics.Succeeded ? "passed" : "failed") << "\"\n"
            << "}\n";

        return EmittedBenchmark{kHalfedgeSmokeBenchmarkId, out.str(), metrics.Succeeded};
    }

    auto EmitParameterizationDiagnosticsSmoke(const std::string& commit) -> EmittedBenchmark
    {
        using namespace Intrinsic::Bench::Geometry;

        const auto metrics = RunParameterizationDiagnosticsSmoke();

        std::ostringstream out;
        out.setf(std::ios::fixed);
        out.precision(6);
        out << "{\n"
            << "  \"benchmark_id\": \"" << EscapeJson(kParameterizationDiagnosticsSmokeBenchmarkId) << "\",\n"
            << "  \"method\": \""       << EscapeJson(kParameterizationDiagnosticsSmokeMethod)      << "\",\n"
            << "  \"backend\": \"cpu_reference\",\n"
            << "  \"dataset\": \""      << EscapeJson(kParameterizationDiagnosticsSmokeDataset)     << "\",\n"
            << "  \"commit\": \""       << EscapeJson(commit)                                      << "\",\n"
            << "  \"metrics\": {\n"
            << "    \"runtime_ms\": "      << metrics.RuntimeMilliseconds << ",\n"
            << "    \"quality_error_l2\": 0.0\n"
            << "  },\n"
            << "  \"diagnostics\": {\n"
            << "    \"runner\": \"IntrinsicBenchmarkSmoke\",\n"
            << "    \"mode\": \"smoke\",\n"
            << "    \"warmup_iterations\": 1,\n"
            << "    \"measured_iterations\": 8,\n"
            << "    \"mean_conformal_distortion\": " << metrics.MeanConformalDistortion << ",\n"
            << "    \"max_area_distortion\": "       << metrics.MaxAreaDistortion << ",\n"
            << "    \"mean_stretch\": "              << metrics.MeanStretch << ",\n"
            << "    \"evaluated_face_count\": "      << metrics.EvaluatedFaceCount << ",\n"
            << "    \"flipped_element_count\": "     << metrics.FlippedElementCount << "\n"
            << "  },\n"
            << "  \"status\": \"" << (metrics.Succeeded ? "passed" : "failed") << "\"\n"
            << "}\n";

        return EmittedBenchmark{kParameterizationDiagnosticsSmokeBenchmarkId, out.str(), metrics.Succeeded};
    }

    auto EmitProgressivePoissonReferenceSmoke(const std::string& commit) -> EmittedBenchmark
    {
        using namespace Intrinsic::Bench::Geometry;

        const auto metrics = RunProgressivePoissonReferenceSmoke();

        std::ostringstream out;
        out.setf(std::ios::fixed);
        out.precision(6);
        out << "{\n"
            << "  \"benchmark_id\": \"" << EscapeJson(kProgressivePoissonReferenceSmokeBenchmarkId) << "\",\n"
            << "  \"method\": \""       << EscapeJson(kProgressivePoissonReferenceSmokeMethod)      << "\",\n"
            << "  \"backend\": \"cpu_reference\",\n"
            << "  \"dataset\": \""      << EscapeJson(kProgressivePoissonReferenceSmokeDataset)     << "\",\n"
            << "  \"commit\": \""       << EscapeJson(commit)                                       << "\",\n"
            << "  \"metrics\": {\n"
            << "    \"runtime_ms\": "       << metrics.RuntimeMilliseconds << ",\n"
            << "    \"quality_error_l2\": " << metrics.QualityErrorL2 << "\n"
            << "  },\n"
            << "  \"diagnostics\": {\n"
            << "    \"runner\": \"IntrinsicBenchmarkSmoke\",\n"
            << "    \"mode\": \"smoke\",\n"
            << "    \"warmup_iterations\": 1,\n"
            << "    \"measured_iterations\": 8,\n"
            << "    \"poisson_ratio_min\": "  << metrics.PoissonRatioMin << ",\n"
            << "    \"coverage_fraction\": "  << metrics.CoverageFraction << ",\n"
            << "    \"accepted_count\": "     << metrics.AcceptedCount << ",\n"
            << "    \"level_count\": "        << metrics.LevelCount << "\n"
            << "  },\n"
            << "  \"status\": \"" << (metrics.Succeeded ? "passed" : "failed") << "\"\n"
            << "}\n";

        return EmittedBenchmark{kProgressivePoissonReferenceSmokeBenchmarkId, out.str(), metrics.Succeeded};
    }

    auto EmitSignedHeatReferenceSmoke(const std::string& commit) -> EmittedBenchmark
    {
        using namespace Intrinsic::Bench::Geometry;

        const auto metrics = RunSignedHeatReferenceSmoke();

        std::ostringstream out;
        out.setf(std::ios::fixed);
        out.precision(6);
        out << "{\n"
            << "  \"benchmark_id\": \"" << EscapeJson(kSignedHeatReferenceSmokeBenchmarkId) << "\",\n"
            << "  \"method\": \""       << EscapeJson(kSignedHeatReferenceSmokeMethod)      << "\",\n"
            << "  \"backend\": \"cpu_reference\",\n"
            << "  \"dataset\": \""      << EscapeJson(kSignedHeatReferenceSmokeDataset)     << "\",\n"
            << "  \"commit\": \""       << EscapeJson(commit)                               << "\",\n"
            << "  \"metrics\": {\n"
            << "    \"runtime_ms\": "       << metrics.RuntimeMilliseconds << ",\n"
            << "    \"quality_error_l2\": " << metrics.QualityErrorL2 << "\n"
            << "  },\n"
            << "  \"diagnostics\": {\n"
            << "    \"runner\": \"IntrinsicBenchmarkSmoke\",\n"
            << "    \"mode\": \"smoke\",\n"
            << "    \"warmup_iterations\": 1,\n"
            << "    \"measured_iterations\": 8,\n"
            << "    \"source_vertex_count\": " << metrics.SourceVertexCount << ",\n"
            << "    \"degenerate_boundary_vertex_count\": " << metrics.DegenerateBoundaryVertexCount << ",\n"
            << "    \"max_abs_distance\": " << metrics.MaxAbsDistance << ",\n"
            << "    \"mean_boundary_offset\": " << metrics.MeanBoundaryOffset << "\n"
            << "  },\n"
            << "  \"status\": \"" << (metrics.Succeeded ? "passed" : "failed") << "\"\n"
            << "}\n";

        return EmittedBenchmark{kSignedHeatReferenceSmokeBenchmarkId, out.str(), metrics.Succeeded};
    }

    auto EmitSurfaceSamplingSmoke(const std::string& commit) -> EmittedBenchmark
    {
        using namespace Intrinsic::Bench::Geometry;

        const auto metrics = RunSurfaceSamplingSmoke();

        std::ostringstream out;
        out.setf(std::ios::fixed);
        out.precision(6);
        out << "{\n"
            << "  \"benchmark_id\": \"" << EscapeJson(kSurfaceSamplingSmokeBenchmarkId) << "\",\n"
            << "  \"method\": \""       << EscapeJson(kSurfaceSamplingSmokeMethod)      << "\",\n"
            << "  \"backend\": \"cpu_reference\",\n"
            << "  \"dataset\": \""      << EscapeJson(kSurfaceSamplingSmokeDataset)     << "\",\n"
            << "  \"commit\": \""       << EscapeJson(commit)                           << "\",\n"
            << "  \"metrics\": {\n"
            << "    \"runtime_ms\": "       << metrics.RuntimeMilliseconds << ",\n"
            << "    \"quality_error_l2\": " << metrics.QualityErrorL2 << "\n"
            << "  },\n"
            << "  \"diagnostics\": {\n"
            << "    \"runner\": \"IntrinsicBenchmarkSmoke\",\n"
            << "    \"mode\": \"smoke\",\n"
            << "    \"warmup_iterations\": 1,\n"
            << "    \"measured_iterations\": 8,\n"
            << "    \"sample_count\": " << metrics.WrittenSampleCount << ",\n"
            << "    \"accepted_triangle_count\": " << metrics.AcceptedTriangleCount << ",\n"
            << "    \"small_triangle_fraction\": " << metrics.SmallTriangleFraction << ",\n"
            << "    \"expected_small_triangle_fraction\": " << metrics.ExpectedSmallTriangleFraction << "\n"
            << "  },\n"
            << "  \"status\": \"" << (metrics.Succeeded ? "passed" : "failed") << "\"\n"
            << "}\n";

        return EmittedBenchmark{kSurfaceSamplingSmokeBenchmarkId, out.str(), metrics.Succeeded};
    }

    auto EmitQualityMetricsSmoke(const std::string& commit) -> EmittedBenchmark
    {
        using namespace Intrinsic::Bench::Geometry;

        const auto metrics = RunQualityMetricsSmoke();

        std::ostringstream out;
        out.setf(std::ios::fixed);
        out.precision(6);
        out << "{\n"
            << "  \"benchmark_id\": \"" << EscapeJson(kQualityMetricsSmokeBenchmarkId) << "\",\n"
            << "  \"method\": \""       << EscapeJson(kQualityMetricsSmokeMethod)      << "\",\n"
            << "  \"backend\": \"cpu_reference\",\n"
            << "  \"dataset\": \""      << EscapeJson(kQualityMetricsSmokeDataset)     << "\",\n"
            << "  \"commit\": \""       << EscapeJson(commit)                          << "\",\n"
            << "  \"metrics\": {\n"
            << "    \"runtime_ms\": "       << metrics.RuntimeMilliseconds << ",\n"
            << "    \"quality_error_l2\": " << metrics.QualityErrorL2 << "\n"
            << "  },\n"
            << "  \"diagnostics\": {\n"
            << "    \"runner\": \"IntrinsicBenchmarkSmoke\",\n"
            << "    \"mode\": \"smoke\",\n"
            << "    \"warmup_iterations\": 1,\n"
            << "    \"measured_iterations\": 8,\n"
            << "    \"point_count\": " << metrics.PointCount << ",\n"
            << "    \"nearest_neighbor_cv\": " << metrics.NearestNeighborCv << ",\n"
            << "    \"poisson_ratio\": " << metrics.PoissonRatio << ",\n"
            << "    \"rdf_mean_away_from_zero\": " << metrics.RdfMeanAwayFromZero << ",\n"
            << "    \"raps_cv\": " << metrics.RapsCv << ",\n"
            << "    \"coverage_fraction\": " << metrics.CoverageFraction << "\n"
            << "  },\n"
            << "  \"status\": \"" << (metrics.Succeeded ? "passed" : "failed") << "\"\n"
            << "}\n";

        return EmittedBenchmark{kQualityMetricsSmokeBenchmarkId, out.str(), metrics.Succeeded};
    }

    auto EmitRigidBodyReferenceSmoke(const std::string& commit) -> EmittedBenchmark
    {
        using namespace Intrinsic::Bench::Physics;

        const auto metrics = RunRigidBodyReferenceSmoke();

        std::ostringstream out;
        out.setf(std::ios::fixed);
        out.precision(6);
        out << "{\n"
            << "  \"benchmark_id\": \"" << EscapeJson(kRigidBodyReferenceSmokeBenchmarkId) << "\",\n"
            << "  \"method\": \""       << EscapeJson(kRigidBodyReferenceSmokeMethod)      << "\",\n"
            << "  \"backend\": \"cpu_reference\",\n"
            << "  \"dataset\": \""      << EscapeJson(kRigidBodyReferenceSmokeDataset)     << "\",\n"
            << "  \"commit\": \""       << EscapeJson(commit)                              << "\",\n"
            << "  \"metrics\": {\n"
            << "    \"runtime_ms\": "      << metrics.RuntimeMilliseconds << ",\n"
            << "    \"quality_error_l2\": " << metrics.QualityErrorL2 << "\n"
            << "  },\n"
            << "  \"diagnostics\": {\n"
            << "    \"runner\": \"IntrinsicBenchmarkSmoke\",\n"
            << "    \"mode\": \"smoke\",\n"
            << "    \"warmup_iterations\": 1,\n"
            << "    \"measured_iterations\": 8,\n"
            << "    \"contact_count\": "          << metrics.ContactCount << ",\n"
            << "    \"unsupported_pair_count\": " << metrics.UnsupportedPairCount << ",\n"
            << "    \"final_velocity_a\": "       << metrics.FinalVelocityA << ",\n"
            << "    \"final_velocity_b\": "       << metrics.FinalVelocityB << "\n"
            << "  },\n"
            << "  \"status\": \"" << (metrics.Succeeded ? "passed" : "failed") << "\"\n"
            << "}\n";

        return EmittedBenchmark{kRigidBodyReferenceSmokeBenchmarkId, out.str(), metrics.Succeeded};
    }

    auto EmitParticleSpringReferenceSmoke(const std::string& commit) -> EmittedBenchmark
    {
        using namespace Intrinsic::Bench::Physics;

        const auto metrics = RunParticleSpringReferenceSmoke();

        std::ostringstream out;
        out.setf(std::ios::fixed);
        out.precision(6);
        out << "{\n"
            << "  \"benchmark_id\": \"" << EscapeJson(kParticleSpringReferenceSmokeBenchmarkId) << "\",\n"
            << "  \"method\": \""       << EscapeJson(kParticleSpringReferenceSmokeMethod)      << "\",\n"
            << "  \"backend\": \"cpu_reference\",\n"
            << "  \"dataset\": \""      << EscapeJson(kParticleSpringReferenceSmokeDataset)     << "\",\n"
            << "  \"commit\": \""       << EscapeJson(commit)                                   << "\",\n"
            << "  \"metrics\": {\n"
            << "    \"runtime_ms\": "       << metrics.RuntimeMilliseconds << ",\n"
            << "    \"quality_error_l2\": " << metrics.QualityErrorL2 << "\n"
            << "  },\n"
            << "  \"diagnostics\": {\n"
            << "    \"runner\": \"IntrinsicBenchmarkSmoke\",\n"
            << "    \"mode\": \"smoke\",\n"
            << "    \"warmup_iterations\": 1,\n"
            << "    \"measured_iterations\": 8,\n"
            << "    \"degenerate_spring_count\": "   << metrics.DegenerateSpringCount << ",\n"
            << "    \"max_spring_residual\": "       << metrics.MaxSpringResidual << ",\n"
            << "    \"energy_drift\": "              << metrics.EnergyDrift << ",\n"
            << "    \"max_stiffness_dt_ratio\": "    << metrics.MaxStiffnessDtRatio << "\n"
            << "  },\n"
            << "  \"status\": \"" << (metrics.Succeeded ? "passed" : "failed") << "\"\n"
            << "}\n";

        return EmittedBenchmark{kParticleSpringReferenceSmokeBenchmarkId, out.str(), metrics.Succeeded};
    }

    auto EmitXpbdClothReferenceSmoke(const std::string& commit) -> EmittedBenchmark
    {
        using namespace Intrinsic::Bench::Physics;

        const auto metrics = RunXpbdClothReferenceSmoke();

        std::ostringstream out;
        out.setf(std::ios::fixed);
        out.precision(6);
        out << "{\n"
            << "  \"benchmark_id\": \"" << EscapeJson(kXpbdClothReferenceSmokeBenchmarkId) << "\",\n"
            << "  \"method\": \""       << EscapeJson(kXpbdClothReferenceSmokeMethod)      << "\",\n"
            << "  \"backend\": \"cpu_reference\",\n"
            << "  \"dataset\": \""      << EscapeJson(kXpbdClothReferenceSmokeDataset)     << "\",\n"
            << "  \"commit\": \""       << EscapeJson(commit)                              << "\",\n"
            << "  \"metrics\": {\n"
            << "    \"runtime_ms\": "       << metrics.RuntimeMilliseconds << ",\n"
            << "    \"quality_error_l2\": " << metrics.QualityErrorL2 << "\n"
            << "  },\n"
            << "  \"diagnostics\": {\n"
            << "    \"runner\": \"IntrinsicBenchmarkSmoke\",\n"
            << "    \"mode\": \"smoke\",\n"
            << "    \"warmup_iterations\": 1,\n"
            << "    \"measured_iterations\": 8,\n"
            << "    \"max_bend_residual\": "          << metrics.MaxBendResidual << ",\n"
            << "    \"degenerate_triangle_count\": "  << metrics.DegenerateTriangleCount << ",\n"
            << "    \"degenerate_constraint_count\": " << metrics.DegenerateConstraintCount << ",\n"
            << "    \"converged\": "                  << (metrics.Converged ? "true" : "false") << "\n"
            << "  },\n"
            << "  \"status\": \"" << (metrics.Succeeded ? "passed" : "failed") << "\"\n"
            << "}\n";

        return EmittedBenchmark{kXpbdClothReferenceSmokeBenchmarkId, out.str(), metrics.Succeeded};
    }

    auto EmitSphFluidReferenceSmoke(const std::string& commit) -> EmittedBenchmark
    {
        using namespace Intrinsic::Bench::Physics;

        const auto metrics = RunSphFluidReferenceSmoke();

        std::ostringstream out;
        out.setf(std::ios::fixed);
        out.precision(6);
        out << "{\n"
            << "  \"benchmark_id\": \"" << EscapeJson(kSphFluidReferenceSmokeBenchmarkId) << "\",\n"
            << "  \"method\": \""       << EscapeJson(kSphFluidReferenceSmokeMethod)      << "\",\n"
            << "  \"backend\": \"cpu_reference\",\n"
            << "  \"dataset\": \""      << EscapeJson(kSphFluidReferenceSmokeDataset)     << "\",\n"
            << "  \"commit\": \""       << EscapeJson(commit)                             << "\",\n"
            << "  \"metrics\": {\n"
            << "    \"runtime_ms\": "       << metrics.RuntimeMilliseconds << ",\n"
            << "    \"quality_error_l2\": " << metrics.QualityErrorL2 << "\n"
            << "  },\n"
            << "  \"diagnostics\": {\n"
            << "    \"runner\": \"IntrinsicBenchmarkSmoke\",\n"
            << "    \"mode\": \"smoke\",\n"
            << "    \"warmup_iterations\": 1,\n"
            << "    \"measured_iterations\": 4,\n"
            << "    \"column_max_compression\": "      << metrics.ColumnMaxCompression << ",\n"
            << "    \"column_avg_density_error\": "    << metrics.ColumnAverageDensityError << ",\n"
            << "    \"column_max_neighbor_count\": "   << metrics.ColumnMaxNeighborCount << ",\n"
            << "    \"column_stable\": "               << (metrics.ColumnStable ? "true" : "false") << "\n"
            << "  },\n"
            << "  \"status\": \"" << (metrics.Succeeded ? "passed" : "failed") << "\"\n"
            << "}\n";

        return EmittedBenchmark{kSphFluidReferenceSmokeBenchmarkId, out.str(), metrics.Succeeded};
    }

    auto WriteFile(const std::filesystem::path& path, std::string_view payload) -> bool
    {
        std::error_code ec;
        if (!path.parent_path().empty())
        {
            std::filesystem::create_directories(path.parent_path(), ec);
        }
        std::ofstream file(path, std::ios::trunc);
        if (!file.is_open())
        {
            std::cerr << "Failed to open output file: " << path << '\n';
            return false;
        }
        file << payload;
        return file.good();
    }
} // namespace

auto main(int argc, char** argv) -> int
{
    const std::filesystem::path outArg = argc > 1
        ? std::filesystem::path(argv[1])
        : std::filesystem::path("benchmark_smoke_result.json");

    const std::string commit = ResolveCommit();

    std::vector<EmittedBenchmark> emitted;
    emitted.push_back(EmitHalfedgeSmoke(commit));
    emitted.push_back(EmitParameterizationDiagnosticsSmoke(commit));
    emitted.push_back(EmitProgressivePoissonReferenceSmoke(commit));
    emitted.push_back(EmitSignedHeatReferenceSmoke(commit));
    emitted.push_back(EmitSurfaceSamplingSmoke(commit));
    emitted.push_back(EmitQualityMetricsSmoke(commit));
    emitted.push_back(EmitRigidBodyReferenceSmoke(commit));
    emitted.push_back(EmitParticleSpringReferenceSmoke(commit));
    emitted.push_back(EmitXpbdClothReferenceSmoke(commit));
    emitted.push_back(EmitSphFluidReferenceSmoke(commit));

    // Output target: an existing directory or a path with no extension (or no
    // filename component) is treated as a directory and gets one JSON per
    // benchmark inside; create it eagerly so a fresh build tree behaves the
    // same as a populated one. A path with an extension is treated as an
    // explicit file (preserving the single-artifact CI path); extras land as
    // siblings keyed by id.
    std::error_code dirEc;
    const bool      isExistingDir = std::filesystem::is_directory(outArg, dirEc);
    const bool      looksLikeDir  = !outArg.has_filename() || !outArg.has_extension();
    const bool      isDirectoryArg = isExistingDir || looksLikeDir;
    if (looksLikeDir && !isExistingDir)
    {
        std::filesystem::create_directories(outArg, dirEc);
    }

    int  exitCode      = 0;
    bool anyNotPassed  = false;
    for (std::size_t i = 0; i < emitted.size(); ++i)
    {
        std::filesystem::path target;
        if (isDirectoryArg)
        {
            target = outArg / (emitted[i].Id + ".json");
        }
        else if (i == 0)
        {
            target = outArg;
        }
        else
        {
            target = outArg.parent_path() / (emitted[i].Id + ".json");
        }

        if (!WriteFile(target, emitted[i].Payload))
        {
            exitCode = 1;
            continue;
        }
        std::cout << "Wrote benchmark smoke result: " << target << '\n';

        if (!emitted[i].Passed)
        {
            anyNotPassed = true;
            std::cerr << "Benchmark reported non-passed status: " << emitted[i].Id << '\n';
        }
    }

    // The result JSON itself remains schema-valid for downstream consumers
    // even when the workload failed (status="failed" is a valid value), but
    // the runner exit code must reflect failure so CTest/CI gates do not pass
    // silently on a broken smoke benchmark.
    if (anyNotPassed && exitCode == 0)
    {
        exitCode = 2;
    }
    return exitCode;
}

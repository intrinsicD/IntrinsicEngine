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
#include "../physics/Bench.ParticleSpringReferenceSmoke.hpp"
#include "../physics/Bench.RigidBodyReferenceSmoke.hpp"

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
    emitted.push_back(EmitRigidBodyReferenceSmoke(commit));
    emitted.push_back(EmitParticleSpringReferenceSmoke(commit));

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

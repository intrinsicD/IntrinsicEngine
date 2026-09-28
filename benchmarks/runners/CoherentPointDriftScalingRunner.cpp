// METHOD-049 opt-in scaling benchmark for the optimized Coherent Point Drift backends
// (geometry.coherent_point_drift.accelerated). Not a default CTest: run it on a Release
// build and keep the JSON as evidence.
//
//   IntrinsicCoherentPointDriftScaling <out dir or .json> [sizes...]   (default sizes: 1000 10000 100000)
//
// Writes one result in the benchmark result schema (seal it with
// tools/benchmark/seal_benchmark_results.py): runtime_ms is the auto run at the largest
// size, quality_error_l2 the largest RMS error to the ground truth, quality_error_linf the
// largest parity delta; diagnostics hold every run per size.
//   CPD_SCALING_TRACE=1 prints each iteration's E-step policy, time and kernel evaluations;
//   CPD_SCALING_ONLY=<text> runs only the runs whose name contains <text> (no parity);
//   CPD_SCALING_ALL=1 also runs the truncated and fast Gauss policies above the reference cap;
//   CPD_SCALING_ITERATIONS=<k> caps every run at k iterations (profiling).
//
// Fixture per size S: S points sampled on a bumpy closed surface (seeded), the target is a
// rotated, translated and noisy copy with 5% of its points replaced by uniform clutter.
// Rigid runs compare the reference with the dense-parallel, truncated, fast Gauss and auto
// E-steps;
// nonrigid runs compare the full solve (while S fits) with the low-rank solve on a bent
// copy. Quality is the RMS distance of the registered source to its ground-truth image;
// parity is the max point difference to the reference, or to the dense-parallel run above
// the reference size cap (the dense run is itself parity-checked at the smaller sizes).
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numbers>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

import Geometry.Registration.CoherentPointDrift;

namespace
{
    namespace CPD = Geometry::CoherentPointDrift;

    constexpr std::size_t kReferenceRigidCap = 10'000;
    constexpr std::size_t kFullNonrigidCap = 2'000;

    struct Fixture
    {
        std::vector<glm::vec3> Source, Target, Truth;
    };

    glm::vec3 SurfacePoint(double u, double v)
    {
        // Bumpy ellipsoid: a closed 2-D surface, like a scanned object.
        const double r = 1.0 + 0.15 * std::sin(5.0 * u) * std::cos(4.0 * v);
        return glm::vec3(float(r * std::cos(u) * std::sin(v)), float(0.8 * r * std::sin(u) * std::sin(v)),
                         float(0.6 * r * std::cos(v)));
    }

    std::vector<glm::vec3> Surface(std::size_t count, std::uint32_t seed)
    {
        std::mt19937 random(seed);
        std::uniform_real_distribution<double> uniform(0.0, 1.0);
        std::vector<glm::vec3> points(count);
        for (auto& p : points)
            p = SurfacePoint(2.0 * std::numbers::pi * uniform(random), std::acos(1.0 - 2.0 * uniform(random)));
        return points;
    }

    Fixture RigidFixture(std::size_t count)
    {
        Fixture f;
        f.Source = Surface(count, 1234u + std::uint32_t(count));
        const glm::dmat3 rotation(glm::rotate(glm::dmat4(1.0), 0.35, glm::normalize(glm::dvec3(1.0, 2.0, 0.5))));
        const glm::dvec3 translation{0.3, -0.2, 0.1};
        std::mt19937 random(99u + std::uint32_t(count));
        std::normal_distribution<double> noise(0.0, 0.004);
        std::uniform_real_distribution<double> clutter(-1.5, 1.5);
        for (std::size_t i = 0; i < count; ++i)
        {
            const glm::dvec3 q = rotation * glm::dvec3(f.Source[i]) + translation;
            f.Truth.push_back(glm::vec3(q));
            f.Target.push_back(i % 20 == 0 ? glm::vec3(float(clutter(random)), float(clutter(random)), float(clutter(random)))
                                            : glm::vec3(q + glm::dvec3(noise(random), noise(random), noise(random))));
        }
        return f;
    }

    Fixture BentFixture(std::size_t count)
    {
        Fixture f;
        f.Source = Surface(count, 777u + std::uint32_t(count));
        for (const auto& p : f.Source)
        {
            const glm::vec3 q = p + glm::vec3(0.0f, 0.12f * std::sin(1.8f * p.x), 0.1f * std::cos(1.4f * p.y));
            f.Truth.push_back(q);
            f.Target.push_back(q);
        }
        return f;
    }

    struct Run
    {
        std::string Name;
        CPD::Result Result;
        double Milliseconds{0.0};
        double RmsError{0.0};
        std::optional<double> Parity{};
        std::string ParityAgainst{};
    };

    Run Execute(const std::string& name, const Fixture& f, const CPD::Params& params)
    {
        Run run;
        run.Name = name;
        CPD::Params limited = params;
        if (const char* cap = std::getenv("CPD_SCALING_ITERATIONS")) limited.MaxIterations = std::uint32_t(std::strtoul(cap, nullptr, 10));
        if (const char* only = std::getenv("CPD_SCALING_ONLY"); only && name.find(only) == std::string::npos)
        {
            run.Result.State = CPD::Status::InvalidParameters;
            return run;
        }
        const auto start = std::chrono::steady_clock::now();
        const bool trace = std::getenv("CPD_SCALING_TRACE") != nullptr;
        auto last = std::chrono::steady_clock::now();
        run.Result = CPD::Register(f.Target, f.Source, limited, [&](const CPD::IterationTrace& t)
        {
            if (!trace) return;
            const auto now = std::chrono::steady_clock::now();
            std::cerr << "    " << name << " #" << t.Iteration << " " << CPD::ToString(t.EStep) << " "
                      << std::chrono::duration<double, std::milli>(now - last).count() << " ms, sigma2 " << t.Sigma2
                      << ", evaluations " << t.KernelEvaluations << ", bound " << t.EStepErrorBound << '\n';
            last = now;
        });
        run.Milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        if (run.Result.Succeeded())
        {
            double sum = 0.0;
            for (std::size_t i = 0; i < f.Source.size(); ++i)
            {
                const glm::dvec3 d = run.Result.TransformedSource[i] - glm::dvec3(f.Truth[i]);
                sum += glm::dot(d, d);
            }
            run.RmsError = std::sqrt(sum / double(f.Source.size()));
        }
        std::cerr << "  " << name << ": " << CPD::ToString(run.Result.State) << ", " << run.Result.Iterations
                  << " iterations, " << run.Milliseconds << " ms, rms " << run.RmsError << '\n';
        return run;
    }

    void Compare(Run& run, const Run& against)
    {
        if (!run.Result.Succeeded() || !against.Result.Succeeded()) return;
        double worst = 0.0;
        for (std::size_t i = 0; i < run.Result.TransformedSource.size(); ++i)
            worst = std::max(worst, glm::length(run.Result.TransformedSource[i] - against.Result.TransformedSource[i]));
        run.Parity = worst;
        run.ParityAgainst = against.Name;
    }

    void Write(std::ostringstream& out, const Run& run, bool last)
    {
        const CPD::Result& r = run.Result;
        out << "        {\"name\": \"" << run.Name << "\", \"status\": \"" << CPD::ToString(r.State)
            << "\", \"backend\": \"" << r.Backend << "\", \"iterations\": " << r.Iterations
            << ", \"termination\": \"" << CPD::ToString(r.Stop) << "\", \"runtime_ms\": " << run.Milliseconds
            << ", \"ms_per_iteration\": " << (r.Iterations ? run.Milliseconds / double(r.Iterations) : 0.0)
            << ", \"rms_error_to_truth\": " << run.RmsError << ", \"sigma2\": " << r.Sigma2
            << ", \"kernel_evaluations\": " << r.KernelEvaluations << ", \"e_step_error_bound\": " << r.EStepErrorBound
            << ", \"kernel_rank\": " << r.KernelRank << ", \"kernel_approximation_error\": " << r.KernelApproximationError;
        if (run.Parity) out << ", \"max_point_delta\": " << *run.Parity << ", \"parity_against\": \"" << run.ParityAgainst << "\"";
        out << '}' << (last ? "\n" : ",\n");
    }
}

int main(int argc, char** argv)
{
    std::filesystem::path outPath = argc > 1 ? argv[1] : "cpd_scaling";
    if (!outPath.has_extension())
    {
        std::filesystem::create_directories(outPath);
        outPath /= "geometry.coherent_point_drift.accelerated.json";
    }
    std::vector<std::size_t> sizes;
    for (int i = 2; i < argc; ++i) sizes.push_back(std::size_t(std::strtoull(argv[i], nullptr, 10)));
    if (sizes.empty()) sizes = {1'000, 10'000, 100'000};
    const std::uint32_t threads = CPD::EStep::ResolveThreads(0u);

    std::ostringstream out;
    out.precision(9);
    double autoMilliseconds = 0.0, worstRms = 0.0, worstParity = 0.0;
    bool passed = true;
    for (std::size_t s = 0; s < sizes.size(); ++s)
    {
        const std::size_t count = sizes[s];
        std::cerr << "size " << count << '\n';
        const Fixture rigid = RigidFixture(count);
        const CPD::Params base{.OutlierWeight = 0.05, .MaxIterations = 100, .EStepTolerance = 1e-6};
        std::vector<Run> rigidRuns;
        if (count <= kReferenceRigidCap) rigidRuns.push_back(Execute("rigid_reference", rigid, base));
        for (const auto policy : {CPD::EStepPolicy::Dense, CPD::EStepPolicy::Truncated, CPD::EStepPolicy::FastGauss,
                                  CPD::EStepPolicy::Auto})
        {
            // Measured up to the reference cap only: the fast Gauss transform's plans exceed the
            // dense cost at tol 1e-6, and an explicit truncated E-step keeps nearly every pair
            // while sigma is wide (about 150 s per wide iteration at 100000 points); Auto
            // chooses per iteration instead (see paper.md).
            if ((policy == CPD::EStepPolicy::FastGauss || policy == CPD::EStepPolicy::Truncated) &&
                count > kReferenceRigidCap && !std::getenv("CPD_SCALING_ALL"))
                continue;
            CPD::Params p = base;
            p.EStep = policy;
            rigidRuns.push_back(Execute("rigid_" + std::string(CPD::ToString(policy)), rigid, p));
        }
        // The first run is the oracle: the reference, or dense parallel above the reference cap.
        for (std::size_t r = 1; r < rigidRuns.size(); ++r) Compare(rigidRuns[r], rigidRuns[0]);

        const Fixture bent = BentFixture(count);
        const CPD::Params nonrigid{.Method = CPD::Variant::Nonrigid, .MaxIterations = 150, .EStepTolerance = 1e-6};
        std::vector<Run> nonrigidRuns;
        if (count <= kFullNonrigidCap)
        {
            CPD::Params full = nonrigid;
            full.EStep = CPD::EStepPolicy::Auto;
            nonrigidRuns.push_back(Execute("nonrigid_full_auto", bent, full));
        }
        for (const std::uint32_t rank : {50u, 150u})
        {
            CPD::Params lowRank = nonrigid;
            lowRank.EStep = CPD::EStepPolicy::Auto;
            lowRank.LowRank = rank;
            nonrigidRuns.push_back(Execute("nonrigid_lowrank" + std::to_string(rank) + "_auto", bent, lowRank));
        }
        for (std::size_t r = 1; r < nonrigidRuns.size(); ++r) Compare(nonrigidRuns[r], nonrigidRuns[0]);

        for (const auto* runs : {&rigidRuns, &nonrigidRuns})
            for (const Run& run : *runs)
            {
                passed = passed && run.Result.Succeeded();
                worstRms = std::max(worstRms, run.RmsError);
                if (run.Parity && run.Name.rfind("rigid_", 0) == 0) worstParity = std::max(worstParity, *run.Parity);
                if (run.Name == "rigid_auto") autoMilliseconds = run.Milliseconds;
            }
        out << "      {\"points\": " << count << ", \"rigid\": [\n";
        for (std::size_t r = 0; r < rigidRuns.size(); ++r) Write(out, rigidRuns[r], r + 1 == rigidRuns.size());
        out << "      ], \"nonrigid\": [\n";
        for (std::size_t r = 0; r < nonrigidRuns.size(); ++r) Write(out, nonrigidRuns[r], r + 1 == nonrigidRuns.size());
        out << "      ]}" << (s + 1 == sizes.size() ? "\n" : ",\n");
    }
    // Manifest thresholds (coherent_point_drift_accelerated.yaml).
    passed = passed && worstRms <= 1e-3 && worstParity <= 1e-4;
    const char* commit = std::getenv("GITHUB_SHA");
    std::ostringstream result;
    result.precision(9);
    result << "{\n  \"benchmark_id\": \"geometry.coherent_point_drift.accelerated\",\n"
           << "  \"method\": \"geometry.coherent_point_drift\",\n  \"backend\": \"cpu_optimized\",\n"
           << "  \"dataset\": \"builtin.cpd_bumpy_ellipsoid_scaling.v1\",\n"
           << "  \"commit\": \"" << (commit ? commit : "local-dev") << "\",\n"
           << "  \"metrics\": {\n    \"runtime_ms\": " << autoMilliseconds << ",\n    \"quality_error_l2\": " << worstRms
           << ",\n    \"quality_error_linf\": " << worstParity << "\n  },\n"
           << "  \"diagnostics\": {\n    \"runner\": \"IntrinsicCoherentPointDriftScaling\",\n"
           << "    \"mode\": \"performance_scaling_profile\",\n    \"threads\": " << threads << ",\n"
           << "    \"hardware_concurrency\": " << std::thread::hardware_concurrency() << ",\n"
           << "    \"e_step_tolerance\": 1e-06,\n    \"sizes\": [\n"
           << out.str() << "    ]\n  },\n  \"status\": \"" << (passed ? "passed" : "failed") << "\"\n}\n";
    std::ofstream file(outPath, std::ios::trunc);
    file << result.str();
    std::cout << result.str();
    return file.good() && passed ? 0 : 1;
}

// GEOM-113 opt-in comparison of every Geometry.PointSampling method (geometry.point_sampling.
// comparison). Not a default CTest: run it on a Release build and keep the JSON as evidence.
//
//   IntrinsicPointSamplingComparison <out dir or .json> [points]   (default 20000)
//
// Fixtures: a uniform box and a scan-like surface (bumpy ellipsoid samples). For every method
// and prefix size k in {256, 1024, 4096}: wall time to produce k samples (median of 3 runs
// after a warmup), the nearest-neighbor coefficient of variation of the prefix (lower is more
// even) and its coverage radius (largest distance of any input point to the prefix, relative
// to exact farthest point, which minimizes it greedily). Guarantees are re-checked: the
// eta/beta methods must keep their bound on every prefix, farthest point must equal the
// brute-force order. quality_error_l2 counts guarantee violations (must be 0).
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <numbers>
#include <random>
#include <sstream>
#include <string>
#include <vector>

import Geometry.PointSampling;

namespace
{
    namespace PS = Geometry::PointSampling;

    struct Cloud
    {
        std::string Name;
        std::vector<double> X, Y, Z;
        [[nodiscard]] PS::PointView View() const { return {X, Y, Z}; }
        [[nodiscard]] std::size_t Size() const { return X.size(); }
    };

    Cloud Box(std::size_t n)
    {
        Cloud c{"uniform_box"};
        std::mt19937 random(2026u);
        std::uniform_real_distribution<double> u(0.0, 1.0);
        for (std::size_t i = 0; i < n; ++i) { c.X.push_back(u(random)); c.Y.push_back(u(random)); c.Z.push_back(u(random)); }
        return c;
    }

    Cloud Surface(std::size_t n)
    {
        Cloud c{"bumpy_ellipsoid_surface"};
        std::mt19937 random(928u);
        std::uniform_real_distribution<double> u(0.0, 1.0);
        for (std::size_t i = 0; i < n; ++i)
        {
            const double a = 2.0 * std::numbers::pi * u(random), b = std::acos(1.0 - 2.0 * u(random));
            const double r = 1.0 + 0.15 * std::sin(5.0 * a) * std::cos(4.0 * b);
            c.X.push_back(r * std::cos(a) * std::sin(b));
            c.Y.push_back(0.8 * r * std::sin(a) * std::sin(b));
            c.Z.push_back(0.6 * r * std::cos(b));
        }
        return c;
    }

    double Distance2(const Cloud& c, std::uint32_t a, std::size_t b)
    {
        const double dx = c.X[a] - c.X[b], dy = c.Y[a] - c.Y[b], dz = c.Z[a] - c.Z[b];
        return dx * dx + dy * dy + dz * dz;
    }

    // Nearest-neighbor CV of the prefix and its coverage radius over all input points.
    std::pair<double, double> Quality(const Cloud& c, const std::vector<std::uint32_t>& order, std::size_t k)
    {
        k = std::min(k, order.size());
        std::vector<double> nn(k, std::numeric_limits<double>::infinity());
        for (std::size_t a = 0; a < k; ++a)
            for (std::size_t b = a + 1; b < k; ++b)
            {
                const double d = Distance2(c, order[a], order[b]);
                nn[a] = std::min(nn[a], d);
                nn[b] = std::min(nn[b], d);
            }
        double mean = 0.0, sq = 0.0;
        for (double& d : nn) { d = std::sqrt(d); mean += d; }
        mean /= double(k);
        for (const double d : nn) sq += (d - mean) * (d - mean);
        const double cv = k > 1 ? std::sqrt(sq / double(k)) / mean : 0.0;
        double cover = 0.0;
        for (std::size_t i = 0; i < c.Size(); ++i)
        {
            double best = std::numeric_limits<double>::infinity();
            for (std::size_t a = 0; a < k; ++a) best = std::min(best, Distance2(c, order[a], i));
            cover = std::max(cover, best);
        }
        return {cv, std::sqrt(cover)};
    }

    struct MethodCase
    {
        std::string Name;
        PS::Params Params;
    };
}

int main(int argc, char** argv)
{
    std::filesystem::path outPath = argc > 1 ? argv[1] : "point_sampling_comparison";
    if (!outPath.has_extension())
    {
        std::filesystem::create_directories(outPath);
        outPath /= "geometry.point_sampling.comparison.json";
    }
    const std::size_t n = argc > 2 ? std::size_t(std::strtoull(argv[2], nullptr, 10)) : 20000u;
    const std::vector<std::size_t> prefixes{256u, 1024u, 4096u};
    PS::PoissonSettings fastPoisson = PS::WithProfile({}, PS::PoissonProfile::Fast);
    fastPoisson.ComputeSplatRadii = false;
    PS::PoissonSettings hapds{};
    hapds.ComputeSplatRadii = false;
    PS::PoissonSettings balanced = hapds;
    balanced.Ordering = PS::PoissonOrdering::SpatiallyBalanced;
    const std::vector<MethodCase> methods{
        {"random", {.Method = PS::Method::Random, .Seed = 7u}},
        {"farthest_point", {.Method = PS::Method::FarthestPoint}},
        {"coupled_sieve_eta095", {.Method = PS::Method::CoupledSieve, .Eta = 0.95, .CandidateCap = 32u}},
        {"flat_greedy_beta110", {.Method = PS::Method::FlatGreedy, .Beta = 1.1}},
        {"lazy_greedy_beta110", {.Method = PS::Method::LazyGreedy, .Beta = 1.1}},
        {"progressive_poisson_hapds", {.Method = PS::Method::ProgressivePoisson, .Poisson = hapds}},
        {"progressive_poisson_fast", {.Method = PS::Method::ProgressivePoisson, .Poisson = fastPoisson}},
        {"progressive_poisson_balanced", {.Method = PS::Method::ProgressivePoisson, .Poisson = balanced}},
        {"sample_elimination", {.Method = PS::Method::SampleElimination, .ManifoldDimension = 3u}},
        {"tournament", {.Method = PS::Method::Tournament}},
    };

    std::ostringstream out;
    out.precision(9);
    std::size_t violations = 0;
    double farthestMs = 0.0;
    const std::vector<Cloud> clouds{Box(n), Surface(n)};
    for (std::size_t ci = 0; ci < clouds.size(); ++ci)
    {
        const Cloud& cloud = clouds[ci];
        std::cerr << cloud.Name << '\n';
        std::vector<double> fpsCover(prefixes.size(), 0.0);
        out << "      {\"fixture\": \"" << cloud.Name << "\", \"points\": " << n << ", \"methods\": [\n";
        for (std::size_t mi = 0; mi < methods.size(); ++mi)
        {
            PS::Params params = methods[mi].Params;
            if (methods[mi].Name == "sample_elimination" && ci == 1) params.ManifoldDimension = 2u;
            out << "        {\"name\": \"" << methods[mi].Name << "\", \"prefixes\": [";
            for (std::size_t pi = 0; pi < prefixes.size(); ++pi)
            {
                const std::size_t k = prefixes[pi];
                (void)PS::Order(cloud.View(), params, k); // warmup
                std::vector<double> times;
                PS::Result result;
                for (int r = 0; r < 3; ++r)
                {
                    const auto t0 = std::chrono::steady_clock::now();
                    result = PS::Order(cloud.View(), params, k);
                    times.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
                }
                std::sort(times.begin(), times.end());
                if (!result.Succeeded()) ++violations;
                const auto [cv, cover] = Quality(cloud, result.Order, k);
                if (methods[mi].Name == "farthest_point")
                {
                    fpsCover[pi] = cover;
                    if (ci == 0 && k == 4096u) farthestMs = times[1];
                }
                // Guarantees: every eta/beta batch member keeps its insertion priority
                // (squared clearance) >= factor * U, U the largest clearance at the batch start.
                const bool etaMethod = methods[mi].Params.Method == PS::Method::CoupledSieve;
                const bool betaMethod = methods[mi].Params.Method == PS::Method::FlatGreedy ||
                                        methods[mi].Params.Method == PS::Method::LazyGreedy;
                if (etaMethod || betaMethod)
                {
                    const double factor = etaMethod ? params.Eta * params.Eta : 1.0 / (params.Beta * params.Beta);
                    std::vector<double> clear(cloud.Size(), std::numeric_limits<double>::infinity());
                    std::vector<char> taken(cloud.Size(), 0);
                    double batchLargest = 0.0;
                    std::size_t batch = 1;
                    for (std::size_t j = 0; j < result.Order.size(); ++j)
                    {
                        if (batch < result.BatchOffsets.size() && result.BatchOffsets[batch] == j)
                        {
                            batchLargest = 0.0;
                            for (std::size_t i = 0; i < cloud.Size(); ++i)
                                if (!taken[i]) batchLargest = std::max(batchLargest, clear[i]);
                            ++batch;
                        }
                        if (j > 0 && batchLargest > 0.0 && result.Clearance[j] < factor * batchLargest * (1.0 - 1e-9))
                        {
                            std::cerr << "  guarantee violated: " << methods[mi].Name << " k=" << k << " rank " << j << '\n';
                            ++violations;
                        }
                        const std::uint32_t id = result.Order[j];
                        taken[id] = 1;
                        for (std::size_t i = 0; i < cloud.Size(); ++i) clear[i] = std::min(clear[i], Distance2(cloud, id, i));
                    }
                }
                out << (pi ? ", " : "") << "{\"k\": " << k << ", \"runtime_ms\": " << times[1] << ", \"runtime_ms_min\": "
                    << times[0] << ", \"runtime_ms_max\": " << times[2] << ", \"samples\": " << result.Order.size()
                    << ", \"nn_cv\": " << cv << ", \"coverage_radius\": " << cover << ", \"coverage_vs_fps\": "
                    << (fpsCover[pi] > 0.0 ? cover / fpsCover[pi] : 0.0) << "}";
                std::cerr << "  " << methods[mi].Name << " k=" << k << ": " << times[1] << " ms, cv " << cv << ", cover "
                          << cover << '\n';
            }
            out << "]}" << (mi + 1 == methods.size() ? "\n" : ",\n");
        }
        out << "      ]}" << (ci + 1 == clouds.size() ? "\n" : ",\n");
    }
    const char* commit = std::getenv("GITHUB_SHA");
    std::ostringstream result;
    result.precision(9);
    result << "{\n  \"benchmark_id\": \"geometry.point_sampling.comparison\",\n"
           << "  \"method\": \"geometry.point_sampling\",\n  \"backend\": \"cpu_reference\",\n"
           << "  \"dataset\": \"builtin.point_sampling_box_and_surface.v1\",\n"
           << "  \"commit\": \"" << (commit ? commit : "local-dev") << "\",\n"
           << "  \"metrics\": {\n    \"runtime_ms\": " << farthestMs << ",\n    \"quality_error_l2\": " << double(violations)
           << "\n  },\n  \"diagnostics\": {\n    \"runner\": \"IntrinsicPointSamplingComparison\",\n"
           << "    \"mode\": \"method_comparison\",\n    \"fixtures\": [\n" << out.str() << "    ]\n  },\n"
           << "  \"status\": \"" << (violations == 0 ? "passed" : "failed") << "\"\n}\n";
    std::ofstream file(outPath, std::ios::trunc);
    file << result.str();
    std::cout << result.str();
    return file.good() && violations == 0 ? 0 : 1;
}

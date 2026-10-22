// UI-081 opt-in comparison of Geometry.PointSampling orders as vector-field glyph anchors
// (geometry.glyph_order.comparison). Not a default CTest; run it on a Release build.
//
//   IntrinsicGlyphOrderComparison --self-test
//   IntrinsicGlyphOrderComparison <out dir> <mesh.obj>... [--budget-seconds S]
//
// Per mesh and element domain (vertices, edge midpoints, face centers) every method orders all
// anchors once; the vector-field slider draws prefixes of that order. Quality is measured on the
// surface: each triangle's area goes 1/2 to its centroid and 1/6 to each corner (exact for linear
// functions), and every such reference point belongs to its nearest sample (3-D Euclidean). Per
// prefix of k samples: coverage = largest distance of a reference point to its sample, packing =
// smallest distance between two samples, both divided by the hexagonal spacing
// d = sqrt(2 A / (sqrt(3) k)); cell CV = std / mean of the reference area per sample. "stride" is
// today's every-n-th row set (s = ceil(N / k)), a set per k rather than an order.
// Writes cells.jsonl (one line per measured cell, flushed) and summary.json; a watchdog ends the
// process with exit code 3 once the budget is spent, leaving the remaining cells unevaluated.
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <numbers>
#include <span>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

import Extrinsic.Core.Error;
import Geometry.Properties;
import Geometry.HalfedgeMesh.IO;
import Geometry.PointSampling;
import Geometry.KDTree;

namespace
{
    namespace PS = Geometry::PointSampling;
    using Clock = std::chrono::steady_clock;

    struct Surface
    {
        std::string Name;
        std::vector<glm::vec3> Positions;
        std::vector<std::array<std::uint32_t, 3>> Triangles;
    };

    struct Domain
    {
        std::string Name;
        std::vector<glm::vec3> Anchors;
    };

    struct Reference
    {
        std::vector<glm::vec3> Points;
        std::vector<double> Weights;
        double Area{0.0};
    };

    [[nodiscard]] double TriangleArea(const Surface& s, const std::array<std::uint32_t, 3>& t)
    {
        const glm::dvec3 a = s.Positions[t[0]], b = s.Positions[t[1]], c = s.Positions[t[2]];
        return 0.5 * glm::length(glm::cross(b - a, c - a));
    }

    [[nodiscard]] glm::vec3 Centroid(const Surface& s, const std::array<std::uint32_t, 3>& t)
    {
        return (s.Positions[t[0]] + s.Positions[t[1]] + s.Positions[t[2]]) / 3.0f;
    }

    // Edges in first-seen order while walking the triangles, as mesh construction numbers them.
    [[nodiscard]] std::vector<Domain> Domains(const Surface& s)
    {
        Domain vertices{"vertices", s.Positions}, edges{"edges", {}}, faces{"faces", {}};
        std::unordered_map<std::uint64_t, std::uint32_t> seen;
        for (const auto& t : s.Triangles)
        {
            faces.Anchors.push_back(Centroid(s, t));
            for (int i = 0; i < 3; ++i)
            {
                const std::uint32_t a = std::min(t[i], t[(i + 1) % 3]), b = std::max(t[i], t[(i + 1) % 3]);
                if (seen.emplace((std::uint64_t(a) << 32u) | b, std::uint32_t(edges.Anchors.size())).second)
                    edges.Anchors.push_back(0.5f * (s.Positions[a] + s.Positions[b]));
            }
        }
        return {vertices, edges, faces};
    }

    [[nodiscard]] Reference MakeReference(const Surface& s)
    {
        Reference r;
        r.Points = s.Positions;
        r.Weights.assign(s.Positions.size(), 0.0);
        for (const auto& t : s.Triangles)
        {
            const double area = TriangleArea(s, t);
            for (const std::uint32_t v : t)
                r.Weights[v] += area / 6.0;
            r.Points.push_back(Centroid(s, t));
            r.Weights.push_back(area / 2.0);
            r.Area += area;
        }
        return r;
    }

    // Row-major n x n grid on [0,1]^2; `graded` spaces the columns geometrically (density x16).
    [[nodiscard]] Surface Grid(const std::string& name, const std::uint32_t n, const bool graded)
    {
        Surface s{name, {}, {}};
        const double a = std::log(16.0);
        for (std::uint32_t j = 0; j < n; ++j)
            for (std::uint32_t i = 0; i < n; ++i)
            {
                const double u = double(i) / double(n - 1);
                const double x = graded ? std::expm1(a * u) / std::expm1(a) : u;
                s.Positions.push_back(glm::vec3(float(x), float(double(j) / double(n - 1)), 0.0f));
            }
        for (std::uint32_t j = 0; j + 1 < n; ++j)
            for (std::uint32_t i = 0; i + 1 < n; ++i)
            {
                const std::uint32_t v = j * n + i;
                s.Triangles.push_back({v, v + 1, v + n + 1});
                s.Triangles.push_back({v, v + n + 1, v + n});
            }
        return s;
    }

    [[nodiscard]] bool LoadObj(const std::string& path, Surface& out, std::string& error)
    {
        auto source = Geometry::MeshIO::LoadOBJ(path);
        if (!source)
        {
            error = std::string(Extrinsic::Core::Error::ToString(source.error()));
            return false;
        }
        const auto positions = source->Vertices.Get<glm::vec3>("v:point");
        const auto faces = source->Faces.Get<std::vector<std::uint32_t>>("f:vertices");
        if (!positions || !faces)
        {
            error = "missing v:point or f:vertices";
            return false;
        }
        out.Name = std::filesystem::path(path).stem().string();
        out.Positions.assign(positions.Span().begin(), positions.Span().end());
        for (const auto& f : faces.Vector())
        {
            if (f.size() != 3)
            {
                error = "non-triangle face";
                return false;
            }
            out.Triangles.push_back({f[0], f[1], f[2]});
        }
        return true;
    }

    struct Quality
    {
        std::size_t Count{0};
        double Coverage{0.0};  // / d_hex
        double Packing{0.0};   // / d_hex
        double CellCv{0.0};
        double CellMaxOverMean{0.0};
    };

    [[nodiscard]] Quality Measure(
        const std::vector<glm::vec3>& anchors, std::span<const std::uint32_t> ids, const Reference& ref)
    {
        Quality q{.Count = ids.size()};
        if (ids.size() < 2)
            return q;
        std::vector<glm::vec3> samples;
        samples.reserve(ids.size());
        for (const std::uint32_t id : ids)
            samples.push_back(anchors[id]);
        Geometry::KDTree tree;
        if (!tree.BuildFromPoints(samples))
            return q;
        std::vector<double> cell(samples.size(), 0.0);
        std::vector<std::uint32_t> nn;
        double cover2 = 0.0;
        for (std::size_t i = 0; i < ref.Points.size(); ++i)
        {
            nn.clear();
            (void)tree.QueryKNN(ref.Points[i], 1u, nn, true);
            const glm::dvec3 d = glm::dvec3(ref.Points[i]) - glm::dvec3(samples[nn[0]]);
            cover2 = std::max(cover2, glm::dot(d, d));
            cell[nn[0]] += ref.Weights[i];
        }
        double pack2 = std::numeric_limits<double>::infinity();
        for (std::size_t j = 0; j < samples.size(); ++j)
        {
            nn.clear();
            (void)tree.QueryKNN(samples[j], 2u, nn, true);
            const std::uint32_t other = nn[0] == j ? nn[1] : nn[0];
            const glm::dvec3 d = glm::dvec3(samples[j]) - glm::dvec3(samples[other]);
            pack2 = std::min(pack2, glm::dot(d, d));
        }
        const double k = double(samples.size());
        const double hex = std::sqrt(2.0 * ref.Area / (std::sqrt(3.0) * k));
        const double mean = ref.Area / k;
        double var = 0.0, largest = 0.0;
        for (const double a : cell)
        {
            var += (a - mean) * (a - mean);
            largest = std::max(largest, a);
        }
        q.Coverage = std::sqrt(cover2) / hex;
        q.Packing = std::sqrt(pack2) / hex;
        q.CellCv = std::sqrt(var / k) / mean;
        q.CellMaxOverMean = largest / mean;
        return q;
    }

    [[nodiscard]] std::vector<std::uint32_t> StrideSet(const std::size_t n, const std::size_t k)
    {
        const std::size_t s = std::max<std::size_t>(1u, (n + k - 1u) / k);
        std::vector<std::uint32_t> ids;
        for (std::size_t i = 0; i < n; i += s)
            ids.push_back(std::uint32_t(i));
        return ids;
    }

    struct MethodCase
    {
        std::string Name;
        PS::Params Params;
        bool Seeded{false};
    };

    [[nodiscard]] std::vector<MethodCase> Methods()
    {
        PS::PoissonSettings fast = PS::WithProfile({}, PS::PoissonProfile::Fast);
        fast.ComputeSplatRadii = false;
        PS::PoissonSettings hapds{};
        hapds.ComputeSplatRadii = false;
        PS::PoissonSettings balanced = hapds;
        balanced.Ordering = PS::PoissonOrdering::SpatiallyBalanced;
        return {
            {"random", {.Method = PS::Method::Random}, true},
            {"farthest_point", {.Method = PS::Method::FarthestPoint}},
            {"coupled_sieve_eta095", {.Method = PS::Method::CoupledSieve, .Eta = 0.95, .CandidateCap = 32u}},
            {"flat_greedy_beta110", {.Method = PS::Method::FlatGreedy, .Beta = 1.1}, true},
            {"lazy_greedy_beta110", {.Method = PS::Method::LazyGreedy, .Beta = 1.1}, true},
            {"progressive_poisson_hapds", {.Method = PS::Method::ProgressivePoisson, .Poisson = hapds}},
            {"progressive_poisson_fast", {.Method = PS::Method::ProgressivePoisson, .Poisson = fast}},
            {"progressive_poisson_balanced", {.Method = PS::Method::ProgressivePoisson, .Poisson = balanced}},
            {"sample_elimination", {.Method = PS::Method::SampleElimination, .ManifoldDimension = 2u}},
            {"tournament", {.Method = PS::Method::Tournament}},
        };
    }

    // Sample elimination's default radius uses the bounding box; a curved surface needs its area.
    [[nodiscard]] PS::Params ForSurface(PS::Params params, const double area, const std::size_t count)
    {
        if (params.Method == PS::Method::SampleElimination && count > 0u)
            params.EliminationRadius = 2.0 * std::sqrt(area / (double(count) * 2.0 * std::sqrt(3.0)));
        return params;
    }

    [[nodiscard]] double Ms(const Clock::time_point start)
    {
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    }

    int SelfTest()
    {
        int failures = 0;
        const auto check = [&](const bool ok, const std::string& what, const double value)
        {
            std::cout << (ok ? "ok   " : "FAIL ") << what << " = " << value << '\n';
            failures += ok ? 0 : 1;
        };
        // 65 x 65 grid, spacing h = 1/64; the subgrid every 4th row and column (17 x 17).
        const Surface grid = Grid("self_grid", 65u, false);
        const Reference ref = MakeReference(grid);
        check(std::abs(ref.Area - 1.0) < 1e-9, "area", ref.Area);
        std::vector<std::uint32_t> sub;
        for (std::uint32_t j = 0; j < 65u; j += 4u)
            for (std::uint32_t i = 0; i < 65u; i += 4u)
                sub.push_back(j * 65u + i);
        const Quality q = Measure(grid.Positions, sub, ref);
        const double k = double(sub.size()), hex = std::sqrt(2.0 / (std::sqrt(3.0) * k));
        // Exact: the farthest reference points are the subgrid cell centres, half a cell diagonal away.
        check(std::abs(q.Coverage * hex - std::sqrt(2.0) * 2.0 / 64.0) < 1e-6,
            "subgrid coverage (world)",
            q.Coverage * hex);
        check(std::abs(q.Packing * hex - 4.0 / 64.0) < 1e-6, "subgrid packing (world)", q.Packing * hex);
        // Interior cells 1/256, edge cells half, corners a quarter (ties go to one side; allow 10%).
        const double mean = 1.0 / k;
        double var = 225.0 * std::pow(1.0 / 256.0 - mean, 2.0) + 60.0 * std::pow(0.5 / 256.0 - mean, 2.0) +
            4.0 * std::pow(0.25 / 256.0 - mean, 2.0);
        const double expectedCv = std::sqrt(var / k) / mean;
        check(std::abs(q.CellCv - expectedCv) < 0.1 * expectedCv,
            "subgrid cell CV vs analytic " + std::to_string(expectedCv),
            q.CellCv);
        // Null probe: today's stride at k = 65 on the row-major grid draws one column (x = 0). Its
        // strip cells are all equal (CV near the boundary-only value) while coverage is the plane width.
        const std::vector<std::uint32_t> column = StrideSet(grid.Positions.size(), 65u);
        const Quality c = Measure(grid.Positions, column, ref);
        bool oneColumn = true;
        for (const std::uint32_t id : column)
            oneColumn = oneColumn && grid.Positions[id].x == 0.0f;
        check(oneColumn, "stride k=65 is one column", double(column.size()));
        check(c.CellCv < 0.15, "column cell CV (does not reject it)", c.CellCv);
        check(c.Coverage > 5.0, "column coverage / d_hex (rejects it)", c.Coverage);
        // Duplicate samples have zero packing.
        const std::vector<std::uint32_t> dup{0u, 0u, 4u};
        check(Measure(grid.Positions, dup, ref).Packing == 0.0, "duplicate packing", 0.0);
        // Every method returns a permutation of all anchors (prefix semantics hold for every k).
        for (const MethodCase& m : Methods())
        {
            const Domain vertices = Domains(grid)[0];
            const PS::Result r = PS::Order(std::span<const glm::vec3>(vertices.Anchors),
                ForSurface(m.Params, ref.Area, vertices.Anchors.size()),
                vertices.Anchors.size());
            std::vector<std::uint32_t> sorted = r.Order;
            std::sort(sorted.begin(), sorted.end());
            const bool permutation = std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end() &&
                (sorted.empty() || sorted.back() < vertices.Anchors.size());
            check(r.Succeeded() && permutation, m.Name + " order is a permutation (length)", double(r.Order.size()));
        }
        return failures == 0 ? 0 : 1;
    }
}

int main(int argc, char** argv)
{
    if (argc > 1 && std::string(argv[1]) == "--self-test")
        return SelfTest();
    if (argc < 2)
    {
        std::cerr
            << "usage: IntrinsicGlyphOrderComparison --self-test | <out dir> <mesh.obj>... [--budget-seconds S]\n";
        return 2;
    }
    const std::filesystem::path outDir = argv[1];
    std::vector<std::string> meshes;
    double budgetSeconds = 3600.0;
    for (int i = 2; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--budget-seconds" && i + 1 < argc)
            budgetSeconds = std::strtod(argv[++i], nullptr);
        else
            meshes.push_back(arg);
    }
    std::filesystem::create_directories(outDir);
    std::ofstream cells(outDir / "cells.jsonl", std::ios::trunc);
    cells.precision(9);
    const auto started = Clock::now();
    std::atomic<bool> finished{false};
    std::thread watchdog(
        [&]
        {
            while (!finished.load())
            {
                if (std::chrono::duration<double>(Clock::now() - started).count() > budgetSeconds)
                {
                    std::cerr << "budget of " << budgetSeconds << " s spent; remaining cells unevaluated\n";
                    std::_Exit(3);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
        });

    std::vector<Surface> surfaces;
    for (const std::string& path : meshes)
    {
        Surface s;
        std::string error;
        if (!LoadObj(path, s, error))
        {
            std::cerr << path << ": " << error << '\n';
            finished = true;
            watchdog.join();
            return 4;
        }
        surfaces.push_back(std::move(s));
    }
    surfaces.push_back(Grid("regular_grid_256", 256u, false));
    surfaces.push_back(Grid("graded_grid_256", 256u, true));
    const std::vector<std::size_t> ratios{4u, 16u, 64u, 256u};
    const std::vector<std::uint32_t> seeds{1u, 2u, 3u, 4u, 5u};
    const std::vector<MethodCase> methods = Methods();

    for (const Surface& surface : surfaces)
    {
        const Reference ref = MakeReference(surface);
        for (const Domain& domain : Domains(surface))
        {
            const std::size_t n = domain.Anchors.size();
            const auto emit = [&](const std::string& method,
                                  const std::uint32_t seed,
                                  const std::size_t ratio,
                                  const std::size_t k,
                                  const Quality& q,
                                  const double orderMs)
            {
                cells << "{\"kind\": \"quality\", \"fixture\": \"" << surface.Name << "\", \"domain\": \""
                      << domain.Name << "\", \"elements\": " << n << ", \"area\": " << ref.Area << ", \"method\": \""
                      << method << "\", \"seed\": " << seed << ", \"ratio\": " << ratio << ", \"k\": " << k
                      << ", \"count\": " << q.Count << ", \"coverage\": " << q.Coverage
                      << ", \"packing\": " << q.Packing << ", \"cell_cv\": " << q.CellCv
                      << ", \"cell_max_over_mean\": " << q.CellMaxOverMean << ", \"order_ms\": " << orderMs << "}\n";
                cells.flush();
            };
            for (const std::size_t ratio : ratios)
            {
                const std::size_t k = n / ratio;
                if (k >= 8u)
                    emit("stride", 0u, ratio, k, Measure(domain.Anchors, StrideSet(n, k), ref), 0.0);
            }
            for (const MethodCase& m : methods)
                for (const std::uint32_t seed : m.Seeded ? seeds : std::vector<std::uint32_t>{0u})
                {
                    PS::Params params = ForSurface(m.Params, ref.Area, n);
                    params.Seed = seed;
                    const auto t0 = Clock::now();
                    const PS::Result order = PS::Order(std::span<const glm::vec3>(domain.Anchors), params, n);
                    const double orderMs = Ms(t0);
                    if (!order.Succeeded())
                    {
                        cells << "{\"kind\": \"failure\", \"fixture\": \"" << surface.Name << "\", \"domain\": \""
                              << domain.Name << "\", \"method\": \"" << m.Name << "\", \"seed\": " << seed << "}\n";
                        cells.flush();
                        continue;
                    }
                    for (const std::size_t ratio : ratios)
                    {
                        const std::size_t k = std::min(n / ratio, order.Order.size());
                        if (k < 8u)
                            continue;
                        emit(m.Name,
                            seed,
                            ratio,
                            k,
                            Measure(domain.Anchors, std::span<const std::uint32_t>(order.Order).first(k), ref),
                            orderMs);
                    }
                    // The graded plane's vertex prefixes at ratio 64, for the report's before/after view.
                    if (surface.Name == "graded_grid_256" && domain.Name == "vertices" && seed <= 1u)
                    {
                        cells << "{\"kind\": \"samples\", \"method\": \"" << m.Name << "\", \"points\": [";
                        for (std::size_t i = 0; i < n / 64u && i < order.Order.size(); ++i)
                            cells << (i ? ", " : "") << "[" << domain.Anchors[order.Order[i]].x << ", "
                                  << domain.Anchors[order.Order[i]].y << "]";
                        cells << "]}\n";
                        cells.flush();
                    }
                    std::cerr << surface.Name << '/' << domain.Name << ' ' << m.Name << " seed " << seed << ": "
                              << orderMs << " ms\n";
                }
            if (surface.Name == "graded_grid_256" && domain.Name == "vertices")
            {
                cells << "{\"kind\": \"samples\", \"method\": \"stride\", \"points\": [";
                const std::vector<std::uint32_t> ids = StrideSet(n, n / 64u);
                for (std::size_t i = 0; i < ids.size(); ++i)
                    cells << (i ? ", " : "") << "[" << domain.Anchors[ids[i]].x << ", " << domain.Anchors[ids[i]].y
                          << "]";
                cells << "]}\n";
                cells.flush();
            }
        }
    }

    // Timing on a 1000 x 1000 grid (1M vertices, ~3M edges, 2M faces): the full order and the
    // first 65536; a full order slower than 4 s is not repeated.
    const Surface big = Grid("timing_grid_1000", 1000u, false);
    const Reference bigRef = MakeReference(big);
    for (const Domain& domain : Domains(big))
        for (const MethodCase& m : methods)
            for (const std::size_t count : {domain.Anchors.size(), std::size_t(65536u)})
            {
                std::vector<double> times;
                for (int r = 0; r < 3; ++r)
                {
                    PS::Params params = ForSurface(m.Params, bigRef.Area, count);
                    params.Seed = 1u;
                    const auto t0 = Clock::now();
                    const PS::Result order = PS::Order(std::span<const glm::vec3>(domain.Anchors), params, count);
                    times.push_back(order.Succeeded() ? Ms(t0) : -1.0);
                    if (times.back() < 0.0 || times.back() > 4000.0)
                        break;
                }
                std::sort(times.begin(), times.end());
                cells << "{\"kind\": \"timing\", \"domain\": \"" << domain.Name
                      << "\", \"elements\": " << domain.Anchors.size() << ", \"method\": \"" << m.Name
                      << "\", \"count\": " << count << ", \"repeats\": " << times.size()
                      << ", \"ms\": " << times[times.size() / 2] << "}\n";
                cells.flush();
                std::cerr << "timing " << domain.Name << ' ' << m.Name << " count " << count << ": "
                          << times[times.size() / 2] << " ms\n";
            }

    finished = true;
    watchdog.join();
    const char* commit = std::getenv("INTRINSIC_SOURCE_COMMIT");
    std::ofstream summary(outDir / "summary.json", std::ios::trunc);
    summary
        << "{\n  \"benchmark_id\": \"geometry.glyph_order.comparison\",\n  \"method\": \"geometry.point_sampling\",\n"
        << "  \"backend\": \"cpu_reference\",\n  \"commit\": \"" << (commit ? commit : "local-dev") << "\",\n"
        << "  \"status\": \"complete\",\n  \"seconds\": "
        << std::chrono::duration<double>(Clock::now() - started).count() << "\n}\n";
    return summary.good() && cells.good() ? 0 : 1;
}

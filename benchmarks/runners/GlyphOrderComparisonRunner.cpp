// UI-081 opt-in comparison of Geometry.PointSampling orders as vector-field glyph anchors
// (geometry.glyph_order.comparison). Not a default CTest; run it on a Release build.
//
//   IntrinsicGlyphOrderComparison --self-test
//   IntrinsicGlyphOrderComparison <empty or new out dir> <mesh.obj>... [--budget-seconds S]
//
// Per mesh and element domain (vertices, edge midpoints, face centers) every method orders the
// anchors as the feature will: count = min(N, 65536), sample elimination's radius from the true
// area; the vector-field slider draws prefixes of that order. Quality is measured on the surface:
// each triangle's area goes 1/2 to its centroid and 1/6 to each corner (exact for linear
// functions), and every such reference point belongs to its nearest sample (3-D Euclidean). Per
// prefix of k samples: coverage = largest distance of a reference point to its sample, packing =
// smallest distance between two samples, both divided by the hexagonal spacing
// d = sqrt(2 A / (sqrt(3) k)); cell CV = std / mean of the reference area per sample. A prefix
// longer than the order is recorded as "short", a failed measurement as "measure_failed".
// "stride" is today's every-n-th row set (s = ceil(N / k)), a set per k rather than an order.
// Prefix sizes k = N / r at sqrt(2) steps, r = 4 .. 256, so prefixes fall between the levels of
// hierarchical methods too. Every order runs in a forked child with a deadline (quality: 60 s,
// timing: 6 s = 3x the operator's 2 s budget); a missed deadline is recorded as "timeout", a
// measured "too slow", never a silent gap. Probes: the same coverage on a 1-to-4 refined reference
// (FPS and sample elimination), and whether each method's capped order is a prefix of its full
// order. Timing on a 1M-vertex grid keeps every repeat; the capped count runs first and the full
// count only while the capped median is within the budget. Writes cells.jsonl (one flushed line per cell) and summary.json; a watchdog
// ends the process with exit code 3 once the budget is spent (remaining cells unevaluated).
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iostream>
#include <limits>
#include <numbers>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <csignal>
#include <poll.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

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
    // The feature orders at most this many anchors per domain unless the cap is dropped.
    constexpr std::size_t kCap = 65536u;
    constexpr double kBudgetMs = 2000.0; // the operator's per-domain budget
    constexpr double kQualityDeadlineS = 60.0;
    constexpr double kTimingDeadlineS = 6.0;

    // Runs `work` in a forked child that writes its result words to a pipe; nullopt when the child
    // misses the deadline (it is killed) or fails. The parent's watchdog thread is not in the child.
    [[nodiscard]] std::optional<std::vector<std::uint32_t>> InChild(
        const double deadlineSeconds, const std::function<std::vector<std::uint32_t>()>& work)
    {
        int fds[2];
        if (pipe(fds) != 0)
            return std::nullopt;
        const pid_t pid = fork();
        if (pid < 0)
            return std::nullopt;
        if (pid == 0)
        {
            close(fds[0]);
            const std::vector<std::uint32_t> words = work();
            const std::uint64_t size = words.size();
            const auto put = [&](const void* data, std::size_t bytes)
            {
                const char* p = static_cast<const char*>(data);
                while (bytes > 0u)
                {
                    const ssize_t n = write(fds[1], p, bytes);
                    if (n <= 0)
                        _exit(1);
                    p += n;
                    bytes -= std::size_t(n);
                }
            };
            put(&size, sizeof(size));
            put(words.data(), words.size() * sizeof(std::uint32_t));
            _exit(0);
        }
        close(fds[1]);
        std::vector<char> bytes;
        const auto deadline = Clock::now() + std::chrono::duration<double>(deadlineSeconds);
        bool timedOut = false;
        for (;;)
        {
            const double left = std::chrono::duration<double, std::milli>(deadline - Clock::now()).count();
            if (left <= 0.0)
            {
                timedOut = true;
                break;
            }
            pollfd fd{fds[0], POLLIN, 0};
            if (poll(&fd, 1, int(std::min(left, 1000.0))) < 0)
            {
                timedOut = true;
                break;
            }
            if (!(fd.revents & (POLLIN | POLLHUP)))
                continue;
            char chunk[65536];
            const ssize_t n = read(fds[0], chunk, sizeof(chunk));
            if (n <= 0)
                break;
            bytes.insert(bytes.end(), chunk, chunk + n);
        }
        close(fds[0]);
        if (timedOut)
            kill(pid, SIGKILL);
        int status = 0;
        waitpid(pid, &status, 0);
        if (timedOut || !WIFEXITED(status) || WEXITSTATUS(status) != 0 || bytes.size() < sizeof(std::uint64_t))
            return std::nullopt;
        std::uint64_t size = 0;
        std::memcpy(&size, bytes.data(), sizeof(size));
        if (bytes.size() != sizeof(size) + size * sizeof(std::uint32_t))
            return std::nullopt;
        std::vector<std::uint32_t> words(size);
        std::memcpy(words.data(), bytes.data() + sizeof(size), size * sizeof(std::uint32_t));
        return words;
    }

    // The k = N / r prefix sizes at sqrt(2) steps from r = 4 to r = 256 (13 ratios).
    [[nodiscard]] std::vector<double> Ratios()
    {
        std::vector<double> r;
        for (int j = 0; j <= 12; ++j)
            r.push_back(4.0 * std::pow(2.0, 0.5 * j));
        return r;
    }

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

    // 1-to-4 midpoint subdivision: the same surface with a reference four times as dense.
    [[nodiscard]] Surface Refined(const Surface& s)
    {
        Surface r{s.Name + "_refined", s.Positions, {}};
        std::unordered_map<std::uint64_t, std::uint32_t> mid;
        const auto midpoint = [&](std::uint32_t a, std::uint32_t b)
        {
            const std::uint64_t key = (std::uint64_t(std::min(a, b)) << 32u) | std::max(a, b);
            const auto [it, added] = mid.emplace(key, std::uint32_t(r.Positions.size()));
            if (added)
                r.Positions.push_back(0.5f * (s.Positions[a] + s.Positions[b]));
            return it->second;
        };
        for (const auto& t : s.Triangles)
        {
            const std::uint32_t ab = midpoint(t[0], t[1]), bc = midpoint(t[1], t[2]), ca = midpoint(t[2], t[0]);
            r.Triangles.push_back({t[0], ab, ca});
            r.Triangles.push_back({ab, t[1], bc});
            r.Triangles.push_back({ca, bc, t[2]});
            r.Triangles.push_back({ab, bc, ca});
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
        double Coverage{0.0}; // / d_hex
        double Packing{0.0}; // / d_hex
        double CellCv{0.0};
        double CellMaxOverMean{0.0};
    };

    // nullopt when the measurement itself fails (fewer than two samples, tree or query failure).
    [[nodiscard]] std::optional<Quality> Measure(
        const std::vector<glm::vec3>& anchors, std::span<const std::uint32_t> ids, const Reference& ref)
    {
        if (ids.size() < 2)
            return std::nullopt;
        std::vector<glm::vec3> samples;
        samples.reserve(ids.size());
        for (const std::uint32_t id : ids)
            samples.push_back(anchors[id]);
        Geometry::KDTree tree;
        if (!tree.BuildFromPoints(samples))
            return std::nullopt;
        std::vector<double> cell(samples.size(), 0.0);
        std::vector<std::uint32_t> nn;
        double cover2 = 0.0;
        for (std::size_t i = 0; i < ref.Points.size(); ++i)
        {
            nn.clear();
            if (!tree.QueryKNN(ref.Points[i], 1u, nn, true) || nn.size() != 1u || nn[0] >= samples.size())
                return std::nullopt;
            const glm::dvec3 d = glm::dvec3(ref.Points[i]) - glm::dvec3(samples[nn[0]]);
            cover2 = std::max(cover2, glm::dot(d, d));
            cell[nn[0]] += ref.Weights[i];
        }
        double pack2 = std::numeric_limits<double>::infinity();
        for (std::size_t j = 0; j < samples.size(); ++j)
        {
            nn.clear();
            if (!tree.QueryKNN(samples[j], 2u, nn, true) || nn.size() != 2u)
                return std::nullopt;
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
        return Quality{.Count = ids.size(),
            .Coverage = std::sqrt(cover2) / hex,
            .Packing = std::sqrt(pack2) / hex,
            .CellCv = std::sqrt(var / k) / mean,
            .CellMaxOverMean = largest / mean};
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
        bool MayBeShort{false}; // ProgressivePoisson may accept fewer points than requested
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
            {"progressive_poisson_hapds", {.Method = PS::Method::ProgressivePoisson, .Poisson = hapds}, true, true},
            {"progressive_poisson_fast", {.Method = PS::Method::ProgressivePoisson, .Poisson = fast}, true, true},
            {"progressive_poisson_balanced",
                {.Method = PS::Method::ProgressivePoisson, .Poisson = balanced},
                true,
                true},
            {"sample_elimination", {.Method = PS::Method::SampleElimination, .ManifoldDimension = 2u}},
            {"tournament", {.Method = PS::Method::Tournament}},
        };
    }

    // Seed 0 keeps the library defaults; seeds 1..5 vary every random stream the method reads.
    [[nodiscard]] PS::Params WithSeed(PS::Params params, const std::uint32_t seed)
    {
        if (seed == 0u)
            return params;
        params.Seed = seed;
        params.GreedySeed ^= seed * 0x9e3779b9u;
        params.Poisson.GridOriginSeed += seed * 7919u;
        params.Poisson.ShuffleSeed ^= seed * 0x85ebca6bu;
        params.Poisson.PhaseOrderSeed ^= seed * 0xc2b2ae35u;
        return params;
    }

    // Sample elimination's default radius uses the bounding box; a curved surface needs its area.
    [[nodiscard]] PS::Params ForSurface(PS::Params params, const double area, const std::size_t count)
    {
        if (params.Method == PS::Method::SampleElimination && count > 0u)
            params.EliminationRadius = 2.0 * std::sqrt(area / (double(count) * 2.0 * std::sqrt(3.0)));
        return params;
    }

    [[nodiscard]] PS::Result OrderFor(
        const MethodCase& m, const std::uint32_t seed, const Domain& domain, const double area, const std::size_t count)
    {
        return PS::Order(
            std::span<const glm::vec3>(domain.Anchors), ForSurface(WithSeed(m.Params, seed), area, count), count);
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
        check(std::abs(MakeReference(Refined(grid)).Area - 1.0) < 1e-9,
            "refined area",
            MakeReference(Refined(grid)).Area);
        std::vector<std::uint32_t> sub;
        for (std::uint32_t j = 0; j < 65u; j += 4u)
            for (std::uint32_t i = 0; i < 65u; i += 4u)
                sub.push_back(j * 65u + i);
        const auto q = Measure(grid.Positions, sub, ref);
        check(q.has_value(), "subgrid measured", 0.0);
        if (!q)
            return 1;
        const double k = double(sub.size()), hex = std::sqrt(2.0 / (std::sqrt(3.0) * k));
        // Exact: the farthest reference points are the subgrid cell centres, half a cell diagonal away.
        check(std::abs(q->Coverage * hex - std::sqrt(2.0) * 2.0 / 64.0) < 1e-6,
            "subgrid coverage (world)",
            q->Coverage * hex);
        check(std::abs(q->Packing * hex - 4.0 / 64.0) < 1e-6, "subgrid packing (world)", q->Packing * hex);
        // Interior cells 1/256, edge cells half, corners a quarter (ties go to one side; allow 10%).
        const double mean = 1.0 / k;
        const double var = 225.0 * std::pow(1.0 / 256.0 - mean, 2.0) + 60.0 * std::pow(0.5 / 256.0 - mean, 2.0) +
            4.0 * std::pow(0.25 / 256.0 - mean, 2.0);
        const double expectedCv = std::sqrt(var / k) / mean;
        check(std::abs(q->CellCv - expectedCv) < 0.1 * expectedCv,
            "subgrid cell CV vs analytic " + std::to_string(expectedCv),
            q->CellCv);
        // Null probe: today's stride at k = 65 on the row-major grid draws one column (x = 0). Its
        // strip cells are all equal (CV near the boundary-only value) while coverage is the plane width.
        const std::vector<std::uint32_t> column = StrideSet(grid.Positions.size(), 65u);
        const auto c = Measure(grid.Positions, column, ref);
        bool oneColumn = true;
        for (const std::uint32_t id : column)
            oneColumn = oneColumn && grid.Positions[id].x == 0.0f;
        check(oneColumn, "stride k=65 is one column", double(column.size()));
        check(c && c->CellCv < 0.15, "column cell CV (does not reject it)", c ? c->CellCv : -1.0);
        check(c && c->Coverage > 5.0, "column coverage / d_hex (rejects it)", c ? c->Coverage : -1.0);
        // Duplicate samples have zero packing; one sample is not a measurement.
        const std::vector<std::uint32_t> dup{0u, 0u, 4u}, one{0u};
        const auto d = Measure(grid.Positions, dup, ref);
        check(d && d->Packing == 0.0, "duplicate packing", d ? d->Packing : -1.0);
        check(!Measure(grid.Positions, one, ref), "a single sample is not measured", 0.0);
        // Every method orders count = N into a duplicate-free, in-range order of full length (only
        // ProgressivePoisson may stop early); seeded methods change with the seed.
        const Domain vertices = Domains(grid)[0];
        for (const MethodCase& m : Methods())
        {
            const PS::Result r = OrderFor(m, m.Seeded ? 1u : 0u, vertices, ref.Area, vertices.Anchors.size());
            std::vector<std::uint32_t> sorted = r.Order;
            std::sort(sorted.begin(), sorted.end());
            const bool unique = std::adjacent_find(sorted.begin(), sorted.end()) == sorted.end() &&
                (sorted.empty() || sorted.back() < vertices.Anchors.size());
            const bool length = m.MayBeShort ? !r.Order.empty() : r.Order.size() == vertices.Anchors.size();
            check(r.Succeeded() && unique && length, m.Name + " order (length)", double(r.Order.size()));
            if (m.Seeded)
            {
                const PS::Result other = OrderFor(m, 2u, vertices, ref.Area, vertices.Anchors.size());
                check(other.Order != r.Order, m.Name + " seeds 1 and 2 differ", 0.0);
            }
        }
        return failures == 0 ? 0 : 1;
    }

    // JSON fields shared by every quality-like cell.
    [[nodiscard]] std::string CellHead(const std::string& kind,
        const Surface& surface,
        const Domain& domain,
        const Reference& ref,
        const std::string& method,
        const std::uint32_t seed)
    {
        std::ostringstream s;
        s.precision(9);
        s << "{\"kind\": \"" << kind << "\", \"fixture\": \"" << surface.Name << "\", \"domain\": \"" << domain.Name
          << "\", \"elements\": " << domain.Anchors.size() << ", \"area\": " << ref.Area << ", \"method\": \"" << method
          << "\", \"seed\": " << seed;
        return s.str();
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
    // Raw outputs are never overwritten: refuse a directory that already holds files.
    if (std::filesystem::exists(outDir) && !std::filesystem::is_empty(outDir))
    {
        std::cerr << outDir << " is not empty; choose a new run directory\n";
        return 2;
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
    const auto stop = [&](const int code)
    {
        finished = true;
        watchdog.join();
        return code;
    };

    std::vector<Surface> surfaces;
    for (const std::string& path : meshes)
    {
        Surface s;
        std::string error;
        if (!LoadObj(path, s, error))
        {
            std::cerr << path << ": " << error << '\n';
            return stop(4);
        }
        surfaces.push_back(std::move(s));
    }
    surfaces.push_back(Grid("regular_grid_256", 256u, false));
    surfaces.push_back(Grid("graded_grid_256", 256u, true));
    const std::vector<double> ratios = Ratios();
    const std::vector<std::uint32_t> seeds{1u, 2u, 3u, 4u, 5u};
    const std::vector<MethodCase> methods = Methods();

    for (const Surface& surface : surfaces)
    {
        const Reference ref = MakeReference(surface);
        const Reference refined = MakeReference(Refined(surface));
        for (const Domain& domain : Domains(surface))
        {
            const std::size_t n = domain.Anchors.size();
            const std::size_t count = std::min(n, kCap);
            const auto emit = [&](const std::string& method,
                                  const std::uint32_t seed,
                                  const double ratio,
                                  const std::size_t k,
                                  const std::optional<Quality>& q,
                                  const std::size_t produced,
                                  const double orderMs)
            {
                cells << CellHead("quality", surface, domain, ref, method, seed) << ", \"ratio\": " << ratio
                      << ", \"k\": " << k << ", \"produced\": " << produced << ", \"status\": \""
                      << (produced < k ? "short"
                                 : q   ? "ok"
                                       : "measure_failed")
                      << "\"";
                if (produced >= k && q)
                    cells << ", \"count\": " << q->Count << ", \"coverage\": " << q->Coverage
                          << ", \"packing\": " << q->Packing << ", \"cell_cv\": " << q->CellCv
                          << ", \"cell_max_over_mean\": " << q->CellMaxOverMean;
                cells << ", \"order_ms\": " << orderMs << "}\n";
                cells.flush();
            };
            for (const double ratio : ratios)
            {
                const std::size_t k = std::size_t(double(n) / ratio);
                if (k < 8u)
                    continue;
                const std::vector<std::uint32_t> ids = StrideSet(n, k);
                emit("stride", 0u, ratio, k, Measure(domain.Anchors, ids, ref), k, 0.0);
            }
            for (const MethodCase& m : methods)
                for (const std::uint32_t seed : m.Seeded ? seeds : std::vector<std::uint32_t>{0u})
                {
                    const auto t0 = Clock::now();
                    // Words: 1 + order on success, {0} when the method reports a failure.
                    const auto child = InChild(kQualityDeadlineS,
                        [&]
                        {
                            const PS::Result r = OrderFor(m, seed, domain, ref.Area, count);
                            std::vector<std::uint32_t> words{r.Succeeded() ? 1u : 0u};
                            if (r.Succeeded())
                                words.insert(words.end(), r.Order.begin(), r.Order.end());
                            return words;
                        });
                    const double orderMs = Ms(t0);
                    if (!child || child->empty() || child->front() != 1u)
                    {
                        cells << CellHead(child ? "failure" : "timeout", surface, domain, ref, m.Name, seed)
                              << ", \"deadline_s\": " << kQualityDeadlineS << "}\n";
                        cells.flush();
                        std::cerr << surface.Name << '/' << domain.Name << ' ' << m.Name << " seed " << seed << ": "
                                  << (child ? "failed" : "timeout") << '\n';
                        continue;
                    }
                    struct
                    {
                        std::vector<std::uint32_t> Order;
                    } order{{child->begin() + 1, child->end()}};
                    for (const double ratio : ratios)
                    {
                        const std::size_t k = std::size_t(double(n) / ratio);
                        if (k < 8u)
                            continue;
                        const std::size_t produced = std::min(k, order.Order.size());
                        emit(m.Name,
                            seed,
                            ratio,
                            k,
                            produced < k
                                ? std::nullopt
                                : Measure(domain.Anchors, std::span<const std::uint32_t>(order.Order).first(k), ref),
                            produced,
                            orderMs);
                        // Reference resolution: the same prefix against a 4x denser reference.
                        if (seed == 0u && produced >= k &&
                            (m.Name == "farthest_point" || m.Name == "sample_elimination"))
                        {
                            const auto base =
                                Measure(domain.Anchors, std::span<const std::uint32_t>(order.Order).first(k), ref);
                            const auto fine =
                                Measure(domain.Anchors, std::span<const std::uint32_t>(order.Order).first(k), refined);
                            cells << CellHead("refinement", surface, domain, ref, m.Name, seed)
                                  << ", \"ratio\": " << ratio << ", \"k\": " << k
                                  << ", \"coverage\": " << (base ? base->Coverage : -1.0)
                                  << ", \"coverage_refined\": " << (fine ? fine->Coverage : -1.0) << "}\n";
                            cells.flush();
                        }
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

    // Prefix stability: dropping the cap is only sound when the capped order is a prefix of the full one.
    {
        const Surface grid = Grid("regular_grid_256", 256u, false);
        const Reference ref = MakeReference(grid);
        const Domain vertices = Domains(grid)[0];
        for (const MethodCase& m : methods)
        {
            const auto stable = InChild(kQualityDeadlineS,
                [&]
                {
                    const PS::Result full =
                        OrderFor(m, m.Seeded ? 1u : 0u, vertices, ref.Area, vertices.Anchors.size());
                    const PS::Result capped = OrderFor(m, m.Seeded ? 1u : 0u, vertices, ref.Area, 4096u);
                    return std::vector<std::uint32_t>{full.Succeeded() && capped.Succeeded() &&
                        full.Order.size() >= capped.Order.size() &&
                        std::equal(capped.Order.begin(), capped.Order.end(), full.Order.begin())};
                });
            cells << "{\"kind\": \"prefix_stability\", \"method\": \"" << m.Name
                  << "\", \"stable\": " << (stable && !stable->empty() && stable->front() == 1u ? "true" : "false")
                  << ", \"status\": \"" << (stable ? "ok" : "timeout") << "\"}\n";
            cells.flush();
        }
    }

    // Timing on a 1000 x 1000 grid (1M vertices, ~3M edges, 2M faces): the capped order first,
    // then the full one only while the capped median is within the budget. Each repeat runs in a
    // child (measured inside it) with a 6 s deadline; a missed deadline or failure ends the repeats.
    const Surface big = Grid("timing_grid_1000", 1000u, false);
    const Reference bigRef = MakeReference(big);
    for (const Domain& domain : Domains(big))
        for (const MethodCase& m : methods)
        {
            double cappedMedian = std::numeric_limits<double>::infinity();
            for (const std::size_t count : {kCap, domain.Anchors.size()})
            {
                const bool full = count == domain.Anchors.size();
                std::string status = "ok";
                std::vector<double> times;
                if (full && !(cappedMedian <= kBudgetMs))
                    status = "skipped";
                for (int r = 0; r < 3 && status == "ok"; ++r)
                {
                    const auto words = InChild(kTimingDeadlineS,
                        [&]
                        {
                            const auto t0 = Clock::now();
                            const PS::Result order = OrderFor(m, m.Seeded ? 1u : 0u, domain, bigRef.Area, count);
                            const float ms = float(Ms(t0));
                            std::uint32_t bits = 0;
                            std::memcpy(&bits, &ms, sizeof(bits));
                            return std::vector<std::uint32_t>{order.Succeeded() ? 1u : 0u, bits};
                        });
                    if (!words || words->size() != 2u)
                        status = "timeout";
                    else if ((*words)[0] != 1u)
                        status = "failed";
                    else
                    {
                        float ms = 0.0f;
                        std::memcpy(&ms, &(*words)[1], sizeof(ms));
                        times.push_back(double(ms));
                    }
                }
                if (!full && status == "ok" && !times.empty())
                {
                    std::vector<double> sorted = times;
                    std::sort(sorted.begin(), sorted.end());
                    cappedMedian = sorted.size() % 2
                        ? sorted[sorted.size() / 2]
                        : 0.5 * (sorted[sorted.size() / 2 - 1] + sorted[sorted.size() / 2]);
                }
                cells << "{\"kind\": \"timing\", \"domain\": \"" << domain.Name
                      << "\", \"elements\": " << domain.Anchors.size() << ", \"method\": \"" << m.Name
                      << "\", \"count\": " << count << ", \"status\": \"" << status
                      << "\", \"deadline_s\": " << kTimingDeadlineS << ", \"ms\": [";
                for (std::size_t i = 0; i < times.size(); ++i)
                    cells << (i ? ", " : "") << times[i];
                cells << "]}\n";
                cells.flush();
                std::cerr << "timing " << domain.Name << ' ' << m.Name << " count " << count << ": " << status
                          << (times.empty() ? 0.0 : times.front()) << " ms\n";
            }
        }

    finished = true;
    watchdog.join();
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    const char* commit = std::getenv("INTRINSIC_SOURCE_COMMIT");
    std::ofstream summary(outDir / "summary.json", std::ios::trunc);
    summary
        << "{\n  \"benchmark_id\": \"geometry.glyph_order.comparison\",\n  \"method\": \"geometry.point_sampling\",\n"
        << "  \"backend\": \"cpu_reference\",\n  \"commit\": \"" << (commit ? commit : "local-dev") << "\",\n"
        << "  \"status\": \"complete\",\n  \"seconds\": "
        << std::chrono::duration<double>(Clock::now() - started).count() << ",\n  \"peak_rss_kib\": " << usage.ru_maxrss
        << "\n}\n";
    return summary.good() && cells.good() ? 0 : 1;
}

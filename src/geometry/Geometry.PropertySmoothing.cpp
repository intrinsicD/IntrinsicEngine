// Graph filters share a fixed, nonnegative neighborhood and operate on all channels together.
module;
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include <glm/glm.hpp>
module Geometry.Smoothing;
import Geometry.PointLBVH;
import Geometry.Sparse;

namespace Geometry::Smoothing
{
    bool ValidatePropertyFilterParams(const PropertyFilterParams& p) noexcept
    {
        return p.Method <= PropertyFilter::Implicit && p.Laplacian <= PropertyLaplacian::LumpedMass &&
            p.Solver <= PropertySolver::ConjugateGradient &&
            (p.Laplacian != PropertyLaplacian::LumpedMass || p.Method == PropertyFilter::Implicit) &&
            std::isfinite(p.TimeStep) && p.TimeStep > 0 &&
            std::isfinite(p.SolverTolerance) && p.SolverTolerance > 0 && p.SolverTolerance < 1 &&
            p.MaxSolverIterations > 0 && p.MaxSolverIterations <= 100000 &&
            p.Iterations >= 1 && p.Iterations <= 10000 &&
            std::isfinite(p.Lambda) && p.Lambda > 0 && p.Lambda <= 1 &&
            std::isfinite(p.Mu) && p.Mu >= -1 && p.Mu < 0 &&
            std::isfinite(p.HeatTime) && p.HeatTime > 0 && p.HeatTime <= 1000 &&
            std::isfinite(p.RangeSigma) && p.RangeSigma > 0;
    }

    std::optional<std::vector<PropertyEdge>> BuildPropertyNeighborhood(
        std::span<const glm::vec3> positions, std::uint32_t k,
        PropertyWeight weight, double sigma)
    {
        if (!k || k > 1024 || positions.empty() || positions.size() > std::numeric_limits<std::uint32_t>::max() || weight > PropertyWeight::InverseDistance ||
            !std::isfinite(sigma) || sigma <= 0 ||
            !std::ranges::all_of(positions, PointLBVH::ValidPoint)) return {};
        std::vector<PropertyEdge> edges;
        edges.reserve(positions.size() * k);
        PointLBVH::Index index;
        if (!index.Build(positions)) return {};
        for (std::uint32_t i = 0; i < positions.size(); ++i)
            for (const auto neighbor : index.KNearest(positions[i], k, i))
            {
                const auto delta = glm::dvec3(positions[i]) - glm::dvec3(positions[neighbor.Index]);
                const double distance = glm::length(delta);
                const double w = weight == PropertyWeight::Gaussian ? std::exp(-0.5 * (distance / sigma) * (distance / sigma))
                    : weight == PropertyWeight::InverseDistance ? 1.0 / std::max(distance, sigma * 1e-12) : 1.0;
                if (!std::isfinite(w)) return {};
                edges.push_back({std::min<std::size_t>(i, neighbor.Index), std::max<std::size_t>(i, neighbor.Index), w});
            }
        // Mutual neighbors produce the same pair twice with bitwise-equal weights; keep one, ordered by pair.
        const auto key = [](const PropertyEdge& e) { return std::pair{e.A, e.B}; };
        std::ranges::sort(edges, {}, key);
        const auto duplicates = std::ranges::unique(edges, {}, key);
        edges.erase(duplicates.begin(), duplicates.end());
        return edges;
    }

    PropertyFilterResult FilterProperty(std::span<const double> input, std::size_t channels,
        std::span<const PropertyEdge> edges, const PropertyFilterParams& p,
        std::span<const std::size_t> fixedRows, std::span<const double> lumpedMass)
    {
        PropertyFilterResult result;
        const auto fail = [&](const char* diagnostic) {
            result.Success = false;
            result.Values.clear();
            result.Diagnostic = diagnostic;
            return result;
        };
        if (!ValidatePropertyFilterParams(p) || channels < 1 || channels > 4 || input.empty() || input.size() % channels)
            return fail("Invalid filter parameters or property shape.");
        if (!std::ranges::all_of(input, [](double x) { return std::isfinite(x); }))
            return fail("Property contains non-finite live values.");
        const std::size_t count = input.size() / channels;
        std::vector<bool> fixed(count, false);
        for (auto row : fixedRows)
        {
            if (row >= count || fixed[row]) return fail("Invalid or duplicate fixed row.");
            fixed[row] = true;
        }
        if (p.Laplacian == PropertyLaplacian::LumpedMass &&
            (lumpedMass.size() != count || !std::ranges::all_of(lumpedMass,
                [](double m) { return std::isfinite(m) && m > 0; })))
            return fail("Lumped-mass Laplacian requires positive finite row masses.");
        std::vector<double> degree(count, 0.0), weights;
        weights.reserve(edges.size());
        for (const auto& edge : edges)
        {
            if (edge.A >= count || edge.B >= count || edge.A == edge.B || !std::isfinite(edge.Weight) || edge.Weight < 0)
                return fail("Invalid nonnegative property neighborhood.");
            degree[edge.A] += edge.Weight;
            degree[edge.B] += edge.Weight;
            weights.push_back(edge.Weight);
        }
        if (!std::ranges::all_of(degree, [](double x) { return std::isfinite(x); }))
            return fail("Neighborhood degree overflow.");
        std::vector<bool> isolated(count);
        for (std::size_t i = 0; i < count; ++i) isolated[i] = degree[i] == 0;
        const double maxDegree = *std::max_element(degree.begin(), degree.end());
        const double rate = p.Laplacian == PropertyLaplacian::RandomWalk ? 1.0 : maxDegree;
        std::vector<double> values(input.begin(), input.end()), next(input.size()), delta(input.size());
        const auto apply = [&](const std::vector<double>& source, std::vector<double>& target, double step) {
            std::fill(delta.begin(), delta.end(), 0.0);
            for (std::size_t e = 0; e < edges.size(); ++e)
                for (std::size_t c = 0; c < channels; ++c)
                {
                    const auto a = edges[e].A * channels + c, b = edges[e].B * channels + c;
                    const double difference = weights[e] * (source[b] - source[a]);
                    delta[a] += difference;
                    delta[b] -= difference;
                }
            for (std::size_t i = 0; i < count; ++i)
                for (std::size_t c = 0; c < channels; ++c)
                {
                    const auto j = i * channels + c;
                    const double divisor = p.Laplacian == PropertyLaplacian::RandomWalk ? degree[i] : 1.0;
                    target[j] = source[j] + (!fixed[i] && divisor > 0 ? step * delta[j] / divisor : 0.0);
                }
            ++result.OperatorApplications;
        };
        if (p.Method == PropertyFilter::Implicit)
        {
            // Solve (M + dt L) x = M b. Random walk uses M = D, preserving symmetry;
            // combinatorial uses unit mass; custom mass supports area-aware fairing.
            // The operator is fixed for the run, so it is assembled once with fixed
            // rows eliminated: they become identity rows and their coupling moves
            // into the free right-hand side, keeping the system SPD.
            std::vector<double> mass(count, 1.0), diagonal(count);
            for (std::size_t i = 0; i < count; ++i)
            {
                if (!isolated[i]) mass[i] = p.Laplacian == PropertyLaplacian::RandomWalk
                    ? degree[i] : p.Laplacian == PropertyLaplacian::LumpedMass ? lumpedMass[i] : 1.0;
                diagonal[i] = fixed[i] ? 1.0 : mass[i];
            }
            struct Coupling { std::size_t Free, Fixed; double Weight; };
            std::vector<Coupling> couplings;
            Sparse::SparseBuilder builder(count, count);
            builder.Reserve(2 * edges.size() + count);
            for (const auto& edge : edges)
            {
                const double w = p.TimeStep * edge.Weight;
                for (const auto [row, col] : {std::pair{edge.A, edge.B}, std::pair{edge.B, edge.A}})
                {
                    if (fixed[row]) continue;
                    diagonal[row] += w;
                    if (fixed[col]) couplings.push_back({row, col, w});
                    else builder.Add(row, col, -w);
                }
            }
            for (std::size_t i = 0; i < count; ++i) builder.Add(i, i, diagonal[i]);
            const auto system = builder.Build();
            if (!system.Valid || !std::ranges::all_of(diagonal, [](double x) { return std::isfinite(x); }))
                return fail("Unable to assemble implicit system.");
            Sparse::SparseLLT cholesky;
            const bool direct = p.Solver == PropertySolver::Direct && cholesky.factor(system.Matrix).Succeeded();
            const Sparse::CGParams solver{p.MaxSolverIterations, p.SolverTolerance};
            std::vector<double> rhs(count), solved(count);
            for (std::size_t iteration = 0; iteration < p.Iterations; ++iteration)
                for (std::size_t c = 0; c < channels; ++c)
                {
                    for (std::size_t i = 0; i < count; ++i)
                    {
                        solved[i] = fixed[i] ? input[i * channels + c] : values[i * channels + c];
                        rhs[i] = fixed[i] ? solved[i] : mass[i] * solved[i];
                    }
                    for (const auto& coupling : couplings)
                        rhs[coupling.Free] += coupling.Weight * input[coupling.Fixed * channels + c];
                    if (direct)
                    {
                        if (!cholesky.solve(rhs, solved).Succeeded())
                            return fail("Implicit Cholesky solve failed; no property was changed.");
                        ++result.OperatorApplications;
                    }
                    else
                    {
                        const auto status = Sparse::SolveCG(system.Matrix, rhs, solved, solver);
                        result.OperatorApplications += status.Iterations + 1;
                        if (!status.Converged) return fail("Implicit solver failed to converge; no property was changed.");
                    }
                    for (std::size_t i = 0; i < count; ++i) values[i * channels + c] = solved[i];
                }
            result.Diagnostic = direct ? "cpu_sparse_cholesky"
                : p.Solver == PropertySolver::Direct ? "cpu_reference (Cholesky factorization failed; used CG)" : "cpu_reference";
        }
        else if (p.Method == PropertyFilter::SpectralHeat && rate > 0)
        {
            // exp(-t L) = exp(-a) sum a^k/k! (I-L/rate)^k, a=t*rate.
            // Split a into <=1 intervals so all terms are nonnegative and well conditioned.
            const double total = p.HeatTime * rate;
            if (!std::isfinite(total) || total > 10000) return fail("Heat time times maximum degree exceeds 10000; reduce time or use random-walk Laplacian.");
            const auto steps = static_cast<std::size_t>(std::max(1.0, std::ceil(total)));
            const double a = total / double(steps);
            std::vector<double> term(input.size()), sum(input.size());
            for (std::size_t iteration = 0; iteration < p.Iterations; ++iteration)
                for (std::size_t split = 0; split < steps; ++split)
                {
                    term = values;
                    double coefficient = std::exp(-a), mass = coefficient;
                    for (std::size_t j = 0; j < values.size(); ++j) sum[j] = coefficient * term[j];
                    for (unsigned k = 1; k <= 18; ++k)
                    {
                        apply(term, next, 1.0 / rate);
                        term.swap(next);
                        coefficient *= a / k;
                        mass += coefficient;
                        for (std::size_t j = 0; j < values.size(); ++j) sum[j] += coefficient * term[j];
                    }
                    for (std::size_t j = 0; j < values.size(); ++j) values[j] = sum[j] / mass;
                }
        }
        else if (rate > 0)
            for (std::size_t iteration = 0; iteration < p.Iterations; ++iteration)
            {
                if (p.Method == PropertyFilter::Bilateral)
                {
                    std::fill(degree.begin(), degree.end(), 1.0);
                    for (std::size_t e = 0; e < edges.size(); ++e)
                    {
                        double norm = 0;
                        for (std::size_t c = 0; c < channels; ++c)
                        {
                            const double d = (values[edges[e].A * channels + c] - values[edges[e].B * channels + c]) / p.RangeSigma;
                            norm += d * d;
                        }
                        weights[e] = edges[e].Weight * std::exp(-0.5 * norm);
                        degree[edges[e].A] += weights[e];
                        degree[edges[e].B] += weights[e];
                    }
                }
                // Combinatorial explicit steps use the maximum-degree stability bound.
                const double scale = p.Laplacian == PropertyLaplacian::Combinatorial ? rate : 1.0;
                apply(values, next, p.Lambda / scale);
                values.swap(next);
                if (p.Method == PropertyFilter::Taubin)
                {
                    apply(values, next, p.Mu / scale);
                    values.swap(next);
                }
            }
        for (std::size_t i = 0; i < count; ++i)
            if (isolated[i] || fixed[i])
                for (std::size_t c = 0; c < channels; ++c) values[i * channels + c] = input[i * channels + c];
        if (!std::ranges::all_of(values, [](double x) { return std::isfinite(x); }))
            return fail("Filter produced non-finite values; no property was changed.");
        result.Success = true;
        result.Values = std::move(values);
        if (result.Diagnostic.empty()) result.Diagnostic = "cpu_reference";
        return result;
    }
}

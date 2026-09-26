// Variational fitting: iteratively reweighted least squares for Huber/L1 penalties, a primal-dual
// active set for per-channel bounds and a log-space bisection on the data weight for a target noise
// level. Every inner step is a harmonic Solve with reweighted edges and soft rows.
module;
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>
module Geometry.HarmonicField;

namespace Geometry::HarmonicField
{
    namespace
    {
        using Smoothing::FitPenalty;

        double Rho(FitPenalty penalty, double r, double delta)
        {
            switch (penalty)
            {
            case FitPenalty::Huber: return r <= delta ? r * r : 2.0 * delta * r - delta * delta;
            case FitPenalty::L1: return r >= delta ? r : r * r / (2.0 * delta) + 0.5 * delta;
            default: return r * r;
            }
        }

        // IRLS weight rho'(r) / (2 r): the quadratic psi r^2 majorizes rho and touches it at r.
        double Psi(FitPenalty penalty, double r, double delta)
        {
            switch (penalty)
            {
            case FitPenalty::Huber: return r <= delta ? 1.0 : delta / r;
            case FitPenalty::L1: return 0.5 / std::max(r, delta);
            default: return 1.0;
            }
        }

        struct Problem
        {
            std::span<const double> F;
            std::size_t Channels{}, Count{};
            std::span<const Smoothing::PropertyEdge> Edges;
            const Smoothing::PropertyFilterParams* P{};
            std::vector<bool> Fixed;
            std::vector<std::size_t> FixedRows;
            std::vector<double> Mass, Radius; // Radius empty without bounds
            double Scale{1.0};                // value range, for the relative change test
            std::size_t Solves{}, ActiveBounds{};
            std::string Error;

            double Distance(std::span<const double> x, std::size_t a, std::span<const double> y, std::size_t b) const
            {
                double sum = 0;
                for (std::size_t c = 0; c < Channels; ++c)
                {
                    const double d = x[a * Channels + c] - y[b * Channels + c];
                    sum += d * d;
                }
                return std::sqrt(sum);
            }

            double Energy(std::span<const double> u, double lambda) const
            {
                double energy = 0;
                for (const auto& e : Edges) energy += e.Weight * Rho(P->SmoothnessPenalty, Distance(u, e.A, u, e.B), P->PenaltyDelta);
                for (std::size_t i = 0; i < Count; ++i)
                    if (!Fixed[i]) energy += lambda * Mass[i] * Rho(P->DataPenalty, Distance(u, i, F, i), P->PenaltyDelta);
                return energy;
            }

            double RmsResidual(std::span<const double> u) const
            {
                double sum = 0, mass = 0;
                for (std::size_t i = 0; i < Count; ++i)
                    if (!Fixed[i])
                    {
                        const double r = Distance(u, i, F, i);
                        sum += Mass[i] * r * r;
                        mass += Mass[i];
                    }
                return mass > 0 ? std::sqrt(sum / mass) : 0.0;
            }

            // Minimizes sum w' |u_a - u_b|^2 + sum s |u_i - f_i|^2 under the fixed rows and bounds.
            std::optional<std::vector<double>> SolveQuadratic(std::span<const Smoothing::PropertyEdge> weighted,
                                                              std::span<const double> soft)
            {
                const Params harmonic{};
                if (Radius.empty())
                {
                    std::vector<SoftConstraint> softRows;
                    for (std::size_t i = 0; i < Count; ++i)
                        if (!Fixed[i]) softRows.push_back({i, soft[i]});
                    const auto solved = Solve(F, Channels, weighted, harmonic, FixedRows, softRows);
                    ++Solves;
                    if (!solved.Success) { Error = solved.Diagnostic; return {}; }
                    ActiveBounds = 0;
                    return solved.Values;
                }
                // Primal-dual active set per channel: rows at a bound become hard rows at that bound
                // and are released when the energy gradient points back into the feasible interval.
                // The reduced systems are M-matrices, for which the iteration converges finitely.
                std::vector<double> u(F.size()), target(Count), gradient(Count);
                std::vector<std::int8_t> active(Count);
                ActiveBounds = 0;
                for (std::size_t c = 0; c < Channels; ++c)
                {
                    std::ranges::fill(active, std::int8_t{0});
                    bool settled = false;
                    for (std::size_t round = 0; round < 2 * Count + 10 && !settled; ++round)
                    {
                        std::vector<std::size_t> hard = FixedRows;
                        std::vector<SoftConstraint> softRows;
                        for (std::size_t i = 0; i < Count; ++i)
                        {
                            target[i] = F[i * Channels + c] + active[i] * Radius[i];
                            if (Fixed[i]) continue;
                            if (active[i]) hard.push_back(i);
                            else softRows.push_back({i, soft[i]});
                        }
                        std::ranges::sort(hard);
                        const auto solved = Solve(target, 1, weighted, harmonic, hard, softRows);
                        ++Solves;
                        if (!solved.Success) { Error = solved.Diagnostic; return {}; }
                        const auto& x = solved.Values;
                        // Half gradient of the channel energy; zero on free rows up to the solve residual.
                        for (std::size_t i = 0; i < Count; ++i) gradient[i] = soft[i] * (x[i] - F[i * Channels + c]);
                        for (const auto& e : weighted)
                        {
                            const double d = e.Weight * (x[e.A] - x[e.B]);
                            gradient[e.A] += d;
                            gradient[e.B] -= d;
                        }
                        settled = true;
                        for (std::size_t i = 0; i < Count; ++i)
                        {
                            if (Fixed[i]) continue;
                            const double deviation = x[i] - F[i * Channels + c];
                            const double slack = 1e-12 * Scale;
                            std::int8_t next = active[i];
                            if (active[i] > 0 && gradient[i] > 0) next = 0;      // wants to move down
                            else if (active[i] < 0 && gradient[i] < 0) next = 0; // wants to move up
                            else if (!active[i] && deviation > Radius[i] + slack) next = 1;
                            else if (!active[i] && deviation < -Radius[i] - slack) next = -1;
                            if (next != active[i]) settled = false;
                            active[i] = next;
                        }
                        for (std::size_t i = 0; i < Count; ++i) u[i * Channels + c] = x[i];
                    }
                    if (!settled) { Error = "Bounded fit did not settle its active set; no property was changed."; return {}; }
                    ActiveBounds += static_cast<std::size_t>(std::ranges::count_if(active, [](auto a) { return a != 0; }));
                }
                return u;
            }

            // Reweighting iterations at a fixed data weight, starting from the input. Each step
            // minimizes the quadratic majorizer, so the energy never increases; convergence is
            // linear and slows as residuals on plateaus approach a small delta.
            std::optional<std::vector<double>> Fit(double lambda, std::size_t& iterations)
            {
                const bool quadratic = P->SmoothnessPenalty == FitPenalty::Quadratic && P->DataPenalty == FitPenalty::Quadratic;
                std::vector<double> u(F.begin(), F.end()), soft(Count);
                std::vector<Smoothing::PropertyEdge> weighted(Edges.begin(), Edges.end());
                for (iterations = 1; iterations <= P->MaxFitIterations; ++iterations)
                {
                    for (std::size_t e = 0; e < Edges.size(); ++e)
                        weighted[e].Weight = Edges[e].Weight *
                            Psi(P->SmoothnessPenalty, Distance(u, Edges[e].A, u, Edges[e].B), P->PenaltyDelta);
                    for (std::size_t i = 0; i < Count; ++i)
                        soft[i] = lambda * Mass[i] * Psi(P->DataPenalty, Distance(u, i, F, i), P->PenaltyDelta);
                    auto next = SolveQuadratic(weighted, soft);
                    if (!next) return {};
                    double change = 0;
                    for (std::size_t j = 0; j < u.size(); ++j) change = std::max(change, std::abs((*next)[j] - u[j]));
                    u = std::move(*next);
                    if (quadratic || change <= P->FitTolerance * Scale) return u;
                }
                --iterations;
                Error = "Reweighting did not converge in " + std::to_string(P->MaxFitIterations) +
                        " iterations; raise the iteration limit or the penalty delta. No property was changed.";
                return {};
            }
        };
    }

    FitResult FitProperty(std::span<const double> values, std::size_t channels,
                          std::span<const Smoothing::PropertyEdge> edges,
                          const Smoothing::PropertyFilterParams& params,
                          std::span<const std::size_t> fixedRows, std::span<const double> lumpedMass,
                          std::span<const double> boundRadii)
    {
        using Smoothing::FitBound;
        using Smoothing::PropertyLaplacian;
        FitResult result;
        const auto fail = [&](std::string diagnostic) {
            result.Success = false;
            result.Values.clear();
            result.Diagnostic = std::move(diagnostic);
            return result;
        };
        if (params.Method != Smoothing::PropertyFilter::VariationalFit || !Smoothing::ValidatePropertyFilterParams(params) ||
            channels == 0 || values.empty() || values.size() % channels)
            return fail("Invalid variational-fit parameters or value shape.");
        if (!std::ranges::all_of(values, [](double x) { return std::isfinite(x); }))
            return fail("Property contains non-finite live values.");
        Problem problem{.F = values, .Channels = channels, .Count = values.size() / channels, .Edges = edges, .P = &params};
        const auto count = problem.Count;
        problem.Fixed.assign(count, false);
        for (const auto row : fixedRows)
        {
            if (row >= count || problem.Fixed[row]) return fail("Invalid or duplicate fixed row.");
            problem.Fixed[row] = true;
        }
        for (std::size_t row = 0; row < count; ++row)
            if (problem.Fixed[row]) problem.FixedRows.push_back(row);
        std::vector<double> degree(count, 0.0);
        double totalWeight = 0;
        for (const auto& e : edges)
        {
            if (e.A >= count || e.B >= count || e.A == e.B || !std::isfinite(e.Weight) || e.Weight < 0)
                return fail("Invalid nonnegative property neighborhood.");
            degree[e.A] += e.Weight;
            degree[e.B] += e.Weight;
            totalWeight += e.Weight;
        }
        if (params.Laplacian == PropertyLaplacian::LumpedMass)
        {
            if (lumpedMass.size() != count || !std::ranges::all_of(lumpedMass, [](double m) { return std::isfinite(m) && m > 0; }))
                return fail("Lumped-mass fitting requires positive finite row masses.");
            problem.Mass.assign(lumpedMass.begin(), lumpedMass.end());
        }
        else
        {
            problem.Mass.assign(count, 1.0);
            if (params.Laplacian == PropertyLaplacian::RandomWalk)
                for (std::size_t i = 0; i < count; ++i)
                    if (degree[i] > 0) problem.Mass[i] = degree[i];
        }
        if (params.Bound == FitBound::Uniform) problem.Radius.assign(count, params.BoundRadius);
        else if (params.Bound == FitBound::PerRow)
        {
            if (boundRadii.size() != count || !std::ranges::all_of(boundRadii, [](double r) { return std::isfinite(r) && r >= 0; }))
                return fail("Per-row bounds need one finite nonnegative radius per row.");
            problem.Radius.assign(boundRadii.begin(), boundRadii.end());
        }
        double range = 0;
        for (std::size_t c = 0; c < channels; ++c)
        {
            double lo = values[c], hi = values[c];
            for (std::size_t i = 0; i < count; ++i)
            {
                lo = std::min(lo, values[i * channels + c]);
                hi = std::max(hi, values[i * channels + c]);
            }
            range = std::max(range, hi - lo);
        }
        problem.Scale = range > 0 ? range : 1.0;

        std::size_t iterations = 0;
        double lambda = params.FitWeight;
        std::optional<std::vector<double>> fitted;
        if (params.Fidelity == Smoothing::FitFidelity::FixedWeight)
            fitted = problem.Fit(lambda, iterations);
        else
        {
            // The residual decreases with the data weight (strictly for quadratic penalties), so
            // bisect log(lambda) within eight decades either side of the balanced weight.
            double freeMass = 0;
            for (std::size_t i = 0; i < count; ++i) if (!problem.Fixed[i]) freeMass += problem.Mass[i];
            const double balanced = totalWeight > 0 && freeMass > 0 ? totalWeight / freeMass : 1.0;
            const double target = params.NoiseLevel;
            double lo = balanced * 1e-8, hi = balanced * 1e8;
            const auto at = [&](double weight) {
                lambda = weight;
                fitted = problem.Fit(weight, iterations);
                return fitted ? problem.RmsResidual(*fitted) : -1.0;
            };
            if (const double r = at(lo); r < 0) return fail(problem.Error);
            else if (r <= target) result.Stats.NoiseTargetClamped = r < target * (1 - 1e-3);
            else if (const double rh = at(hi); rh < 0) return fail(problem.Error);
            else if (rh >= target) result.Stats.NoiseTargetClamped = rh > target * (1 + 1e-3);
            else
                for (std::size_t step = 0; step < 100; ++step)
                {
                    const double mid = std::sqrt(lo * hi);
                    const double r = at(mid);
                    if (r < 0) return fail(problem.Error);
                    if (std::abs(r - target) <= 1e-4 * target || hi / lo < 1 + 1e-12) break;
                    (r > target ? lo : hi) = mid;
                }
        }
        if (!fitted) return fail(problem.Error);
        if (!std::ranges::all_of(*fitted, [](double x) { return std::isfinite(x); }))
            return fail("Variational fit produced non-finite values; no property was changed.");
        result.Values = std::move(*fitted);
        result.Stats.ReweightIterations = iterations;
        result.Stats.Solves = problem.Solves;
        result.Stats.ActiveBounds = problem.ActiveBounds;
        result.Stats.FitWeight = lambda;
        result.Stats.RmsResidual = problem.RmsResidual(result.Values);
        result.Stats.Energy = problem.Energy(result.Values, lambda);
        result.Success = true;
        result.Diagnostic = "cpu_reference_sparse_cholesky";
        return result;
    }
}

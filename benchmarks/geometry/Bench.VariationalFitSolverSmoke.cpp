#include "Bench.VariationalFitSolverSmoke.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>
#include <glm/glm.hpp>
import Geometry.HarmonicField;
namespace Intrinsic::Bench::Geometry
{
    VariationalFitSolverSmokeMetrics RunVariationalFitSolverSmoke()
    {
        namespace S = ::Geometry::Smoothing;
        namespace H = ::Geometry::HarmonicField;
        // A noisy unit step sampled on a line, connected by uniform 12-nearest neighbors: the kNN
        // total-variation case where reweighting needs hundreds of refactorizations.
        constexpr std::size_t n = 256;
        std::vector<glm::vec3> points(n);
        std::vector<double> f(n);
        std::mt19937 rng(20260926u);
        for (std::size_t i = 0; i < n; ++i)
        {
            points[i] = {float(i) / float(n), 0.0f, 0.0f};
            const double noise = (double(rng()) / double(std::mt19937::max()) - 0.5) * 0.2;
            f[i] = (2 * i < n ? 0.0 : 1.0) + noise;
        }
        VariationalFitSolverSmokeMetrics metrics{.Succeeded = true};
        const auto edges = S::BuildPropertyNeighborhood(points, 12, S::PropertyWeight::Uniform, 1.0);
        if (!edges) return {};
        S::PropertyFilterParams p{.Method = S::PropertyFilter::VariationalFit};
        p.SmoothnessPenalty = S::FitPenalty::L1;
        p.FitWeight = 0.05;
        p.PenaltyDelta = 1e-6;
        p.FitTolerance = 1e-8;
        p.MaxFitIterations = 20000;
        const auto timed = [](auto&& run, double& milliseconds) {
            const auto start = std::chrono::steady_clock::now();
            auto result = run();
            milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            return result;
        };
        const auto reference = timed([&] { return H::FitProperty(f, 1, *edges, p); }, metrics.ReweightedRuntimeMilliseconds);
        p.FitAlgorithm = S::FitSolver::Admm;
        (void)H::FitProperty(f, 1, *edges, p); // warmup
        const auto admm = timed([&] { return H::FitProperty(f, 1, *edges, p); }, metrics.RuntimeMilliseconds);
        if (!reference.Success || !admm.Success) return {};
        for (std::size_t i = 0; i < n; ++i)
            metrics.MaxValueDelta = std::max(metrics.MaxValueDelta, std::abs(admm.Values[i] - reference.Values[i]));
        metrics.RelativeEnergyDelta = std::abs(admm.Stats.Energy - reference.Stats.Energy) / reference.Stats.Energy;
        metrics.AdmmIterations = admm.Stats.Iterations;
        metrics.ReweightedIterations = reference.Stats.Iterations;
        metrics.AdmmFactorizations = admm.Stats.Factorizations;
        metrics.ReweightedFactorizations = reference.Stats.Factorizations;

        // Undamped TV (delta = 0) of a clean step on a path: each 64-row plateau moves 1 / (2 * 64).
        constexpr std::size_t m = 128;
        std::vector<double> step(m, 0.0);
        std::fill(step.begin() + m / 2, step.end(), 1.0);
        std::vector<S::PropertyEdge> path;
        for (std::size_t i = 0; i + 1 < m; ++i) path.push_back({i, i + 1, 1.0});
        S::PropertyFilterParams exact{.Method = S::PropertyFilter::VariationalFit, .Laplacian = S::PropertyLaplacian::Combinatorial};
        exact.SmoothnessPenalty = S::FitPenalty::L1;
        exact.FitAlgorithm = S::FitSolver::Admm;
        exact.PenaltyDelta = 0;
        exact.FitTolerance = 1e-12;
        exact.MaxFitIterations = 100000;
        const auto tv = H::FitProperty(step, 1, path, exact);
        if (!tv.Success) return {};
        for (std::size_t i = 0; i < m; ++i)
            metrics.ExactTvStepError = std::max(metrics.ExactTvStepError,
                std::abs(tv.Values[i] - (i < m / 2 ? 1.0 / m : 1.0 - 1.0 / m)));
        // Non-local TGV keeps the affine part of the kNN line signal and does not staircase a noisy
        // ramp with a jump, where first-order TV does.
        S::PropertyFilterParams tgv{.Method = S::PropertyFilter::VariationalFit};
        tgv.SmoothnessPenalty = S::FitPenalty::L1;
        tgv.FitAlgorithm = S::FitSolver::Admm;
        tgv.SmoothnessOrder = S::FitOrder::Second;
        tgv.PenaltyDelta = 0;
        tgv.FitTolerance = 1e-10;
        tgv.MaxFitIterations = 100000;
        std::vector<double> positions, ramp(n), clean(n), noisy(n);
        for (std::size_t i = 0; i < n; ++i)
        {
            positions.insert(positions.end(), {double(points[i].x), 0.0, 0.0});
            ramp[i] = 3.0 * points[i].x - 1.0;
            clean[i] = points[i].x + (2 * i < n ? 0.0 : 1.0);
            noisy[i] = clean[i] + (f[i] - (2 * i < n ? 0.0 : 1.0)) * 0.5;
        }
        tgv.FitWeight = 0.01;
        const auto affine = H::FitProperty(ramp, 1, *edges, tgv, {}, {}, {}, positions);
        if (!affine.Success) return {};
        for (std::size_t i = 0; i < n; ++i) metrics.TgvAffineError = std::max(metrics.TgvAffineError, std::abs(affine.Values[i] - ramp[i]));
        tgv.FitWeight = 5.0;
        tgv.SecondOrderWeight = 4.0;
        const auto second = H::FitProperty(noisy, 1, *edges, tgv, {}, {}, {}, positions);
        tgv.SmoothnessOrder = S::FitOrder::First;
        const auto first = H::FitProperty(noisy, 1, *edges, tgv);
        if (!second.Success || !first.Success) return {};
        for (std::size_t i = 0; i < n; ++i)
        {
            metrics.TgvRampRmsError += (second.Values[i] - clean[i]) * (second.Values[i] - clean[i]) / double(n);
            metrics.TvRampRmsError += (first.Values[i] - clean[i]) * (first.Values[i] - clean[i]) / double(n);
        }
        metrics.TgvRampRmsError = std::sqrt(metrics.TgvRampRmsError);
        metrics.TvRampRmsError = std::sqrt(metrics.TvRampRmsError);
        metrics.Succeeded = metrics.MaxValueDelta <= 1e-4 && metrics.RelativeEnergyDelta <= 1e-6 &&
                            metrics.ExactTvStepError <= 1e-9 && metrics.AdmmFactorizations == 1 &&
                            metrics.TgvAffineError <= 1e-6 && metrics.TgvRampRmsError < metrics.TvRampRmsError;
        return metrics;
    }
}

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <vector>
#include <gtest/gtest.h>
import Geometry.HarmonicField;
namespace H = Geometry::HarmonicField;
namespace S = Geometry::Smoothing;
using Edge = S::PropertyEdge;
namespace
{
    std::vector<Edge> Path(std::size_t n, double w = 1)
    {
        std::vector<Edge> edges;
        for (std::size_t i = 0; i + 1 < n; ++i) edges.push_back({i, i + 1, w});
        return edges;
    }

    std::vector<Edge> RandomGraph(std::size_t n, std::mt19937& rng)
    {
        std::uniform_real_distribution<double> weight(0.2, 2.0);
        auto edges = Path(n);
        for (auto& e : edges) e.Weight = weight(rng);
        std::uniform_int_distribution<std::size_t> row(0, n - 1);
        for (std::size_t k = 0; k < n; ++k)
        {
            auto a = row(rng), b = row(rng);
            if (a == b || std::max(a, b) - std::min(a, b) == 1) continue;
            edges.push_back({std::min(a, b), std::max(a, b), weight(rng)});
        }
        return edges;
    }

    S::PropertyFilterParams Fit(S::FitPenalty smoothness, S::FitPenalty data, double lambda)
    {
        S::PropertyFilterParams p{.Method = S::PropertyFilter::VariationalFit, .Laplacian = S::PropertyLaplacian::Combinatorial};
        p.SmoothnessPenalty = smoothness;
        p.DataPenalty = data;
        p.FitWeight = lambda;
        p.FitTolerance = 1e-11;
        p.MaxFitIterations = 10000;
        return p;
    }

    // Independent evaluation of the documented energy with unit masses.
    double Rho(S::FitPenalty penalty, double r, double delta)
    {
        if (penalty == S::FitPenalty::Huber) return r <= delta ? r * r : 2 * delta * r - delta * delta;
        if (penalty == S::FitPenalty::L1) return r >= delta ? r : r * r / (2 * delta) + delta / 2;
        return r * r;
    }
    double Energy(const std::vector<double>& u, const std::vector<double>& f, std::size_t channels,
                  const std::vector<Edge>& edges, const S::PropertyFilterParams& p)
    {
        const auto norm = [&](const std::vector<double>& x, std::size_t a, const std::vector<double>& y, std::size_t b) {
            double sum = 0;
            for (std::size_t c = 0; c < channels; ++c) sum += (x[a * channels + c] - y[b * channels + c]) * (x[a * channels + c] - y[b * channels + c]);
            return std::sqrt(sum);
        };
        double energy = 0;
        for (const auto& e : edges) energy += e.Weight * Rho(p.SmoothnessPenalty, norm(u, e.A, u, e.B), p.PenaltyDelta);
        for (std::size_t i = 0; i < f.size() / channels; ++i) energy += p.FitWeight * Rho(p.DataPenalty, norm(u, i, f, i), p.PenaltyDelta);
        return energy;
    }
}

TEST(VariationalFit, QuadraticFitIsOneImplicitDiffusionStepForEveryMass)
{
    std::mt19937 rng(7);
    const std::size_t n = 14;
    const auto edges = RandomGraph(n, rng);
    std::uniform_real_distribution<double> value(-3, 3), mass(0.5, 2);
    std::vector<double> f(2 * n), masses(n);
    for (auto& x : f) x = value(rng);
    for (auto& m : masses) m = mass(rng);
    const std::vector<std::size_t> fixed{2, 9};
    for (auto laplacian : {S::PropertyLaplacian::RandomWalk, S::PropertyLaplacian::Combinatorial, S::PropertyLaplacian::LumpedMass})
    {
        auto p = Fit(S::FitPenalty::Quadratic, S::FitPenalty::Quadratic, 0.4);
        p.Laplacian = laplacian;
        const auto fit = H::FitProperty(f, 2, edges, p, fixed, masses);
        ASSERT_TRUE(fit.Success) << fit.Diagnostic;
        EXPECT_EQ(fit.Stats.ReweightIterations, 1u);
        S::PropertyFilterParams implicit{.Method = S::PropertyFilter::Implicit, .Laplacian = laplacian, .TimeStep = 1 / 0.4};
        const auto step = S::FilterProperty(f, 2, edges, implicit, fixed, masses);
        ASSERT_TRUE(step.Success) << step.Diagnostic;
        for (std::size_t j = 0; j < f.size(); ++j) EXPECT_NEAR(fit.Values[j], step.Values[j], 1e-10);
        for (auto row : fixed) EXPECT_EQ(fit.Values[row * 2], f[row * 2]);
    }
}

TEST(VariationalFit, TotalVariationShrinksAStepWithoutBlurringIt)
{
    // Exact TV solution on a path: each plateau of five rows moves 1 / (2 lambda 5) toward the other.
    std::vector<double> f(10, 0.0);
    std::fill(f.begin() + 5, f.end(), 1.0);
    auto p = Fit(S::FitPenalty::L1, S::FitPenalty::Quadratic, 1.0);
    p.PenaltyDelta = 1e-8;
    const auto tv = H::FitProperty(f, 1, Path(10), p);
    ASSERT_TRUE(tv.Success) << tv.Diagnostic;
    for (std::size_t i = 0; i < 10; ++i) EXPECT_NEAR(tv.Values[i], i < 5 ? 0.1 : 0.9, 1e-5);
    const auto blurred = H::FitProperty(f, 1, Path(10), Fit(S::FitPenalty::Quadratic, S::FitPenalty::Quadratic, 1.0));
    ASSERT_TRUE(blurred.Success);
    EXPECT_GT(blurred.Values[4] - blurred.Values[0], 0.05) << "quadratic smoothing ramps into the step";
}

TEST(VariationalFit, TwoRowTotalVariationMatchesClosedForm)
{
    // E = |u1 - u0| + 2 (u0^2 + (1 - u1)^2) has its minimum at (1/4, 3/4).
    auto p = Fit(S::FitPenalty::L1, S::FitPenalty::Quadratic, 2.0);
    p.PenaltyDelta = 1e-9;
    const auto fit = H::FitProperty(std::vector<double>{0, 1}, 1, Path(2), p);
    ASSERT_TRUE(fit.Success) << fit.Diagnostic;
    EXPECT_NEAR(fit.Values[0], 0.25, 1e-6);
    EXPECT_NEAR(fit.Values[1], 0.75, 1e-6);
}

TEST(VariationalFit, RobustDataTermRejectsAnOutlier)
{
    std::vector<double> f(11, 0.0);
    f[5] = 10;
    const auto quadratic = H::FitProperty(f, 1, Path(11), Fit(S::FitPenalty::Quadratic, S::FitPenalty::Quadratic, 1.0));
    auto p = Fit(S::FitPenalty::Quadratic, S::FitPenalty::L1, 1.0);
    p.PenaltyDelta = 1e-8;
    const auto robust = H::FitProperty(f, 1, Path(11), p);
    p.DataPenalty = S::FitPenalty::Huber;
    p.PenaltyDelta = 0.1;
    const auto huber = H::FitProperty(f, 1, Path(11), p);
    ASSERT_TRUE(quadratic.Success && robust.Success && huber.Success) << robust.Diagnostic << huber.Diagnostic;
    EXPECT_GT(quadratic.Values[5], 2.0);
    EXPECT_GT(quadratic.Values[4], 0.5);
    EXPECT_LT(robust.Values[5], 0.5) << "the L1 data term keeps only a small residue of the spike";
    EXPECT_LT(std::abs(robust.Values[4]), 0.1);
    EXPECT_LT(huber.Values[5], 1.0);
}

TEST(VariationalFit, EveryPenaltyAndBoundReachesAConstrainedMinimum)
{
    std::mt19937 rng(11);
    const std::size_t n = 12, channels = 2;
    const auto edges = RandomGraph(n, rng);
    std::uniform_real_distribution<double> value(-2, 2), unit(-1, 1);
    std::vector<double> f(n * channels);
    for (auto& x : f) x = value(rng);
    for (auto smoothness : {S::FitPenalty::Quadratic, S::FitPenalty::Huber, S::FitPenalty::L1})
        for (auto data : {S::FitPenalty::Quadratic, S::FitPenalty::Huber, S::FitPenalty::L1})
            for (auto bound : {S::FitBound::None, S::FitBound::Uniform})
            {
                auto p = Fit(smoothness, data, 0.7);
                p.PenaltyDelta = 0.2;
                p.Bound = bound;
                p.BoundRadius = 0.3;
                const auto fit = H::FitProperty(f, channels, edges, p);
                ASSERT_TRUE(fit.Success) << fit.Diagnostic;
                const double energy = Energy(fit.Values, f, channels, edges, p);
                EXPECT_NEAR(fit.Stats.Energy, energy, 1e-9 * std::max(1.0, energy));
                if (bound == S::FitBound::Uniform)
                {
                    EXPECT_GT(fit.Stats.ActiveBounds, 0u);
                    for (std::size_t j = 0; j < f.size(); ++j) EXPECT_LE(std::abs(fit.Values[j] - f[j]), 0.3 + 1e-9);
                }
                // Convex energy: no feasible nearby point is lower.
                for (int trial = 0; trial < 60; ++trial)
                {
                    auto moved = fit.Values;
                    for (std::size_t j = 0; j < f.size(); ++j)
                    {
                        moved[j] += 1e-3 * unit(rng);
                        if (bound == S::FitBound::Uniform) moved[j] = std::clamp(moved[j], f[j] - 0.3, f[j] + 0.3);
                    }
                    EXPECT_GE(Energy(moved, f, channels, edges, p), energy - 1e-9 * std::max(1.0, energy))
                        << "smoothness " << int(smoothness) << " data " << int(data) << " bound " << int(bound);
                }
            }
}

TEST(VariationalFit, PerRowBoundsPinZeroRadiusRowsAndWideBoundsAreInactive)
{
    std::mt19937 rng(3);
    const std::size_t n = 10;
    const auto edges = RandomGraph(n, rng);
    std::uniform_real_distribution<double> value(-1, 1);
    std::vector<double> f(n);
    for (auto& x : f) x = value(rng);
    auto p = Fit(S::FitPenalty::Quadratic, S::FitPenalty::Quadratic, 0.1);
    p.Bound = S::FitBound::PerRow;
    std::vector<double> radii(n, 0.05);
    radii[3] = 0;
    const auto bounded = H::FitProperty(f, 1, edges, p, {}, {}, radii);
    ASSERT_TRUE(bounded.Success) << bounded.Diagnostic;
    EXPECT_EQ(bounded.Values[3], f[3]);
    for (std::size_t i = 0; i < n; ++i) EXPECT_LE(std::abs(bounded.Values[i] - f[i]), radii[i] + 1e-12);
    std::ranges::fill(radii, 100.0);
    const auto wide = H::FitProperty(f, 1, edges, p, {}, {}, radii);
    p.Bound = S::FitBound::None;
    const auto free = H::FitProperty(f, 1, edges, p);
    ASSERT_TRUE(wide.Success && free.Success);
    EXPECT_EQ(wide.Stats.ActiveBounds, 0u);
    for (std::size_t i = 0; i < n; ++i) EXPECT_NEAR(wide.Values[i], free.Values[i], 1e-12);
}

TEST(VariationalFit, NoiseLevelChoosesTheWeightWhoseResidualMatches)
{
    std::mt19937 rng(5);
    const std::size_t n = 40;
    const auto edges = RandomGraph(n, rng);
    std::normal_distribution<double> noise(0, 0.2);
    std::vector<double> f(n);
    for (std::size_t i = 0; i < n; ++i) f[i] = std::sin(0.3 * double(i)) + noise(rng);
    for (auto data : {S::FitPenalty::Quadratic, S::FitPenalty::Huber})
    {
        auto p = Fit(S::FitPenalty::Quadratic, data, 1.0);
        p.Fidelity = S::FitFidelity::NoiseLevel;
        p.NoiseLevel = 0.15;
        p.Laplacian = S::PropertyLaplacian::RandomWalk;
        const auto fit = H::FitProperty(f, 1, edges, p);
        ASSERT_TRUE(fit.Success) << fit.Diagnostic;
        EXPECT_FALSE(fit.Stats.NoiseTargetClamped);
        EXPECT_NEAR(fit.Stats.RmsResidual, 0.15, 0.15 * 2e-4);
        p.Fidelity = S::FitFidelity::FixedWeight;
        p.FitWeight = fit.Stats.FitWeight;
        const auto fixed = H::FitProperty(f, 1, edges, p);
        ASSERT_TRUE(fixed.Success);
        for (std::size_t i = 0; i < n; ++i) EXPECT_NEAR(fixed.Values[i], fit.Values[i], 1e-9);
    }
    auto p = Fit(S::FitPenalty::Quadratic, S::FitPenalty::Quadratic, 1.0);
    p.Fidelity = S::FitFidelity::NoiseLevel;
    p.NoiseLevel = 100;
    const auto flat = H::FitProperty(f, 1, edges, p);
    ASSERT_TRUE(flat.Success);
    EXPECT_TRUE(flat.Stats.NoiseTargetClamped) << "no fit deviates by 100 from a unit-scale signal";
}

TEST(VariationalFit, InvalidInputsAndNonConvergenceReturnNoValues)
{
    const std::vector<double> f{0, 1, 0, 1};
    const auto edges = Path(4);
    auto p = Fit(S::FitPenalty::Quadratic, S::FitPenalty::Quadratic, 1.0);
    const auto rejected = [&](const S::PropertyFilterParams& params, std::span<const double> radii = {}) {
        const auto result = H::FitProperty(f, 1, edges, params, {}, {}, radii);
        return !result.Success && result.Values.empty();
    };
    auto wrong = p;
    wrong.Method = S::PropertyFilter::Implicit;
    EXPECT_TRUE(rejected(wrong));
    EXPECT_FALSE(S::FilterProperty(f, 1, edges, p).Success) << "FilterProperty does not solve fits";
    for (double bad : {0.0, -1.0, std::numeric_limits<double>::infinity()})
    {
        auto q = p; q.FitWeight = bad; EXPECT_TRUE(rejected(q));
        q = p; q.PenaltyDelta = bad; EXPECT_TRUE(rejected(q));
        q = p; q.NoiseLevel = bad; EXPECT_TRUE(rejected(q));
    }
    auto bounded = p;
    bounded.Bound = S::FitBound::PerRow;
    EXPECT_TRUE(rejected(bounded));
    const std::vector<double> negative{0.1, -0.1, 0.1, 0.1};
    EXPECT_TRUE(rejected(bounded, negative));
    bounded.Bound = S::FitBound::Uniform;
    bounded.BoundRadius = -1;
    EXPECT_TRUE(rejected(bounded));
    auto slow = Fit(S::FitPenalty::L1, S::FitPenalty::Quadratic, 1.0);
    slow.MaxFitIterations = 1;
    slow.PenaltyDelta = 1e-9;
    EXPECT_TRUE(rejected(slow));
    const std::vector<double> nan{0, std::nan(""), 0, 1};
    EXPECT_FALSE(H::FitProperty(nan, 1, edges, p).Success);
    auto lumped = p;
    lumped.Laplacian = S::PropertyLaplacian::LumpedMass;
    EXPECT_TRUE(rejected(lumped));
}

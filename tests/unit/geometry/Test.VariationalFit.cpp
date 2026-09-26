#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <vector>
#include <glm/glm.hpp>
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
        p.MaxFitIterations = 100000;
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
        EXPECT_EQ(fit.Stats.Iterations, 1u);
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
                for (auto solver : {S::FitSolver::Reweighted, S::FitSolver::Admm})
            {
                auto p = Fit(smoothness, data, 0.7);
                p.FitAlgorithm = solver;
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
                        << "smoothness " << int(smoothness) << " data " << int(data) << " bound " << int(bound)
                        << " solver " << int(solver);
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

TEST(VariationalFit, AdmmMatchesTheReweightedReferenceWithOneFactorization)
{
    std::mt19937 rng(17);
    const std::size_t n = 16, channels = 2;
    const auto edges = RandomGraph(n, rng);
    std::uniform_real_distribution<double> value(-2, 2);
    std::vector<double> f(n * channels);
    for (auto& x : f) x = value(rng);
    const std::vector<std::size_t> fixed{4};
    for (auto smoothness : {S::FitPenalty::Quadratic, S::FitPenalty::Huber, S::FitPenalty::L1})
        for (auto data : {S::FitPenalty::Quadratic, S::FitPenalty::Huber, S::FitPenalty::L1})
            for (auto bound : {S::FitBound::None, S::FitBound::Uniform})
            {
                SCOPED_TRACE(int(smoothness) * 100 + int(data) * 10 + int(bound));
                auto p = Fit(smoothness, data, 0.7);
                p.PenaltyDelta = 0.2;
                p.Bound = bound;
                p.BoundRadius = 0.3;
                const auto reference = H::FitProperty(f, channels, edges, p, fixed);
                p.FitAlgorithm = S::FitSolver::Admm;
                const auto admm = H::FitProperty(f, channels, edges, p, fixed);
                ASSERT_TRUE(reference.Success && admm.Success) << reference.Diagnostic << admm.Diagnostic;
                EXPECT_EQ(admm.Diagnostic, "cpu_admm_sparse_cholesky");
                EXPECT_EQ(admm.Stats.Factorizations, 1u);
                EXPECT_LE(admm.Stats.PrimalResidual, 1e-11 * 4);
                EXPECT_NEAR(admm.Stats.Energy, reference.Stats.Energy, 1e-8 * std::max(1.0, reference.Stats.Energy));
                for (std::size_t j = 0; j < f.size(); ++j) EXPECT_NEAR(admm.Values[j], reference.Values[j], 1e-6);
                EXPECT_EQ(admm.Values[4 * channels], f[4 * channels]);
            }
}

TEST(VariationalFit, AdmmSolvesUndampedTotalVariationExactly)
{
    auto p = Fit(S::FitPenalty::L1, S::FitPenalty::Quadratic, 2.0);
    p.FitAlgorithm = S::FitSolver::Admm;
    p.PenaltyDelta = 0;
    const auto pair = H::FitProperty(std::vector<double>{0, 1}, 1, Path(2), p);
    ASSERT_TRUE(pair.Success) << pair.Diagnostic;
    EXPECT_NEAR(pair.Values[0], 0.25, 1e-9);
    EXPECT_NEAR(pair.Values[1], 0.75, 1e-9);
    std::vector<double> f(10, 0.0);
    std::fill(f.begin() + 5, f.end(), 1.0);
    p.FitWeight = 1.0;
    const auto step = H::FitProperty(f, 1, Path(10), p);
    ASSERT_TRUE(step.Success) << step.Diagnostic;
    for (std::size_t i = 0; i < 10; ++i) EXPECT_NEAR(step.Values[i], i < 5 ? 0.1 : 0.9, 1e-9);
    // Undamped L1 data with quadratic smoothness removes the outlier's pull except a residue.
    std::vector<double> spike(11, 0.0);
    spike[5] = 10;
    auto robust = Fit(S::FitPenalty::Quadratic, S::FitPenalty::L1, 1.0);
    robust.FitAlgorithm = S::FitSolver::Admm;
    robust.PenaltyDelta = 0;
    const auto cleaned = H::FitProperty(spike, 1, Path(11), robust);
    ASSERT_TRUE(cleaned.Success) << cleaned.Diagnostic;
    EXPECT_LT(cleaned.Values[5], 0.5);
}

TEST(VariationalFit, AdmmNoiseLevelReusesItsFactorization)
{
    std::mt19937 rng(23);
    const std::size_t n = 40;
    const auto edges = RandomGraph(n, rng);
    std::normal_distribution<double> noise(0, 0.2);
    std::vector<double> f(n);
    for (std::size_t i = 0; i < n; ++i) f[i] = std::sin(0.3 * double(i)) + noise(rng);
    auto p = Fit(S::FitPenalty::L1, S::FitPenalty::Quadratic, 1.0);
    p.FitAlgorithm = S::FitSolver::Admm;
    p.PenaltyDelta = 0;
    p.FitTolerance = 1e-9;
    p.Fidelity = S::FitFidelity::NoiseLevel;
    p.NoiseLevel = 0.15;
    const auto fit = H::FitProperty(f, 1, edges, p);
    ASSERT_TRUE(fit.Success) << fit.Diagnostic;
    EXPECT_EQ(fit.Stats.Factorizations, 1u);
    EXPECT_FALSE(fit.Stats.NoiseTargetClamped);
    EXPECT_NEAR(fit.Stats.RmsResidual, 0.15, 0.15 * 1e-3);
}

TEST(VariationalFit, ZeroDeltaNeedsAdmmAndNoHuberPenalty)
{
    const std::vector<double> f{0, 1, 0, 1};
    auto p = Fit(S::FitPenalty::L1, S::FitPenalty::Quadratic, 1.0);
    p.PenaltyDelta = 0;
    EXPECT_FALSE(H::FitProperty(f, 1, Path(4), p).Success) << "reweighting weights 1/delta are undefined";
    p.FitAlgorithm = S::FitSolver::Admm;
    EXPECT_TRUE(H::FitProperty(f, 1, Path(4), p).Success);
    p.DataPenalty = S::FitPenalty::Huber;
    EXPECT_FALSE(H::FitProperty(f, 1, Path(4), p).Success) << "Huber with delta 0 is no penalty";
    p.DataPenalty = S::FitPenalty::Quadratic;
    p.MaxFitIterations = 1;
    p.FitTolerance = 1e-14;
    const auto slow = H::FitProperty(f, 1, Path(4), p);
    EXPECT_FALSE(slow.Success);
    EXPECT_TRUE(slow.Values.empty());
}

TEST(VariationalFit, EuclideanBoundsLimitTheVectorDeviationAndMatchBoxesForScalars)
{
    std::mt19937 rng(29);
    const std::size_t n = 12, channels = 3;
    const auto edges = RandomGraph(n, rng);
    std::uniform_real_distribution<double> value(-2, 2), unit(-1, 1);
    std::vector<double> f(n * channels);
    for (auto& x : f) x = value(rng);
    auto p = Fit(S::FitPenalty::L1, S::FitPenalty::Quadratic, 0.5);
    p.FitAlgorithm = S::FitSolver::Admm;
    p.PenaltyDelta = 0.1;
    p.Bound = S::FitBound::Uniform;
    p.BoundRadius = 0.3;
    p.BoundNorm = S::FitBoundNorm::Euclidean;
    const auto fit = H::FitProperty(f, channels, edges, p);
    ASSERT_TRUE(fit.Success) << fit.Diagnostic;
    EXPECT_GT(fit.Stats.ActiveBounds, 0u);
    const auto deviation = [&](const std::vector<double>& u, std::size_t i) {
        double sum = 0;
        for (std::size_t c = 0; c < channels; ++c) sum += (u[i * channels + c] - f[i * channels + c]) * (u[i * channels + c] - f[i * channels + c]);
        return std::sqrt(sum);
    };
    for (std::size_t i = 0; i < n; ++i) EXPECT_LE(deviation(fit.Values, i), 0.3 + 1e-12);
    const double energy = Energy(fit.Values, f, channels, edges, p);
    for (int trial = 0; trial < 60; ++trial)
    {
        auto moved = fit.Values;
        for (auto& x : moved) x += 1e-3 * unit(rng);
        for (std::size_t i = 0; i < n; ++i)
            if (const double d = deviation(moved, i); d > 0.3)
                for (std::size_t c = 0; c < channels; ++c)
                    moved[i * channels + c] = f[i * channels + c] + (moved[i * channels + c] - f[i * channels + c]) * 0.3 / d;
        EXPECT_GE(Energy(moved, f, channels, edges, p), energy - 1e-9 * std::max(1.0, energy));
    }
    // A scalar ball is an interval: the reweighted box reference applies.
    std::vector<double> scalar(n);
    for (auto& x : scalar) x = value(rng);
    const auto ball = H::FitProperty(scalar, 1, edges, p);
    auto box = p;
    box.FitAlgorithm = S::FitSolver::Reweighted;
    box.BoundNorm = S::FitBoundNorm::PerChannel;
    const auto reference = H::FitProperty(scalar, 1, edges, box);
    ASSERT_TRUE(ball.Success && reference.Success);
    for (std::size_t i = 0; i < n; ++i) EXPECT_NEAR(ball.Values[i], reference.Values[i], 1e-6);
    box.BoundNorm = S::FitBoundNorm::Euclidean;
    EXPECT_FALSE(H::FitProperty(scalar, 1, edges, box).Success) << "Euclidean bounds need ADMM";
}

namespace
{
    // Random points in a box with symmetric 8-nearest-neighbor edges.
    std::vector<double> RandomPoints(std::size_t n, std::mt19937& rng, std::vector<Edge>& edges)
    {
        std::uniform_real_distribution<double> coordinate(0, 1);
        std::vector<glm::vec3> points(n);
        std::vector<double> flat;
        for (auto& x : points)
        {
            x = {float(coordinate(rng)), float(coordinate(rng)), float(coordinate(rng))};
            flat.insert(flat.end(), {double(x.x), double(x.y), double(x.z)});
        }
        edges = *S::BuildPropertyNeighborhood(points, 8, S::PropertyWeight::Uniform, 1.0);
        return flat;
    }
    S::PropertyFilterParams Tgv(double lambda)
    {
        auto p = Fit(S::FitPenalty::L1, S::FitPenalty::Quadratic, lambda);
        p.FitAlgorithm = S::FitSolver::Admm;
        p.SmoothnessOrder = S::FitOrder::Second;
        p.PenaltyDelta = 0;
        p.FitTolerance = 1e-10;
        return p;
    }
}

TEST(VariationalFit, SecondOrderKeepsAffineFieldsThatFirstOrderFlattens)
{
    std::mt19937 rng(31);
    const std::size_t n = 60;
    std::vector<Edge> edges;
    const auto x = RandomPoints(n, rng, edges);
    std::vector<double> f(2 * n);
    for (std::size_t i = 0; i < n; ++i)
    {
        f[2 * i] = 2 * x[3 * i] - x[3 * i + 1] + 0.5 * x[3 * i + 2] + 1;
        f[2 * i + 1] = -3 * x[3 * i + 2];
    }
    auto p = Tgv(0.01);
    const auto tgv = H::FitProperty(f, 2, edges, p, {}, {}, {}, x);
    ASSERT_TRUE(tgv.Success) << tgv.Diagnostic;
    EXPECT_EQ(tgv.Stats.Factorizations, 1u);
    for (std::size_t j = 0; j < f.size(); ++j) EXPECT_NEAR(tgv.Values[j], f[j], 1e-6);
    EXPECT_NEAR(tgv.Stats.Energy, 0.0, 1e-6);
    p.SmoothnessOrder = S::FitOrder::First;
    const auto tv = H::FitProperty(f, 2, edges, p);
    ASSERT_TRUE(tv.Success) << tv.Diagnostic;
    double flattened = 0;
    for (std::size_t j = 0; j < f.size(); ++j) flattened = std::max(flattened, std::abs(tv.Values[j] - f[j]));
    EXPECT_GT(flattened, 0.1) << "total variation pulls a ramp toward its mean";
}

TEST(VariationalFit, SecondOrderAvoidsStaircasingAndKeepsAJump)
{
    // A noisy ramp with a jump in the middle, sampled on a line.
    const std::size_t n = 80;
    std::vector<double> x(3 * n, 0.0), clean(n), f(n);
    std::mt19937 rng(37);
    std::uniform_real_distribution<double> noise(-0.05, 0.05);
    for (std::size_t i = 0; i < n; ++i)
    {
        x[3 * i] = double(i) / double(n);
        clean[i] = x[3 * i] + (2 * i < n ? 0.0 : 1.0);
        f[i] = clean[i] + noise(rng);
    }
    const auto edges = Path(n);
    auto p = Tgv(5.0);
    p.SecondOrderWeight = 4;
    const auto tgv = H::FitProperty(f, 1, edges, p, {}, {}, {}, x);
    p.SmoothnessOrder = S::FitOrder::First;
    const auto tv = H::FitProperty(f, 1, edges, p);
    ASSERT_TRUE(tgv.Success && tv.Success) << tgv.Diagnostic << tv.Diagnostic;
    const auto error = [&](const std::vector<double>& u) {
        double sum = 0;
        for (std::size_t i = 0; i < n; ++i) sum += (u[i] - clean[i]) * (u[i] - clean[i]);
        return std::sqrt(sum / double(n));
    };
    const auto plateaus = [&](const std::vector<double>& u) {
        std::size_t flat = 0;
        for (std::size_t i = 0; i + 1 < n; ++i) flat += std::abs(u[i + 1] - u[i]) < 1e-6;
        return flat;
    };
    const double noiseRms = 0.1 / std::sqrt(12.0);
    EXPECT_LT(error(tgv.Values), 0.6 * error(tv.Values));
    EXPECT_LT(error(tgv.Values), 0.5 * noiseRms);
    EXPECT_GT(plateaus(tv.Values), n / 4) << "TV staircases the ramp";
    EXPECT_LT(plateaus(tgv.Values), n / 10);
    EXPECT_GT(tgv.Values[n / 2] - tgv.Values[n / 2 - 1], 0.9) << "the jump survives";
}

TEST(VariationalFit, QuadraticSecondOrderMatchesADenseOracle)
{
    std::mt19937 rng(41);
    const std::size_t n = 12;
    std::vector<Edge> edges;
    const auto x = RandomPoints(n, rng, edges);
    std::uniform_real_distribution<double> value(-1, 1);
    std::vector<double> f(n);
    for (auto& v : f) v = value(rng);
    auto p = Tgv(0.3);
    p.SmoothnessPenalty = S::FitPenalty::Quadratic;
    p.SecondOrderWeight = 0.7;
    p.FitTolerance = 1e-12;
    const auto fit = H::FitProperty(f, 1, edges, p, {}, {}, {}, x);
    ASSERT_TRUE(fit.Success) << fit.Diagnostic;
    // Unknowns (u, g); stationarity of sum w |A1|^2 + alpha sum w h^2 |g_a - g_b|^2 + lambda sum |u - f|^2.
    double h = 0;
    for (const auto& e : edges)
        h += std::sqrt(std::pow(x[3 * e.A] - x[3 * e.B], 2) + std::pow(x[3 * e.A + 1] - x[3 * e.B + 1], 2) +
                       std::pow(x[3 * e.A + 2] - x[3 * e.B + 2], 2));
    h /= double(edges.size());
    const std::size_t m = 4 * n;
    std::vector<std::vector<double>> A(m, std::vector<double>(m + 1, 0.0));
    for (std::size_t i = 0; i < n; ++i) { A[i][i] += 0.3; A[i][m] += 0.3 * f[i]; }
    for (const auto& e : edges)
    {
        std::vector<std::pair<std::size_t, double>> a{{e.A, 1.0}, {e.B, -1.0}};
        for (std::size_t k = 0; k < 3; ++k)
        {
            const double d = x[3 * e.A + k] - x[3 * e.B + k];
            a.push_back({n + 3 * e.A + k, -0.5 * d});
            a.push_back({n + 3 * e.B + k, -0.5 * d});
        }
        for (auto [i, ai] : a) for (auto [j, aj] : a) A[i][j] += e.Weight * ai * aj;
        for (std::size_t k = 0; k < 3; ++k)
        {
            const auto ga = n + 3 * e.A + k, gb = n + 3 * e.B + k;
            const double w = 0.7 * e.Weight * h * h;
            A[ga][ga] += w; A[gb][gb] += w; A[ga][gb] -= w; A[gb][ga] -= w;
        }
    }
    for (std::size_t col = 0; col < m; ++col)
    {
        std::size_t pivot = col;
        for (std::size_t row = col + 1; row < m; ++row) if (std::abs(A[row][col]) > std::abs(A[pivot][col])) pivot = row;
        std::swap(A[col], A[pivot]);
        for (std::size_t row = 0; row < m; ++row)
            if (row != col)
            {
                const double factor = A[row][col] / A[col][col];
                for (std::size_t k = col; k <= m; ++k) A[row][k] -= factor * A[col][k];
            }
    }
    for (std::size_t i = 0; i < n; ++i) EXPECT_NEAR(fit.Values[i], A[i][m] / A[i][i], 1e-6);
}

TEST(VariationalFit, SecondOrderNeedsAdmmAndPositions)
{
    const std::vector<double> f{0, 1, 2, 3}, x{0, 0, 0, 1, 0, 0, 2, 0, 0, 3, 0, 0};
    auto p = Tgv(1.0);
    EXPECT_FALSE(H::FitProperty(f, 1, Path(4), p).Success) << "positions are required";
    const std::vector<double> nan{0, 0, 0, std::nan(""), 0, 0, 2, 0, 0, 3, 0, 0};
    EXPECT_FALSE(H::FitProperty(f, 1, Path(4), p, {}, {}, {}, nan).Success);
    const auto ramp = H::FitProperty(f, 1, Path(4), p, {}, {}, {}, x);
    ASSERT_TRUE(ramp.Success) << ramp.Diagnostic;
    for (std::size_t i = 0; i < 4; ++i) EXPECT_NEAR(ramp.Values[i], f[i], 1e-7);
    p.FitAlgorithm = S::FitSolver::Reweighted;
    p.PenaltyDelta = 0.1;
    EXPECT_FALSE(H::FitProperty(f, 1, Path(4), p, {}, {}, {}, x).Success);
    p.FitAlgorithm = S::FitSolver::Admm;
    p.SecondOrderWeight = 0;
    EXPECT_FALSE(H::FitProperty(f, 1, Path(4), p, {}, {}, {}, x).Success);
}

// GEOM-111: unified progressive point sampling. The hole-sieve farthest-point order must equal
// a brute-force float64 scan with the same operation order and tie rule (smallest index).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <numeric>
#include <random>
#include <set>
#include <vector>

#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include "ProgressivePoissonReference.hpp"

import Geometry.PointSampling;

namespace PS = Geometry::PointSampling;
namespace
{
    struct Soa
    {
        std::vector<double> X, Y, Z;
        [[nodiscard]] PS::PointView View() const { return {X, Y, Z}; }
        [[nodiscard]] std::size_t Size() const { return X.size(); }
    };

    Soa Cloud(const std::size_t count, const std::uint32_t seed, const bool planar, const std::size_t duplicates)
    {
        std::mt19937 random(seed);
        std::uniform_real_distribution<double> uniform(-1.0, 1.0);
        Soa points;
        for (std::size_t i = 0; i < count; ++i)
        {
            points.X.push_back(uniform(random));
            points.Y.push_back(0.5 * uniform(random));
            points.Z.push_back(planar ? 0.0 : 0.3 * uniform(random));
        }
        // Exact duplicates of earlier points exercise zero clearances and ties.
        for (std::size_t k = 0; k < duplicates; ++k)
        {
            const std::size_t source = (k * 7u) % count;
            points.X.push_back(points.X[source]);
            points.Y.push_back(points.Y[source]);
            points.Z.push_back(points.Z[source]);
        }
        return points;
    }

    // Brute-force (weighted) farthest-point order with the sieve's arithmetic and tie rule.
    std::pair<std::vector<std::uint32_t>, std::vector<double>> BruteForce(const Soa& p, const std::size_t count,
                                                                          const std::uint32_t first,
                                                                          const std::vector<double>& weights)
    {
        const std::size_t n = p.Size();
        std::vector<double> clear(n, std::numeric_limits<double>::infinity());
        std::vector<bool> selected(n, false);
        std::vector<std::uint32_t> order;
        std::vector<double> keys;
        std::size_t next = first;
        double nextKey = std::numeric_limits<double>::infinity();
        while (order.size() < std::min(count, n))
        {
            order.push_back(std::uint32_t(next));
            keys.push_back(nextKey);
            selected[next] = true;
            for (std::size_t i = 0; i < n; ++i)
            {
                if (selected[i]) continue;
                const double dx = p.X[i] - p.X[next], dy = p.Y[i] - p.Y[next], dz = p.Z[i] - p.Z[next];
                double s = dx * dx;
                s += dy * dy;
                s += dz * dz;
                clear[i] = std::min(clear[i], s);
            }
            nextKey = -1.0;
            for (std::size_t i = 0; i < n; ++i)
            {
                if (selected[i]) continue;
                const double key = weights.empty() ? clear[i] : weights[i] * clear[i];
                if (key > nextKey) { nextKey = key; next = i; }
            }
        }
        return {order, keys};
    }
}

TEST(PointSampling, FarthestPointOrderEqualsBruteForce)
{
    for (const bool planar : {false, true})
        for (const std::uint32_t leafSize : {1u, 16u, 64u})
        {
            const Soa points = Cloud(900, planar ? 5u : 6u, planar, 60);
            const std::uint32_t first = 17u;
            const auto [expected, keys] = BruteForce(points, points.Size(), first, {});
            const auto result = PS::Order(points.View(), {.Method = PS::Method::FarthestPoint, .FirstIndex = first,
                                                          .LeafSize = leafSize}, points.Size());
            ASSERT_TRUE(result.Succeeded()) << PS::ToString(result.State);
            ASSERT_EQ(result.Order, expected) << "planar=" << planar << " leaf=" << leafSize;
            ASSERT_EQ(result.Clearance, keys);
            // Every point appears once; duplicates come last with zero clearance.
            EXPECT_EQ(std::set<std::uint32_t>(result.Order.begin(), result.Order.end()).size(), points.Size());
            EXPECT_EQ(result.Clearance.back(), 0.0);
        }
}

TEST(PointSampling, WeightedFarthestPointOrderEqualsBruteForce)
{
    const Soa points = Cloud(700, 9u, false, 20);
    std::vector<double> weights(points.Size());
    std::mt19937 random(3);
    std::uniform_real_distribution<double> uniform(1.0, 8.0);
    for (auto& w : weights) w = uniform(random);
    const auto [expected, keys] = BruteForce(points, 300, 0u, weights);
    const auto result = PS::Order(points.View(), {.Method = PS::Method::FarthestPoint, .Weights = weights}, 300);
    ASSERT_TRUE(result.Succeeded());
    EXPECT_EQ(result.Order, expected);
    EXPECT_EQ(result.Clearance, keys);
}

TEST(PointSampling, SieveExtendsIncrementallyAndPrunesPairs)
{
    const Soa points = Cloud(20000, 11u, false, 0);
    PS::FarthestPointSieve sieve;
    ASSERT_EQ(sieve.Build(points.View()), PS::Status::Success);
    EXPECT_EQ(sieve.Extend(100), 100u);
    EXPECT_EQ(sieve.Extend(50), 100u) << "a shorter request does no work";
    EXPECT_EQ(sieve.Extend(1000), 1000u);
    const auto oneShot = PS::Order(points.View(), {}, 1000);
    EXPECT_TRUE(std::ranges::equal(sieve.Order(), oneShot.Order));
    // A flat scan evaluates about N pairs per sample.
    const double flat = double(points.Size()) * 1000.0;
    std::printf("hole sieve: %llu pairs for 1000 of 20000 points (%.1fx fewer than a flat scan)\n",
                static_cast<unsigned long long>(sieve.DistancePairs()), flat / double(sieve.DistancePairs()));
    EXPECT_LT(double(sieve.DistancePairs()), 0.2 * flat);
}

TEST(PointSampling, RandomOrderIsASeededPrefixOfAPermutation)
{
    const Soa points = Cloud(500, 13u, false, 0);
    const auto a = PS::Order(points.View(), {.Method = PS::Method::Random, .Seed = 42u}, 500);
    const auto b = PS::Order(points.View(), {.Method = PS::Method::Random, .Seed = 42u}, 120);
    const auto c = PS::Order(points.View(), {.Method = PS::Method::Random, .Seed = 43u}, 500);
    ASSERT_TRUE(a.Succeeded() && b.Succeeded() && c.Succeeded());
    EXPECT_EQ(std::set<std::uint32_t>(a.Order.begin(), a.Order.end()).size(), 500u);
    EXPECT_TRUE(std::equal(b.Order.begin(), b.Order.end(), a.Order.begin())) << "prefixes agree";
    EXPECT_NE(a.Order, c.Order);
    EXPECT_TRUE(a.Clearance.empty());
}

TEST(PointSampling, InvalidInputFailsClosed)
{
    const Soa points = Cloud(10, 1u, false, 0);
    EXPECT_EQ(PS::Order(PS::PointView{}, {}, 5).State, PS::Status::EmptyInput);
    EXPECT_EQ(PS::Order(points.View(), {.FirstIndex = 10u}, 5).State, PS::Status::InvalidParameters);
    EXPECT_EQ(PS::Order(points.View(), {.LeafSize = 0u}, 5).State, PS::Status::InvalidParameters);
    const std::vector<double> shortWeights(3, 1.0), negative(10, -1.0);
    EXPECT_EQ(PS::Order(points.View(), {.Weights = shortWeights}, 5).State, PS::Status::InvalidParameters);
    EXPECT_EQ(PS::Order(points.View(), {.Weights = negative}, 5).State, PS::Status::InvalidParameters);
    Soa bad = points;
    bad.Y[4] = std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(PS::Order(bad.View(), {}, 5).State, PS::Status::NonFiniteInput);
    Soa huge = points;
    huge.X[2] = 1e160;
    EXPECT_EQ(PS::Order(huge.View(), {}, 5).State, PS::Status::UnsupportedMagnitude);
    const std::vector<glm::vec3> single{{1.0f, 2.0f, 3.0f}};
    const auto one = PS::Order(single, {}, 4);
    ASSERT_TRUE(one.Succeeded());
    EXPECT_EQ(one.Order, std::vector<std::uint32_t>{0u});
}

TEST(PointSampling, ProgressivePoissonMatchesTheMethod012Reference)
{
    std::vector<glm::vec3> points;
    std::mt19937 random(21);
    std::uniform_real_distribution<float> uniform(-1.0f, 1.0f);
    for (int i = 0; i < 3000; ++i) points.push_back({uniform(random), uniform(random), 0.5f * uniform(random)});
    namespace PPR = Intrinsic::Methods::Geometry::ProgressivePoissonReference;
    const auto reference = PPR::Compute(points, PPR::Config{});
    const auto full = PS::Order(points, {.Method = PS::Method::ProgressivePoisson}, points.size());
    ASSERT_TRUE(full.Succeeded());
    EXPECT_EQ(full.Order, reference.Order);
    EXPECT_EQ(full.SplatRadii, reference.SplatRadii);
    EXPECT_EQ(full.LevelOffsets, reference.LevelOffsets);
    EXPECT_EQ(full.BaseRadius, reference.BaseRadius);
    const auto prefix = PS::Order(points, {.Method = PS::Method::ProgressivePoisson}, 100);
    ASSERT_EQ(prefix.Order.size(), 100u);
    EXPECT_TRUE(std::equal(prefix.Order.begin(), prefix.Order.end(), reference.Order.begin()));
    EXPECT_EQ(PS::Order(points, {.Method = PS::Method::ProgressivePoisson, .Poisson = {.Dimension = 4u}}, 10).State,
              PS::Status::InvalidParameters);
}

TEST(PointSampling, PoissonProfilesAndPriorityReachTheReference)
{
    std::vector<glm::vec3> points;
    std::mt19937 random(31);
    std::uniform_real_distribution<float> uniform(-1.0f, 1.0f);
    for (int i = 0; i < 2000; ++i) points.push_back({uniform(random), uniform(random), uniform(random)});
    namespace PPR = Intrinsic::Methods::Geometry::ProgressivePoissonReference;
    const PS::PoissonSettings fast = PS::WithProfile({}, PS::PoissonProfile::Fast);
    const auto viaApi = PS::Order(points, {.Method = PS::Method::ProgressivePoisson, .Poisson = fast}, points.size());
    const auto direct = PPR::Compute(points, PPR::WithProfile({}, PPR::Profile::Fast));
    ASSERT_TRUE(viaApi.Succeeded());
    EXPECT_EQ(viaApi.Order, direct.Order);
    std::vector<float> scores;
    for (const auto& p : points) scores.push_back(p.y);
    PS::PoissonSettings priority{};
    priority.Selection = PS::PoissonCellSelection::FeaturePriority;
    priority.ComputeSplatRadii = false;
    const auto prioritized = PS::Order(points, {.Method = PS::Method::ProgressivePoisson, .Poisson = priority,
                                                .PriorityScores = scores}, 50);
    ASSERT_TRUE(prioritized.Succeeded());
    EXPECT_EQ(prioritized.Order.size(), 50u);
    EXPECT_TRUE(prioritized.SplatRadii.empty());
    EXPECT_EQ(PS::Order(points, {.Method = PS::Method::ProgressivePoisson, .Poisson = priority}, 50).State,
              PS::Status::InvalidParameters) << "priority without scores";
}

// ── GEOM-113: approximate farthest-point family and sample elimination ──────
namespace
{
    // Brute force: the largest (weighted) clearance among the points not yet in `prefix`.
    double LargestRemainingKey(const Soa& p, const std::vector<std::uint32_t>& prefix, const std::vector<double>& weights)
    {
        std::vector<bool> taken(p.Size(), false);
        for (const std::uint32_t id : prefix) taken[id] = true;
        double best = -1.0;
        for (std::size_t i = 0; i < p.Size(); ++i)
        {
            if (taken[i]) continue;
            double clear = std::numeric_limits<double>::infinity();
            for (const std::uint32_t id : prefix)
            {
                const double dx = p.X[i] - p.X[id], dy = p.Y[i] - p.Y[id], dz = p.Z[i] - p.Z[id];
                double s = dx * dx;
                s += dy * dy;
                s += dz * dz;
                clear = std::min(clear, s);
            }
            best = std::max(best, weights.empty() ? clear : weights[i] * clear);
        }
        return best;
    }

    void ExpectPermutationPrefix(const PS::Result& r, const std::size_t n)
    {
        ASSERT_TRUE(r.Succeeded()) << PS::ToString(r.State);
        EXPECT_EQ(std::set<std::uint32_t>(r.Order.begin(), r.Order.end()).size(), r.Order.size());
        for (const std::uint32_t id : r.Order) ASSERT_LT(id, n);
    }
}

TEST(PointSampling, CoupledSieveWithEtaOneIsExactFarthestPoint)
{
    const Soa points = Cloud(1500, 41u, false, 30);
    std::vector<double> weights(points.Size());
    std::mt19937 random(8);
    std::uniform_real_distribution<double> uniform(1.0, 4.0);
    for (auto& w : weights) w = uniform(random);
    for (const bool weighted : {false, true})
    {
        const std::span<const double> w = weighted ? std::span<const double>(weights) : std::span<const double>{};
        const auto exact = PS::Order(points.View(), {.Weights = w}, points.Size());
        const auto coupled = PS::Order(points.View(), {.Method = PS::Method::CoupledSieve, .Weights = w, .Eta = 1.0,
                                                       .CandidateCap = 1u}, points.Size());
        ASSERT_TRUE(coupled.Succeeded());
        EXPECT_EQ(coupled.Order, exact.Order);
        EXPECT_EQ(coupled.Clearance, exact.Clearance);
    }
    EXPECT_EQ(PS::Order(points.View(), {.Method = PS::Method::CoupledSieve, .Eta = 1.0, .CandidateCap = 4u}, 10).State,
              PS::Status::InvalidParameters) << "eta = 1 needs a one-point cap";
}

TEST(PointSampling, CoupledSieveKeepsTheEtaBoundOnEveryPrefix)
{
    const Soa points = Cloud(600, 42u, false, 10);
    const double eta = 0.8;
    const auto r = PS::Order(points.View(), {.Method = PS::Method::CoupledSieve, .Eta = eta, .CandidateCap = 16u}, 250);
    ExpectPermutationPrefix(r, points.Size());
    ASSERT_EQ(r.Order.size(), 250u);
    ASSERT_EQ(r.BatchOffsets.back(), 250u);
    EXPECT_LT(r.BatchOffsets.size(), 250u) << "batches hold several points";
    for (std::size_t k = 1; k < r.Order.size(); ++k)
    {
        const std::vector<std::uint32_t> prefix(r.Order.begin(), r.Order.begin() + std::ptrdiff_t(k));
        // The emitted point's priority reaches eta^2 of the largest remaining priority.
        ASSERT_GE(r.Clearance[k], eta * eta * LargestRemainingKey(points, prefix, {}) * (1.0 - 1e-12)) << k;
    }
    // Every count is a prefix of the same order.
    const auto shorter = PS::Order(points.View(), {.Method = PS::Method::CoupledSieve, .Eta = eta, .CandidateCap = 16u}, 97);
    EXPECT_TRUE(std::equal(shorter.Order.begin(), shorter.Order.end(), r.Order.begin()));
}

TEST(PointSampling, FlatGreedyKeepsTheBetaBoundAndBetaOneIsExact)
{
    const Soa points = Cloud(800, 43u, false, 12);
    const auto exact = PS::Order(points.View(), {}, points.Size());
    const auto betaOne = PS::Order(points.View(), {.Method = PS::Method::FlatGreedy, .Beta = 1.0}, points.Size());
    ASSERT_TRUE(betaOne.Succeeded());
    EXPECT_EQ(betaOne.Order, exact.Order);
    const double beta = 1.25;
    for (const auto priority : {PS::GreedyPriority::Random, PS::GreedyPriority::Clearance, PS::GreedyPriority::CoverageGain})
    {
        const auto r = PS::Order(points.View(), {.Method = PS::Method::FlatGreedy, .Beta = beta, .BatchPriority = priority,
                                                 .BatchOrdering = priority}, 300);
        ExpectPermutationPrefix(r, points.Size());
        ASSERT_EQ(r.Order.size(), 300u);
        // Each batch's points keep clearance >= U / beta against the prefix and each other.
        for (std::size_t b = 1; b + 1 < r.BatchOffsets.size(); ++b)
        {
            const std::uint32_t begin = r.BatchOffsets[b];
            const std::vector<std::uint32_t> prefix(r.Order.begin(), r.Order.begin() + begin);
            const double largest = LargestRemainingKey(points, prefix, {});
            if (!(largest > 0.0)) break; // duplicate tail
            for (std::uint32_t k = begin; k < r.BatchOffsets[b + 1]; ++k)
                ASSERT_GE(r.Clearance[k], largest / (beta * beta) * (1.0 - 1e-12)) << "batch " << b;
        }
        const auto shorter = PS::Order(points.View(), {.Method = PS::Method::FlatGreedy, .Beta = beta,
                                                       .BatchPriority = priority, .BatchOrdering = priority}, 111);
        EXPECT_TRUE(std::equal(shorter.Order.begin(), shorter.Order.end(), r.Order.begin()));
    }
}

TEST(PointSampling, SampleEliminationSpreadsItsPrefixes)
{
    const Soa points = Cloud(3000, 44u, false, 0);
    const auto r = PS::Order(points.View(), {.Method = PS::Method::SampleElimination}, 400);
    ExpectPermutationPrefix(r, points.Size());
    ASSERT_EQ(r.Order.size(), 400u);
    EXPECT_EQ(PS::Order(points.View(), {.Method = PS::Method::SampleElimination}, 400).Order, r.Order) << "deterministic";
    const auto random = PS::Order(points.View(), {.Method = PS::Method::Random, .Seed = 5u}, 400);
    const auto minSpacing = [&](const std::vector<std::uint32_t>& order, const std::size_t count) {
        double best = std::numeric_limits<double>::infinity();
        for (std::size_t a = 0; a < count; ++a)
            for (std::size_t b = a + 1; b < count; ++b)
            {
                const double dx = points.X[order[a]] - points.X[order[b]], dy = points.Y[order[a]] - points.Y[order[b]],
                             dz = points.Z[order[a]] - points.Z[order[b]];
                best = std::min(best, dx * dx + dy * dy + dz * dz);
            }
        return std::sqrt(best);
    };
    // Progressive prefixes spread far better than random subsets (measured 3.5-5x). The full
    // first elimination keeps some close pairs at the unbounded boundary, as the method does
    // without tiling, so it is not compared.
    for (const std::size_t prefix : {std::size_t{25}, std::size_t{50}, std::size_t{200}})
        EXPECT_GT(minSpacing(r.Order, prefix), 2.0 * minSpacing(random.Order, prefix)) << prefix;
    EXPECT_EQ(PS::Order(points.View(), {.Method = PS::Method::SampleElimination, .ManifoldDimension = 4u}, 10).State,
              PS::Status::InvalidParameters);
}

TEST(PointSampling, LazyGreedyKeepsTheBetaBoundAndBetaOneIsExact)
{
    const Soa points = Cloud(700, 45u, false, 8);
    const auto exact = PS::Order(points.View(), {}, points.Size());
    EXPECT_EQ(PS::Order(points.View(), {.Method = PS::Method::LazyGreedy, .Beta = 1.0}, points.Size()).Order, exact.Order);
    const double beta = 1.2;
    for (const auto priority : {PS::LazyPriority::Random, PS::LazyPriority::VoidDensity})
    {
        const auto r = PS::Order(points.View(), {.Method = PS::Method::LazyGreedy, .Beta = beta,
                                                 .LazyBatchPriority = priority}, 250);
        ExpectPermutationPrefix(r, points.Size());
        ASSERT_EQ(r.Order.size(), 250u);
        for (std::size_t b = 1; b + 1 < r.BatchOffsets.size(); ++b)
        {
            const std::uint32_t begin = r.BatchOffsets[b];
            const std::vector<std::uint32_t> prefix(r.Order.begin(), r.Order.begin() + begin);
            const double largest = LargestRemainingKey(points, prefix, {});
            if (!(largest > 0.0)) break;
            for (std::uint32_t k = begin; k < r.BatchOffsets[b + 1]; ++k)
                ASSERT_GE(r.Clearance[k], largest / (beta * beta) * (1.0 - 1e-12)) << "batch " << b;
        }
    }
}

TEST(PointSampling, TournamentIsACompleteHierarchicalOrder)
{
    const Soa points = Cloud(1000, 46u, false, 0);
    const auto r = PS::Order(points.View(), {.Method = PS::Method::Tournament}, points.Size());
    ExpectPermutationPrefix(r, points.Size());
    ASSERT_EQ(r.Order.size(), points.Size());
    const auto prefix = PS::Order(points.View(), {.Method = PS::Method::Tournament}, 64);
    EXPECT_TRUE(std::equal(prefix.Order.begin(), prefix.Order.end(), r.Order.begin()));
    // A comparison baseline, not a quality method: its prefixes are not claimed to spread.
    EXPECT_EQ(PS::Order(points.View(), {.Method = PS::Method::Tournament}, points.Size()).Order, r.Order);
}

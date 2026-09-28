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

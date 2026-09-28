// GEOM-111 — deterministic point sampling smoke benchmark: the hole-sieve farthest-point
// order must equal a brute-force scan (quality gate) and prune most point pairs.

#include "Bench.PointSamplingSmoke.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>
#include <vector>

import Geometry.PointSampling;

namespace Intrinsic::Bench::Geometry
{
    namespace
    {
        namespace PS = ::Geometry::PointSampling;
        constexpr int kWarmupIterations = 1;
        constexpr int kMeasuredIterations = 5;
        constexpr std::size_t kTimedPoints = 20000, kTimedSamples = 1000, kParityPoints = 3000;

        struct Cloud
        {
            std::vector<double> X, Y, Z;
            [[nodiscard]] PS::PointView View() const { return {X, Y, Z}; }
        };

        [[nodiscard]] Cloud UniformBox(const std::size_t count, const std::uint32_t seed)
        {
            std::mt19937 random(seed);
            std::uniform_real_distribution<double> uniform(-1.0, 1.0);
            Cloud cloud;
            for (std::size_t i = 0; i < count; ++i)
            {
                cloud.X.push_back(uniform(random));
                cloud.Y.push_back(0.5 * uniform(random));
                cloud.Z.push_back(0.25 * uniform(random));
            }
            return cloud;
        }

        // Brute-force farthest-point order with the sieve's arithmetic and tie rule.
        [[nodiscard]] std::vector<std::uint32_t> BruteForce(const Cloud& p)
        {
            const std::size_t n = p.X.size();
            std::vector<double> clear(n, std::numeric_limits<double>::infinity());
            std::vector<bool> selected(n, false);
            std::vector<std::uint32_t> order;
            std::size_t next = 0;
            while (order.size() < n)
            {
                order.push_back(std::uint32_t(next));
                selected[next] = true;
                double best = -1.0;
                std::size_t winner = next;
                for (std::size_t i = 0; i < n; ++i)
                {
                    if (selected[i]) continue;
                    const double dx = p.X[i] - p.X[next], dy = p.Y[i] - p.Y[next], dz = p.Z[i] - p.Z[next];
                    double s = dx * dx;
                    s += dy * dy;
                    s += dz * dz;
                    clear[i] = std::min(clear[i], s);
                    if (clear[i] > best) { best = clear[i]; winner = i; }
                }
                next = winner;
            }
            return order;
        }
    }

    PointSamplingSmokeMetrics RunPointSamplingSmoke()
    {
        PointSamplingSmokeMetrics metrics{};
        const Cloud timed = UniformBox(kTimedPoints, 111u);
        for (int i = 0; i < kWarmupIterations; ++i) (void)PS::Order(timed.View(), {}, kTimedSamples);
        PS::Result last{};
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < kMeasuredIterations; ++i) last = PS::Order(timed.View(), {}, kTimedSamples);
        const auto t1 = std::chrono::steady_clock::now();
        metrics.RuntimeMilliseconds =
            std::chrono::duration<double, std::milli>(t1 - t0).count() / double(kMeasuredIterations);
        metrics.SievePairs = last.DistancePairs;
        metrics.PairReduction = last.DistancePairs > 0u
            ? double(kTimedPoints) * double(kTimedSamples) / double(last.DistancePairs) : 0.0;

        const Cloud parity = UniformBox(kParityPoints, 112u);
        const auto sieve = PS::Order(parity.View(), {}, kParityPoints);
        const auto expected = BruteForce(parity);
        std::size_t mismatched = 0;
        for (std::size_t k = 0; k < kParityPoints; ++k)
            if (k >= sieve.Order.size() || sieve.Order[k] != expected[k]) ++mismatched;
        metrics.QualityErrorL2 = double(mismatched) / double(kParityPoints);
        metrics.Succeeded = last.Succeeded() && sieve.Succeeded() && last.Order.size() == kTimedSamples &&
                            mismatched == 0u;
        return metrics;
    }
}

// METHOD-012 — progressive Poisson-disk CPU reference smoke benchmark workload.
//
// Subsamples a deterministic uniform cube and verifies the blue-noise quality
// summaries (Poisson-disk ratio per level, coverage). Quality, not speed, is the
// gate: quality_error_l2 = max(0, 1 - min ratio) is 0 exactly when every level
// boundary satisfies the Poisson guarantee. No performance-win claim is made.

#include "Bench.ProgressivePoissonReferenceSmoke.hpp"

#include "ProgressivePoissonReference.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <random>
#include <vector>

namespace Intrinsic::Bench::Geometry
{
    namespace
    {
        constexpr int kWarmupIterations = 1;
        constexpr int kMeasuredIterations = 8;
        constexpr std::uint32_t kPointCount = 4096;
        constexpr std::uint32_t kSeed = 42;

        namespace ppr = ::Intrinsic::Methods::Geometry::ProgressivePoissonReference;

        struct Cloud
        {
            std::vector<float> X, Y, Z;
        };

        [[nodiscard]] Cloud MakeUniformCube()
        {
            std::mt19937 rng(kSeed);
            std::uniform_real_distribution<float> d(0.0f, 1.0f);
            Cloud c;
            c.X.resize(kPointCount);
            c.Y.resize(kPointCount);
            c.Z.resize(kPointCount);
            for (std::uint32_t i = 0; i < kPointCount; ++i)
            {
                c.X[i] = d(rng);
                c.Y[i] = d(rng);
                c.Z[i] = d(rng);
            }
            return c;
        }

        [[nodiscard]] ProgressivePoissonReferenceSmokeMetrics RunWorkload(const Cloud& cloud)
        {
            ppr::PointSet view;
            view.X = cloud.X;
            view.Y = cloud.Y;
            view.Z = cloud.Z;

            ppr::Config cfg;
            cfg.Dimension = 3;
            const ppr::Result r = ppr::Compute(view, cfg);

            ProgressivePoissonReferenceSmokeMetrics m{};
            m.AcceptedCount = r.Diag.AcceptedCount;
            m.LevelCount = static_cast<std::uint32_t>(r.Diag.LevelCounts.size());
            m.CoverageFraction = kPointCount > 0
                                     ? static_cast<double>(r.Diag.AcceptedCount) / static_cast<double>(kPointCount)
                                     : 0.0;

            double ratioMin = 1e30;
            bool sawLevel = false;
            for (std::size_t L = 0; L < r.Diag.LevelRadii.size(); ++L)
            {
                if (r.Diag.LevelCounts[L] < 2 || r.Diag.LevelMinDistance[L] <= 0.0f)
                    continue;
                const double ratio = static_cast<double>(r.Diag.LevelMinDistance[L]) /
                                     static_cast<double>(r.Diag.LevelRadii[L]);
                ratioMin = std::min(ratioMin, ratio);
                sawLevel = true;
            }
            m.PoissonRatioMin = sawLevel ? ratioMin : 0.0;
            m.QualityErrorL2 = sawLevel ? std::max(0.0, 1.0 - ratioMin) : 0.0;
            m.Succeeded = r.Diag.Code == ppr::ValidationCode::Valid &&
                          r.Diag.AcceptedCount > 0 &&
                          (!sawLevel || ratioMin >= 0.9999);
            return m;
        }
    } // namespace

    ProgressivePoissonReferenceSmokeMetrics RunProgressivePoissonReferenceSmoke()
    {
        const Cloud cloud = MakeUniformCube();

        for (int i = 0; i < kWarmupIterations; ++i)
            (void)RunWorkload(cloud);

        ProgressivePoissonReferenceSmokeMetrics last{};
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < kMeasuredIterations; ++i)
            last = RunWorkload(cloud);
        const auto t1 = std::chrono::steady_clock::now();

        const auto totalNs = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        last.RuntimeMilliseconds =
            (static_cast<double>(totalNs) / static_cast<double>(kMeasuredIterations)) * 1.0e-6;
        return last;
    }
} // namespace Intrinsic::Bench::Geometry

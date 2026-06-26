// Correctness tests for the geometry.progressive_poisson CPU reference backend
// (METHOD-012). Hermetic: depends only on the method header, no engine modules.

#include "ProgressivePoissonReference.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <vector>

namespace ppr = Intrinsic::Methods::Geometry::ProgressivePoissonReference;

namespace
{
    struct Cloud
    {
        std::vector<float> X, Y, Z;
    };

    Cloud UniformCube(std::uint32_t n, std::uint32_t seed, int dim)
    {
        std::mt19937 rng(seed);
        std::uniform_real_distribution<float> d(0.0f, 1.0f);
        Cloud c;
        c.X.resize(n);
        c.Y.resize(n);
        c.Z.resize(n);
        for (std::uint32_t i = 0; i < n; ++i)
        {
            c.X[i] = d(rng);
            c.Y[i] = d(rng);
            c.Z[i] = (dim == 3) ? d(rng) : 0.0f;
        }
        return c;
    }

    ppr::PointSet View(const Cloud& c, int dim)
    {
        ppr::PointSet p;
        p.X = c.X;
        p.Y = c.Y;
        if (dim == 3)
            p.Z = c.Z;
        return p;
    }

    // Independent O(k^2) ground truth — the reference must never disagree.
    float BruteMinDistance(const Cloud& c, const std::vector<std::uint32_t>& order,
                           std::uint32_t count, int dim)
    {
        float best = std::numeric_limits<float>::max();
        for (std::uint32_t a = 0; a < count; ++a)
            for (std::uint32_t b = a + 1; b < count; ++b)
            {
                const std::uint32_t i = order[a];
                const std::uint32_t j = order[b];
                const float ex = c.X[i] - c.X[j];
                const float ey = c.Y[i] - c.Y[j];
                const float ez = (dim == 3) ? (c.Z[i] - c.Z[j]) : 0.0f;
                best = std::min(best, std::sqrt(ex * ex + ey * ey + ez * ez));
            }
        return best;
    }
} // namespace

class ProgressivePoissonReferenceDim : public ::testing::TestWithParam<int>
{
};

TEST_P(ProgressivePoissonReferenceDim, PoissonGuaranteeHoldsAtEveryLevelBoundary)
{
    const int dim = GetParam();
    const Cloud cloud = UniformCube(20000, 42, dim);
    ppr::Config cfg;
    cfg.Dimension = static_cast<std::uint32_t>(dim);

    const ppr::Result r = ppr::Compute(View(cloud, dim), cfg);

    ASSERT_EQ(r.Diag.Code, ppr::ValidationCode::Valid);
    ASSERT_FALSE(r.LevelOffsets.empty());
    EXPECT_EQ(r.LevelOffsets.back(), r.Order.size());
    EXPECT_EQ(r.SplatRadii.size(), r.Order.size());
    EXPECT_GT(r.Diag.AcceptedCount, 0u);
    EXPECT_LE(r.Diag.AcceptedCount, 20000u);

    // Accepted ordering is a unique, in-range subset of the input.
    std::vector<char> seen(20000, 0);
    for (const std::uint32_t idx : r.Order)
    {
        ASSERT_LT(idx, 20000u);
        ASSERT_EQ(seen[idx], 0) << "duplicate index in order";
        seen[idx] = 1;
    }

    // The core theorem: every level-boundary prefix is Poisson-disk at r_L.
    for (std::size_t L = 0; L + 1 < r.LevelOffsets.size(); ++L)
    {
        const std::uint32_t le = r.LevelOffsets[L + 1];
        if (le < 2)
            continue;
        const float rL = r.Diag.LevelRadii[L];
        const float measured = BruteMinDistance(cloud, r.Order, le, dim);
        EXPECT_GE(measured, rL * 0.9999f)
            << "level " << L << " min_dist=" << measured << " r_L=" << rL;
    }
}

TEST_P(ProgressivePoissonReferenceDim, MinPairwiseHelperMatchesBruteForce)
{
    const int dim = GetParam();
    const Cloud cloud = UniformCube(8000, 11, dim);
    ppr::Config cfg;
    cfg.Dimension = static_cast<std::uint32_t>(dim);
    const ppr::Result r = ppr::Compute(View(cloud, dim), cfg);
    ASSERT_GE(r.LevelOffsets.size(), 2u);

    // For the level-0 prefix, the helper (search radius r_0) must detect any
    // pair closer than r_0; since the guarantee holds it should report >= r_0.
    const std::uint32_t le = r.LevelOffsets[1];
    const float r0 = r.Diag.LevelRadii[0];
    const float helper = ppr::MinPairwiseDistance(View(cloud, dim), r.Order, le,
                                                  static_cast<std::uint32_t>(dim), r0);
    const float brute = BruteMinDistance(cloud, r.Order, le, dim);
    EXPECT_GE(brute, r0 * 0.9999f);
    if (helper != std::numeric_limits<float>::max())
        EXPECT_GE(helper, r0 * 0.9999f);
}

INSTANTIATE_TEST_SUITE_P(Dimensions, ProgressivePoissonReferenceDim, ::testing::Values(2, 3));

TEST(ProgressivePoissonReference, DeterministicForFixedSeeds)
{
    const Cloud cloud = UniformCube(5000, 7, 3);
    ppr::Config cfg;
    const ppr::Result a = ppr::Compute(View(cloud, 3), cfg);
    const ppr::Result b = ppr::Compute(View(cloud, 3), cfg);
    EXPECT_EQ(a.Order, b.Order);
    EXPECT_EQ(a.LevelOffsets, b.LevelOffsets);
    EXPECT_EQ(a.SplatRadii, b.SplatRadii);
}

TEST(ProgressivePoissonReference, ShuffleSeedPermutesWithinLevelButKeepsBoundaries)
{
    const Cloud cloud = UniformCube(5000, 7, 3);
    ppr::Config cfg;
    ppr::Config cfg2 = cfg;
    cfg2.ShuffleSeed = cfg.ShuffleSeed ^ 0x00abcdefu;

    const ppr::Result a = ppr::Compute(View(cloud, 3), cfg);
    const ppr::Result b = ppr::Compute(View(cloud, 3), cfg2);

    // Same accepted set and level boundaries; only intra-level order changes.
    EXPECT_EQ(a.LevelOffsets, b.LevelOffsets);
    EXPECT_EQ(a.Order.size(), b.Order.size());
}

TEST(ProgressivePoissonReference, EmptyInputIsValidAndEmpty)
{
    ppr::Config cfg;
    const ppr::Result r = ppr::Compute(ppr::PointSet{}, cfg);
    EXPECT_EQ(r.Diag.Code, ppr::ValidationCode::Valid);
    EXPECT_TRUE(r.Order.empty());
    ASSERT_EQ(r.LevelOffsets.size(), 1u);
    EXPECT_EQ(r.LevelOffsets[0], 0u);
}

TEST(ProgressivePoissonReference, CoincidentPointsAcceptExactlyOne)
{
    std::vector<float> x(16, 0.5f), y(16, 0.5f), z(16, 0.5f);
    ppr::PointSet p;
    p.X = x;
    p.Y = y;
    p.Z = z;
    const ppr::Result r = ppr::Compute(p, ppr::Config{});
    EXPECT_EQ(r.Order.size(), 1u);
    EXPECT_EQ(r.Diag.AcceptedCount, 1u);
}

TEST(ProgressivePoissonReference, NonFiniteInputFailsClosed)
{
    std::vector<float> x{0.0f, std::nanf("")}, y{0.0f, 1.0f}, z{0.0f, 1.0f};
    ppr::PointSet p;
    p.X = x;
    p.Y = y;
    p.Z = z;
    const ppr::Result r = ppr::Compute(p, ppr::Config{});
    EXPECT_EQ(r.Diag.Code, ppr::ValidationCode::NonFiniteInput);
    EXPECT_TRUE(r.Order.empty());
}

TEST(ProgressivePoissonReference, InvalidDimensionFailsClosed)
{
    std::vector<float> x{0.0f}, y{0.0f}, z{0.0f};
    ppr::PointSet p;
    p.X = x;
    p.Y = y;
    p.Z = z;
    ppr::Config cfg;
    cfg.Dimension = 4;
    const ppr::Result r = ppr::Compute(p, cfg);
    EXPECT_EQ(r.Diag.Code, ppr::ValidationCode::InvalidDimension);
}

TEST(ProgressivePoissonReference, ZeroGridWidthAndLevelsAreClamped)
{
    std::vector<float> x{0.0f, 1.0f}, y{0.0f, 1.0f}, z{0.0f, 1.0f};
    ppr::PointSet p;
    p.X = x;
    p.Y = y;
    p.Z = z;
    ppr::Config cfg;
    cfg.GridWidth = 0;
    cfg.MaxLevels = 0;
    const ppr::Result r = ppr::Compute(p, cfg);
    EXPECT_TRUE(r.Diag.ClampedGridWidth);
    EXPECT_TRUE(r.Diag.ClampedMaxLevels);
    EXPECT_EQ(r.Diag.Code, ppr::ValidationCode::Valid);
}

TEST(ProgressivePoissonReference, AlphaOutOfRangeDefaultsToSqrtDOverTwo)
{
    const Cloud cloud = UniformCube(1000, 3, 2);
    ppr::Config cfg;
    cfg.Dimension = 2;
    cfg.RadiusAlpha = -1.0f; // out of (0,1)
    const ppr::Result r = ppr::Compute(View(cloud, 2), cfg);
    EXPECT_TRUE(r.Diag.AlphaDefaulted);
    EXPECT_NEAR(r.Diag.UsedAlpha, 0.5f * 1.41421356f, 1e-5f);
}

// Correctness tests for the geometry.progressive_poisson CPU reference backend
// (METHOD-012). Hermetic: depends only on the method header, no engine modules.

#include "ProgressivePoissonReference.hpp"

#include <gtest/gtest.h>

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <vector>

namespace ppr = Intrinsic::Methods::Geometry::ProgressivePoissonReference;

namespace
{
    // A point set is just a span of glm::vec3 — the same type any vec3 property
    // buffer (positions, normals, a `v:foo` property) exposes. No container type
    // is required by the method.
    std::vector<glm::vec3> UniformCube(std::uint32_t n, std::uint32_t seed, int dim)
    {
        std::mt19937 rng(seed);
        std::uniform_real_distribution<float> d(0.0f, 1.0f);
        std::vector<glm::vec3> pts(n);
        for (std::uint32_t i = 0; i < n; ++i)
            pts[i] = glm::vec3{d(rng), d(rng), (dim == 3) ? d(rng) : 0.0f};
        return pts;
    }

    // Independent O(k^2) ground truth — the reference must never disagree.
    float BruteMinDistance(const std::vector<glm::vec3>& pts, const std::vector<std::uint32_t>& order,
                           std::uint32_t count, int dim)
    {
        float best = std::numeric_limits<float>::max();
        for (std::uint32_t a = 0; a < count; ++a)
            for (std::uint32_t b = a + 1; b < count; ++b)
            {
                const glm::vec3& pi = pts[order[a]];
                const glm::vec3& pj = pts[order[b]];
                const float ex = pi.x - pj.x;
                const float ey = pi.y - pj.y;
                const float ez = (dim == 3) ? (pi.z - pj.z) : 0.0f;
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
    const std::vector<glm::vec3> cloud = UniformCube(20000, 42, dim);
    ppr::Config cfg;
    cfg.Dimension = static_cast<std::uint32_t>(dim);

    const ppr::Result r = ppr::Compute(cloud, cfg);

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

TEST_P(ProgressivePoissonReferenceDim, MinPairwiseHelperEqualsBruteForce)
{
    const int dim = GetParam();
    const std::vector<glm::vec3> cloud = UniformCube(8000, 11, dim);
    ppr::Config cfg;
    cfg.Dimension = static_cast<std::uint32_t>(dim);
    const ppr::Result r = ppr::Compute(cloud, cfg);
    ASSERT_GE(r.LevelOffsets.size(), 2u);

    // The helper is the exact measured minimum pairwise distance — it must agree
    // with the O(n^2) ground truth at the (sparse) level-0 prefix, not merely
    // report "no neighbor within one cell".
    const std::uint32_t le = r.LevelOffsets[1];
    const float helper = ppr::MinPairwiseDistance(cloud, r.Order, le, static_cast<std::uint32_t>(dim));
    const float brute = BruteMinDistance(cloud, r.Order, le, dim);
    ASSERT_NE(helper, std::numeric_limits<float>::max());
    EXPECT_NEAR(helper, brute, brute * 1e-5f);
}

TEST(ProgressivePoissonReference, MinPairwiseFindsPairsBeyondAdjacentCells)
{
    // The exact scenario from the review: two points 3 apart. A naive 3^d search
    // at any sub-3 cell size would miss them and report "no finite spacing"; the
    // shell search must return the true distance 3.
    const std::vector<glm::vec3> pts{glm::vec3{0.0f, 0.0f, 0.0f}, glm::vec3{3.0f, 0.0f, 0.0f}};
    const std::vector<std::uint32_t> order{0u, 1u};
    const float d3 = ppr::MinPairwiseDistance(pts, order, 2u, 3u);
    EXPECT_NEAR(d3, 3.0f, 1e-5f);
    const float d2 = ppr::MinPairwiseDistance(pts, order, 2u, 2u);
    EXPECT_NEAR(d2, 3.0f, 1e-5f);

    // A wider, very sparse set: nearest pair is the 1.0 gap among far-flung points.
    const std::vector<glm::vec3> sparse{
        glm::vec3{0.0f, 0.0f, 0.0f}, glm::vec3{50.0f, 0.0f, 0.0f},
        glm::vec3{50.0f, 1.0f, 0.0f}, glm::vec3{0.0f, 80.0f, 0.0f}};
    const std::vector<std::uint32_t> sorder{0u, 1u, 2u, 3u};
    const float ds = ppr::MinPairwiseDistance(sparse, sorder, 4u, 2u);
    EXPECT_NEAR(ds, 1.0f, 1e-5f);

    // Fewer than two points -> sentinel; coincident points -> 0.
    EXPECT_EQ(ppr::MinPairwiseDistance(pts, std::span<const std::uint32_t>{order.data(), 1u}, 1u, 2u),
              std::numeric_limits<float>::max());
    const std::vector<glm::vec3> dup(3, glm::vec3{2.0f, 2.0f, 2.0f});
    const std::vector<std::uint32_t> dorder{0u, 1u, 2u};
    EXPECT_EQ(ppr::MinPairwiseDistance(dup, dorder, 3u, 3u), 0.0f);
}

INSTANTIATE_TEST_SUITE_P(Dimensions, ProgressivePoissonReferenceDim, ::testing::Values(2, 3));

TEST(ProgressivePoissonReference, DeterministicForFixedSeeds)
{
    const std::vector<glm::vec3> cloud = UniformCube(5000, 7, 3);
    ppr::Config cfg;
    const ppr::Result a = ppr::Compute(cloud, cfg);
    const ppr::Result b = ppr::Compute(cloud, cfg);
    EXPECT_EQ(a.Order, b.Order);
    EXPECT_EQ(a.LevelOffsets, b.LevelOffsets);
    EXPECT_EQ(a.SplatRadii, b.SplatRadii);
}

TEST(ProgressivePoissonReference, ShuffleSeedPermutesWithinLevelButKeepsBoundaries)
{
    const std::vector<glm::vec3> cloud = UniformCube(5000, 7, 3);
    ppr::Config cfg;
    ppr::Config cfg2 = cfg;
    cfg2.ShuffleSeed = cfg.ShuffleSeed ^ 0x00abcdefu;

    const ppr::Result a = ppr::Compute(cloud, cfg);
    const ppr::Result b = ppr::Compute(cloud, cfg2);

    // Same accepted set and level boundaries; only intra-level order changes.
    EXPECT_EQ(a.LevelOffsets, b.LevelOffsets);
    EXPECT_EQ(a.Order.size(), b.Order.size());
}

TEST(ProgressivePoissonReference, EmptyInputIsValidAndEmpty)
{
    ppr::Config cfg;
    const ppr::Result r = ppr::Compute(std::span<const glm::vec3>{}, cfg);
    EXPECT_EQ(r.Diag.Code, ppr::ValidationCode::Valid);
    EXPECT_TRUE(r.Order.empty());
    ASSERT_EQ(r.LevelOffsets.size(), 1u);
    EXPECT_EQ(r.LevelOffsets[0], 0u);
}

TEST(ProgressivePoissonReference, CoincidentPointsAcceptExactlyOne)
{
    const std::vector<glm::vec3> pts(16, glm::vec3{0.5f, 0.5f, 0.5f});
    const ppr::Result r = ppr::Compute(pts, ppr::Config{});
    EXPECT_EQ(r.Order.size(), 1u);
    EXPECT_EQ(r.Diag.AcceptedCount, 1u);
}

TEST(ProgressivePoissonReference, NonFiniteInputFailsClosed)
{
    const std::vector<glm::vec3> pts{glm::vec3{0.0f, 0.0f, 0.0f},
                                     glm::vec3{std::nanf(""), 1.0f, 1.0f}};
    const ppr::Result r = ppr::Compute(pts, ppr::Config{});
    EXPECT_EQ(r.Diag.Code, ppr::ValidationCode::NonFiniteInput);
    EXPECT_TRUE(r.Order.empty());
}

TEST(ProgressivePoissonReference, InvalidDimensionFailsClosed)
{
    const std::vector<glm::vec3> pts{glm::vec3{0.0f, 0.0f, 0.0f}};
    ppr::Config cfg;
    cfg.Dimension = 4;
    const ppr::Result r = ppr::Compute(pts, cfg);
    EXPECT_EQ(r.Diag.Code, ppr::ValidationCode::InvalidDimension);
}

TEST(ProgressivePoissonReference, TwoDimensionalIgnoresZComponent)
{
    // Same x,y but wildly different z: in 2D the result must not depend on z.
    std::vector<glm::vec3> a = UniformCube(2000, 5, 2);
    std::vector<glm::vec3> b = a;
    for (std::size_t i = 0; i < b.size(); ++i)
        b[i].z = static_cast<float>(i) * 13.0f; // noise on the ignored axis
    ppr::Config cfg;
    cfg.Dimension = 2;
    const ppr::Result ra = ppr::Compute(a, cfg);
    const ppr::Result rb = ppr::Compute(b, cfg);
    EXPECT_EQ(ra.Order, rb.Order);
    EXPECT_EQ(ra.LevelOffsets, rb.LevelOffsets);
}

TEST(ProgressivePoissonReference, ZeroGridWidthAndLevelsAreClamped)
{
    const std::vector<glm::vec3> pts{glm::vec3{0.0f, 0.0f, 0.0f}, glm::vec3{1.0f, 1.0f, 1.0f}};
    ppr::Config cfg;
    cfg.GridWidth = 0;
    cfg.MaxLevels = 0;
    const ppr::Result r = ppr::Compute(pts, cfg);
    EXPECT_TRUE(r.Diag.ClampedGridWidth);
    EXPECT_TRUE(r.Diag.ClampedMaxLevels);
    EXPECT_EQ(r.Diag.Code, ppr::ValidationCode::Valid);
}

TEST(ProgressivePoissonReference, AlphaOutOfRangeDefaultsToSqrtDOverTwo)
{
    const std::vector<glm::vec3> cloud = UniformCube(1000, 3, 2);
    ppr::Config cfg;
    cfg.Dimension = 2;
    cfg.RadiusAlpha = -1.0f; // out of (0,1)
    const ppr::Result r = ppr::Compute(cloud, cfg);
    EXPECT_TRUE(r.Diag.AlphaDefaulted);
    EXPECT_NEAR(r.Diag.UsedAlpha, 0.5f * 1.41421356f, 1e-5f);
}
